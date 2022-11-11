#include <metal_stdlib>

using namespace metal;
using namespace raytracing;

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
    texture2d<@insertpiece(texture0_pf_type), access::read> depthBuffer [[texture(0)]],
	texture2d<float, access::write> shadowTexture [[texture(UAV_SLOT_START)]], // Destination
 
 
    constant float2 &projectionParams    [[buffer(PARAMETER_SLOT)]], // TODO PARAMTER_SLOT should be const buffer??
    device Light *lights, // TODO replace with correct light source.
 
    instance_acceleration_structure accelerationStructure,
    intersection_function_table<triangle_data, instancing> intersectionFunctionTable,
    

	ushort3 gl_LocalInvocationID	[[thread_position_in_threadgroup]],
	ushort3 gl_GlobalInvocationID	[[thread_position_in_grid]]
)
{
    // The sample aligns the thread count to the threadgroup size. which means the thread count
    // may be different than the bounds of the texture. Test to make sure this thread
    // is referencing a pixel within the bounds of the texture.
    if (tid.x < uniforms.width && tid.y < uniforms.height)
    {
        float fDepth = depthTexture.sample( samplerState, inPs.uv0 ).x;
        
        float linearDepth = projectionParams.y / (fDepth - projectionParams.x);
        
        float3 viewSpacePosition = inPs.cameraDir * linearDepth;
        
        //For this next line to work cameraPos would have to be an uniform in world space, and cameraDir
        //would have to be sent using the compositor setting "quad_normals camera_far_corners_world_space_centered"
        float3 worldSpacePosition = inPs.cameraDir * linearDepth + cameraPos;
        
        
        // The ray to cast.
        ray shadowRay;
        
        // Pixel coordinates for this thread.
        float2 pixel = (float2)tid;
        
        // Apply a random offset to the random number index to decorrelate pixels.
        unsigned int offset = randomTex.read(tid).x;
        
        // Add a random offset to the pixel coordinates for antialiasing.
        float2 r = float2(halton(offset + uniforms.frameIndex, 0),
                          halton(offset + uniforms.frameIndex, 1));
        
        pixel += r;
        
        // Map pixel coordinates to -1..1.
        float2 uv = (float2)pixel / float2(uniforms.width, uniforms.height);
        uv = uv * 2.0f - 1.0f;
        
        // Create an intersector to test for intersection between the ray and the geometry in the scene.
        intersector<triangle_data, instancing> i;
        
        // Shadow rays check only whether there is an object between the intersection point
        // and the light source. Tell Metal to return after finding any intersection.
        i.accept_any_intersection(true);
        
        // Rays start at the camera position.
        shadowRay.origin = worldSpacePosition;
        
        for( int lightIndex = 0; lightIndex < lightCount; ++i )
        {
            
            // Map normalized pixel coordinates into camera's coordinate system.
            shadowRay.direction = normalize( lights[lightIndex] - worldSpacePosition );
            
            // Don't limit intersection distance.
            shadowRay.max_distance = INFINITY;
            
            intersection = i.intersect( shadowRay, accelerationStructure, RAY_MASK_SHADOW );
            
            if( intersection.type == intersection_type::triangle )
            {
                // TODO black out the pixel for testing.
            }
        }
    }
//	testTexture.write( float4( float2(gl_LocalInvocationID.xy) / 16.0f, 0.0f, 1.0f ),
//					   gl_GlobalInvocationID.xy );
}
