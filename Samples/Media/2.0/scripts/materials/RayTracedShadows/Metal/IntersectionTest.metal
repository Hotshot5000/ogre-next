#include <metal_stdlib>

#define GEOMETRY_MASK_TRIANGLE 1
#define GEOMETRY_MASK_SPHERE   2
#define GEOMETRY_MASK_LIGHT    4

#define GEOMETRY_MASK_GEOMETRY (GEOMETRY_MASK_TRIANGLE | GEOMETRY_MASK_SPHERE)

#define RAY_MASK_PRIMARY   (GEOMETRY_MASK_GEOMETRY | GEOMETRY_MASK_LIGHT)
#define RAY_MASK_SHADOW    GEOMETRY_MASK_GEOMETRY
#define RAY_MASK_SECONDARY GEOMETRY_MASK_GEOMETRY

using namespace metal;
using namespace raytracing;

struct PS_INPUT
{
    //float2 uv0;
    float3 cameraDir;
    float3 cameraPos;
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

kernel void main_metal
(
    depth2d<@insertpiece(texture0_pf_type), access::read> depthTexture [[texture(0)]],
    //sampler  samplerState    [[sampler(0)]],
	texture2d<float, access::write> shadowTexture [[texture(UAV_SLOT_START)]], // Destination
 
 
    constant float2 &projectionParams    [[buffer(PARAMETER_SLOT)]], // TODO PARAMTER_SLOT should be const buffer??
    
    device Light *lights, // TODO replace with correct light source.
    device PS_INPUT *inPs,
 
    instance_acceleration_structure accelerationStructure,
    intersection_function_table<triangle_data, instancing> intersectionFunctionTable,
    

	ushort3 gl_LocalInvocationID	    [[thread_position_in_threadgroup]],
	ushort3 gl_GlobalInvocationID	    [[thread_position_in_grid]],
    ushort3 gl_GlobalThreadCount        [[threads_per_threadgroup]]
)
{
    // The sample aligns the thread count to the threadgroup size. which means the thread count
    // may be different than the bounds of the texture. Test to make sure this thread
    // is referencing a pixel within the bounds of the texture.
    //if (gl_GlobalInvocationID.x < uniforms.width && gl_GlobalInvocationID.y < uniforms.height)
    //{
        ushort3 pixelPos = ( gl_GlobalInvocationID * gl_GlobalThreadCount ) + gl_LocalInvocationID;
        float fDepth = depthTexture.read( pixelPos.xy );
        
        float linearDepth = projectionParams.y / (fDepth - projectionParams.x);
        
        float3 viewSpacePosition = inPs->cameraDir * linearDepth;
        
        //For this next line to work cameraPos would have to be an uniform in world space, and cameraDir
        //would have to be sent using the compositor setting "quad_normals camera_far_corners_world_space_centered"
        float3 worldSpacePosition = inPs->cameraDir * linearDepth + inPs->cameraPos;
        
        
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
        
        // Map pixel coordinates to -1..1.
        float2 uv = (float2)pixel / float2(inPs->width, inPs->height);
        uv = uv * 2.0f - 1.0f;
        
        // Create an intersector to test for intersection between the ray and the geometry in the scene.
        intersector<triangle_data, instancing> i;
        
        // Shadow rays check only whether there is an object between the intersection point
        // and the light source. Tell Metal to return after finding any intersection.
        i.accept_any_intersection(true);
    
        typename intersector<triangle_data, instancing>::result_type intersection;
        
        // Rays start at the camera position.
        shadowRay.origin = worldSpacePosition;
        
        //for( int lightIndex = 0; lightIndex < /*lightCount*/1; ++i )
        //{
            
            // Map normalized pixel coordinates into camera's coordinate system.
            shadowRay.direction = normalize( lights[0].position.xyz - worldSpacePosition );
            
            // Don't limit intersection distance.
            shadowRay.max_distance = INFINITY;
            
            intersection = i.intersect( shadowRay, accelerationStructure, RAY_MASK_SHADOW );
            
            if( intersection.type == intersection_type::triangle )
            {
                // TODO black out the pixel for testing.
            }
        //}
    //}
//	testTexture.write( float4( float2(gl_LocalInvocationID.xy) / 16.0f, 0.0f, 1.0f ),
//					   gl_GlobalInvocationID.xy );
}
