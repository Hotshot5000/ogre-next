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
    
    float4 position;    //.w contains the objLightMask
    float4 diffuse;        //.w contains numNonCasterDirectionalLights
    float3 specular;

    float3 attenuation;
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
    if (gl_GlobalInvocationID.x > in->width && gl_GlobalInvocationID.y > in->height)
        return;

    ushort3 pixelPos = ( gl_GlobalInvocationID /* * gl_WorkGroupID*/ );// + gl_LocalInvocationID;
    float2 pixel = (float2)gl_GlobalInvocationID.xy;
    float fDepth = depthTexture.read( pixelPos.xy );
    float linearDepth = in->projectionParams.y / ( fDepth - in->projectionParams.x );
    
    ray shadowRay;
    

    float2 uv = float2( pixel.x / in->width, pixel.y / in->height );
//        uv.x = uv.x * 2.0f - 1.0f;
//        uv.y = ( 1.0f - uv.y ) * 2.0f - 1.0f;

    shadowRay.direction = normalize( float3( lights[0].position.xyz ) );

    // uv must be between 0.0 and 1.0 not -1.0 and 1.0!!!
    float3 interp = mix( mix( in->cameraCorner0.xyz, in->cameraCorner2.xyz, uv.x ),
                         mix( in->cameraCorner1.xyz, in->cameraCorner3.xyz, uv.x ),
                         uv.y );

    float3 worldSpacePosition = in->cameraPos.xyz + interp * linearDepth;
    
    // Create an intersector to test for intersection between the ray and the geometry in the scene.
    intersector<triangle_data, instancing> i;
    
    // Shadow rays check only whether there is an object between the intersection point
    // and the light source. Tell Metal to return after finding any intersection.
    i.accept_any_intersection( true );
    
    typename intersector<triangle_data, instancing>::result_type intersection;
    
    shadowRay.origin = worldSpacePosition.xyz;
        
    // Don't limit intersection distance.
    shadowRay.max_distance = INFINITY;
    shadowRay.min_distance = 0.005f;
    
    intersection = i.intersect( shadowRay, accelerationStructure, RAY_MASK_SHADOW );
    
    float4 col;
            
    if( intersection.type == intersection_type::triangle )
    {
        col = float4( 0.5f, 0.5f, 0.5f, 1.0f );
    }
    else
    {
        col = float4( 1.0f, 1.0f, 1.0f, 1.0f );
    }
    shadowTexture.write( col, gl_GlobalInvocationID.xy );
}
