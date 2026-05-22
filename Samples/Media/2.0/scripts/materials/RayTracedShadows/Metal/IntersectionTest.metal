#include <metal_stdlib>
#include <simd/simd.h>

#define GEOMETRY_MASK_TRIANGLE 1
#define GEOMETRY_MASK_SPHERE   2
#define GEOMETRY_MASK_LIGHT    4

#define GEOMETRY_MASK_GEOMETRY (GEOMETRY_MASK_TRIANGLE | GEOMETRY_MASK_SPHERE)

#define RAY_MASK_PRIMARY   (GEOMETRY_MASK_GEOMETRY | GEOMETRY_MASK_LIGHT)
#define RAY_MASK_SHADOW    GEOMETRY_MASK_GEOMETRY
#define RAY_MASK_SECONDARY GEOMETRY_MASK_GEOMETRY

using namespace metal;
using namespace raytracing;

struct INPUT
{
    float4x4 invProjectionMat;
    float4x4 invViewMat;
    float4x4 invViewProjMat;
    float4 cameraCorner0;
    float4 cameraCorner1;
    float4 cameraCorner2;
    float4 cameraCorner3;
    float4 cameraPos;
    float4 cameraRight;
    float4 cameraUp;
    float4 cameraFront;
    float2 projectionParams;
    float width;
    float height;
    float fovY;
};

struct Light
{
    
    float4 position;    //.w contains the light type marker from getAs4DVector
    float4 diffuse;     //.w contains numCollectedLights in lights[0]
    float4 specular;
    float4 attenuation; //.x contains range
    //Spotlights:
    //  spotDirection.xyz is direction
    //  spotParams.xyz contains falloff params
    float4 spotDirection;
    float4 spotParams;

#define lightTexProfileIdx spotDirection.w
};

float origin() { return 1.0f / 32.0f; }
float float_scale() { return 1.0f / 65536.0f; }
float int_scale() { return 256.0f; }
// Normal points outward for rays exiting the surface, else is flipped. 6 float3 offset_ray(const float3 p, const float3 n)
float3 offset_ray(const float3 p, const float3 n)
{
    int3 of_i(int_scale() * n.x, int_scale() * n.y, int_scale() * n.z);
    
    float3 p_i(
               as_type<float>(as_type<int>(p.x)+((p.x < 0) ? -of_i.x : of_i.x)),
               as_type<float>(as_type<int>(p.y)+((p.y < 0) ? -of_i.y : of_i.y)),
               as_type<float>(as_type<int>(p.z)+((p.z < 0) ? -of_i.z : of_i.z)));
    
    return float3(fabs(p.x) < origin() ? p.x+ float_scale()*n.x : p_i.x,
                  fabs(p.y) < origin() ? p.y+ float_scale()*n.y : p_i.y,
                  fabs(p.z) < origin() ? p.z+ float_scale()*n.z : p_i.z);
}

