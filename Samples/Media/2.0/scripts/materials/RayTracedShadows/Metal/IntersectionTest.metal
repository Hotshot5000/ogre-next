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
    float(int(p.x)+((p.x < 0) ? -of_i.x : of_i.x)),
               float(int(p.y)+((p.y < 0) ? -of_i.y : of_i.y)),
               float(int(p.z)+((p.z < 0) ? -of_i.z : of_i.z)));
    
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
    //if (gl_GlobalInvocationID.x < uniforms.width && gl_GlobalInvocationID.y < uniforms.height)
    //{
//        ushort3 pixelPos = ( gl_GlobalInvocationID /* * gl_WorkGroupID*/ ) + gl_LocalInvocationID;
        ushort3 pixelPos = gl_GlobalInvocationID;
        float fDepth = depthTexture.read( pixelPos.xy );
        float3 fNormal = normalize( normalsTexture.read( pixelPos.xy ).xyz * 2.0 - 1.0 );
        fNormal.z = -fNormal.z; //Normal should be left handed.
        float linearDepth = in->projectionParams.y / (fDepth - in->projectionParams.x);
        
//        float3 viewSpacePosition = in->cameraFront.xyz * linearDepth;
//        viewSpacePosition /= 10.0f;
//        shadowTexture.write( float4( viewSpacePosition, 1.0f ), gl_GlobalInvocationID.xy );
        
        //For this next line to work cameraPos would have to be an uniform in world space, and cameraDir
        //would have to be sent using the compositor setting "quad_normals camera_far_corners_world_space_centered"
//        float3 worldSpacePosition = in->cameraDir * linearDepth + in->cameraPos;
    
//        shadowTexture.write( float4( pixelPos.x * 0.001f, pixelPos.y * 0.001f, pixelPos.z * 0.001f, 1.0f ), pixelPos.xy );
//        shadowTexture.write( float4( 1.0f, 1.0f, 1.0f, 1.0f ), pixelPos.xy );
//        shadowTexture.write( float4( worldSpacePosition.xyz, 1.0f ), pixelPos.xy );
        
        
        // The ray to cast.
        ray shadowRay;
        
        // Pixel coordinates for this thread.
        float2 pixel = (float2)gl_GlobalInvocationID.xy;
        
        // Apply a random offset to the random number index to decorrelate pixels.
//        unsigned int offset = randomTex.read(gl_GlobalInvocationID).x;
        
        // Add a random offset to the pixel coordinates for antialiasing.
//        float2 r = float2(halton(offset + uniforms.frameIndex, 0),
//                          halton(offset + uniforms.frameIndex, 1));
        
//        pixel += r;
    
//        float4x4 invProjectionMat = float4x4( float4( 0.552284837, 0, -0, 0 ),
//                                              float4( 0, 0.414213568, 0, -0 ),
//                                              float4( -0, 0, -0, -1 ),
//                                              float4( 0, -0, -2.49950004, 2.50049996 ) );
//
//        float4x4 invViewMat =       float4x4( float4( 1, -0, 0, -0 ),
//                                              float4( -0, 0.948683381, 0.316227794, 5.00000048 ),
//                                              float4( 0, -0.316227794, 0.948683381, 15.000001 ),
//                                              float4( 0, 0, -0, 1 ) );
        
        // Map pixel coordinates to -1..1.
        float2 uv = float2( pixel.x / in->width, pixel.y / in->height );
//        uv.x = uv.x * 2.0f - 1.0f;
//        uv.y = ( 1.0f - uv.y ) * 2.0f - 1.0f;
    
//        // far
//        mWorldSpaceCorners[4] = eyeToWorld.transformAffine( Vector3( farRight, farTop, -farDist ) );
//        mWorldSpaceCorners[5] = eyeToWorld.transformAffine( Vector3( farLeft, farTop, -farDist ) );
//        mWorldSpaceCorners[6] = eyeToWorld.transformAffine( Vector3( farLeft, farBottom, -farDist ) );
//        mWorldSpaceCorners[7] = eyeToWorld.transformAffine( Vector3( farRight, farBottom, -farDist ) );
    
//        Vector3 cameraDirs[4];
//        cameraDirs[0] = corners[5] - cameraPos;
//        cameraDirs[1] = corners[6] - cameraPos;
//        cameraDirs[2] = corners[4] - cameraPos;
//        cameraDirs[3] = corners[7] - cameraPos;
    
        float3 interp = mix( mix( in->cameraCorner0.xyz, in->cameraCorner2.xyz, uv.x ),
                             mix( in->cameraCorner1.xyz, in->cameraCorner3.xyz, uv.x),
                             uv.y );
    
    
        float4 viewPosH      = in->invProjectionMat * float4( uv.x, uv.y, linearDepth, 1.0 );
//        shadowTexture.write( viewPosH, gl_GlobalInvocationID.xy );
        float3 viewPos       = viewPosH.xyz / viewPosH.w;