kernel void main_metal
(
    depth2d<@insertpiece(texture0_pf_type), access::read> depthTexture [[texture(0)]],
    texture2d<@insertpiece(texture1_pf_type), access::read> normalsTexture [[texture(1)]],
//    sampler  samplerState    [[sampler(0)]],
	texture2d<float, access::write> shadowTexture [[texture(UAV_SLOT_START)]], // Destination
 
 
    //constant float2 &projectionParams    [[buffer(PARAMETER_SLOT)]], // TODO PARAMTER_SLOT should be const buffer??
    
    constant Light *lights, // TODO replace with correct light source.
    constant INPUT *in,
 
    instance_acceleration_structure accelerationStructure,
    intersection_function_table<triangle_data, instancing> intersectionFunctionTable,
    

	ushort3 gl_LocalInvocationID	    [[thread_position_in_threadgroup]],
	ushort3 gl_GlobalInvocationID	    [[thread_position_in_grid]],
    ushort3 gl_WorkGroupID              [[threads_per_threadgroup]]
)
{
    // The sample aligns the thread count to the threadgroup size. which means the thread count
    // may be different than the bounds of the texture. Test to make sure this thread
    // is referencing a pixel within the bounds of the texture.
    if (gl_GlobalInvocationID.x >= in->width || gl_GlobalInvocationID.y >= in->height)
        return;

    ushort3 pixelPos = ( gl_GlobalInvocationID /* * gl_WorkGroupID*/ );// + gl_LocalInvocationID;
    float2 pixel = (float2)gl_GlobalInvocationID.xy;
    float fDepth = depthTexture.read( pixelPos.xy );
    float linearDepth = in->projectionParams.y / ( fDepth - in->projectionParams.x );
    
    float2 uv = float2( pixel.x / in->width, pixel.y / in->height );
//        uv.x = uv.x * 2.0f - 1.0f;
//        uv.y = ( 1.0f - uv.y ) * 2.0f - 1.0f;

    // uv must be between 0.0 and 1.0 not -1.0 and 1.0!!!
    float3 interp = mix( mix( in->cameraCorner0.xyz, in->cameraCorner2.xyz, uv.x ),
                         mix( in->cameraCorner1.xyz, in->cameraCorner3.xyz, uv.x ),
                         uv.y );

    float3 worldSpacePosition = in->cameraPos.xyz + interp * linearDepth;

    float3 viewSpaceNormal = normalize( normalsTexture.read( pixelPos.xy ).xyz * 2.0f - 1.0f );
    float3 worldSpaceNormal = normalize( ( in->invViewMat * float4( viewSpaceNormal, 0.0f ) ).xyz );
    
    // Create an intersector to test for intersection between the ray and the geometry in the scene.
    intersector<triangle_data, instancing> i;
    
    // Shadow rays check only whether there is an object between the intersection point
    // and the light source. Tell Metal to return after finding any intersection.
    i.accept_any_intersection( true );
    
    typename intersector<triangle_data, instancing>::result_type intersection;

    const uint maxSupportedLights = 16u;
    uint numLights = min( (uint)lights[0].diffuse.w, maxSupportedLights );
    uint selectedDirectionalIdx = maxSupportedLights;

    for( uint lightIdx = 0u; lightIdx < numLights; ++lightIdx )
    {
        const uint lightType = (uint)( lights[lightIdx].spotParams.w + 0.5f );
        if( lightType == 0u )
        {
            selectedDirectionalIdx = lightIdx;
            break;
        }
    }

    float directionalVisibleWeight = 0.0f;
    float directionalTotalWeight = 0.0f;
    float localVisibleWeight = 0.0f;
    float localTotalWeight = 0.0f;

    for( uint lightIdx = 0u; lightIdx < numLights; ++lightIdx )
    {
        constant Light &light = lights[lightIdx];
        const uint lightType = (uint)( light.spotParams.w + 0.5f );

        if( lightType == 0u && lightIdx != selectedDirectionalIdx )
            continue;

        if( lightType != 0u && lightType != 1u && lightType != 2u )
            continue;

        ray shadowRay;
        shadowRay.min_distance = 0.005f;
        shadowRay.max_distance = INFINITY;

        bool traceShadowRay = true;
        // Converts RGB light color/intensity into a single scalar “brightness” weight using Rec. 709/sRGB luminance coefficients:
        float lightWeight = max( dot( light.diffuse.xyz, float3( 0.2126f, 0.7152f, 0.0722f ) ), 0.0f );

        if( lightType == 0u )
        {
            shadowRay.direction = normalize( light.position.xyz );
        }
        else if( lightType == 1u || lightType == 2u )
        {
            float3 toLight = light.position.xyz - worldSpacePosition.xyz;
            float lightDistance = length( toLight );
            traceShadowRay = lightDistance > shadowRay.min_distance && lightDistance <= light.attenuation.x;

            if( traceShadowRay )
            {
                shadowRay.direction = toLight / lightDistance;
                shadowRay.max_distance = max( lightDistance - shadowRay.min_distance, 0.0f );

                float attenuation = 1.0f / ( 0.5f + ( light.attenuation.y + light.attenuation.z * lightDistance ) * lightDistance );
                lightWeight *= max( attenuation, 0.0f );

                if( lightType == 2u )
                {
                    float3 lightToSurfaceDir = -shadowRay.direction;
                    float spotCosAngle = dot( lightToSurfaceDir, normalize( light.spotDirection.xyz ) );
                    traceShadowRay = spotCosAngle >= light.spotParams.y;

                    if( traceShadowRay )
                    {
                        float spotAtten = saturate( ( spotCosAngle - light.spotParams.y ) /
                                                    max( light.spotParams.x - light.spotParams.y, 1e-4f ) );
                        lightWeight *= pow( spotAtten, light.spotParams.z );
                    }
                }
            }
        }

        if( lightWeight <= 0.0f )
            continue;

        float visibility = 1.0f;
        if( traceShadowRay )
        {
            float3 rayBiasNormal = dot( worldSpaceNormal, shadowRay.direction ) < 0.0f ?
                -worldSpaceNormal : worldSpaceNormal;
            shadowRay.origin = offset_ray( worldSpacePosition.xyz, rayBiasNormal );

            intersection = i.intersect( shadowRay, accelerationStructure, RAY_MASK_SHADOW );
            visibility = intersection.type == intersection_type::triangle ? 0.5f : 1.0f;
        }

        if( lightType == 0u )
        {
            directionalVisibleWeight += visibility * lightWeight;
            directionalTotalWeight += lightWeight;
        }
        else
        {
            localVisibleWeight += visibility * lightWeight;
            localTotalWeight += lightWeight;
        }
    }

    float directionalShadowFactor = directionalTotalWeight > 0.0f ?
        directionalVisibleWeight / directionalTotalWeight : 1.0f;
    float localShadowFactor = localTotalWeight > 0.0f ? localVisibleWeight / localTotalWeight : 1.0f;
    // shadowTexture is PFG_RG16_FLOAT: R = selected directional, G = merged point/spot.
    shadowTexture.write( float4( directionalShadowFactor, localShadowFactor, 0.0f, 1.0f ), gl_GlobalInvocationID.xy );
}