//        shadowTexture.write( float4( viewPos, 1.0f ), gl_GlobalInvocationID.xy );
    
        float3 worldSpacePosition = in->cameraPos.xyz + interp * linearDepth;//viewPos;//( in->invViewMat * float4( viewPos.xyz, 1.0f ) );// + in->cameraPos.xyz;
    
//        worldSpacePosition.xy += 0.5f;
//        worldSpacePosition.z += 1.0f;
//        worldSpacePosition.x = ( worldSpacePosition.x + 1.0f ) * 0.5f;
//        worldSpacePosition.y = ( worldSpacePosition.y + 1.0f ) * 0.5f;
//        worldSpacePosition.z = ( worldSpacePosition.z + 1.0f ) * 0.5f;
    
//        worldSpacePosition = in->invViewMat * worldSpacePosition;
//        worldSpacePosition.xyz /= worldSpacePosition.w;
//        worldSpacePosition.xyz = ( worldSpacePosition.xyz + 1.0f ) * 0.5f;
    
//        worldSpacePosition.xyz /= 10.0f;
//        shadowTexture.write( float4( worldSpacePosition.x, worldSpacePosition.y, worldSpacePosition.z, 1.0f ), gl_GlobalInvocationID.xy );
    
//        shadowTexture.write( float4( (viewPos.xy + float2(1.0f, 1.0f)) * 0.5f, viewPos.z, 1.0f ), pixelPos.xy );
//        shadowTexture.write( float4( uv.xy, 1.0f ), pixelPos.xy );
        
        // Create an intersector to test for intersection between the ray and the geometry in the scene.
        intersector<triangle_data, instancing> i;
        
        // Shadow rays check only whether there is an object between the intersection point
        // and the light source. Tell Metal to return after finding any intersection.
        i.accept_any_intersection( true );
    
        i.assume_geometry_type(geometry_type::triangle);
        i.force_opacity(forced_opacity::opaque);
    
        typename intersector<triangle_data, instancing>::result_type intersection;
        
        // Rays start at the camera position.
        shadowRay.origin = offset_ray(worldSpacePosition.xyz, fNormal);//worldSpacePosition.xyz;//in->cameraPos.xyz;//float3( pixel.x - pixel.x * 0.5f, 2.0f, pixel.y - pixel.y * 0.5f );
        
        //for( int lightIndex = 0; lightIndex < /*lightCount*/1; ++i )
        //{
            
            // Map normalized pixel coordinates into camera's coordinate system.
            shadowRay.direction = normalize( lights[0].position.xyz /*- worldSpacePosition*/ );//normalize( float3( 1.0f, 1.0f, 1.0f ) );
    
//            uv.y = -uv.y;
//            shadowRay.direction = ( normalize(uv.x * in->cameraRight +
//                                            uv.y * in->cameraUp +
//                                            in->cameraFront) ).xyz;
    
            //shadowTexture.write( float4( worldSpacePosition.xy, -worldSpacePosition.z, 1.0f ), gl_GlobalInvocationID.xy );
            
            // Don't limit intersection distance.
            shadowRay.max_distance = INFINITY;
            shadowRay.min_distance = 0.001f;
            
            intersection = i.intersect( shadowRay, accelerationStructure, RAY_MASK_SHADOW );
            
            if( intersection.type == intersection_type::triangle )
            {
                // Compute intersection point in world space.
                float3 worldSpaceIntersectionPoint = shadowRay.origin + shadowRay.direction * intersection.distance;
                float4 ndc = in->invViewMat * float4( worldSpaceIntersectionPoint, 1.0f );
                ndc.xyz /= ndc.w;
                ndc.y = -ndc.y;
                float2 texCoords = ( ndc.xy * 0.5f ) + 1.0f;
                ushort2 texPos;
                texPos.x = (ushort) (texCoords.x * in->width);
                texPos.y = (ushort) (texCoords.y * in->height);
                float4 finalCol = float4( 0.0f, 0.0f, intersection.distance, 1.0f );
                if( texPos.x < 0 || texPos.x > in->width)
                    finalCol += float4( 0.0f, 1.0f, 0.0f, 0.0f );
                if( texPos.y < 0 || texPos.y > in->height)
                    finalCol += float4( 1.0f, 1.0f, 0.0f, 0.0f );
                texPos.x = clamp( texPos.x, (ushort) 0, (ushort) in->width );
                texPos.y = clamp( texPos.y, (ushort) 0, (ushort) in->height );
                shadowTexture.write( float4( 1.0f, 1.0f, 1.0f, 1.0f ), gl_GlobalInvocationID.xy );
//                shadowTexture.write( finalCol, ushort2( 0, 0) );
            }
            else
            {
                shadowTexture.write( float4( 1.0f, 0.0f, 0.0f, 1.0f ), gl_GlobalInvocationID.xy );
            }
        //}
    //}
//	testTexture.write( float4( float2(gl_LocalInvocationID.xy) / 16.0f, 0.0f, 1.0f ),
//					   gl_GlobalInvocationID.xy );
}
