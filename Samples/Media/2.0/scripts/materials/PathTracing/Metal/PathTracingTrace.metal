#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;
using namespace raytracing;

#define GEOMETRY_MASK_TRIANGLE 1
#define GEOMETRY_MASK_SPHERE   2
#define GEOMETRY_MASK_LIGHT    4
#define GEOMETRY_MASK_GEOMETRY (GEOMETRY_MASK_TRIANGLE | GEOMETRY_MASK_SPHERE)
#define RAY_MASK_PRIMARY       (GEOMETRY_MASK_GEOMETRY | GEOMETRY_MASK_LIGHT)
#define RAY_MASK_SECONDARY     GEOMETRY_MASK_GEOMETRY
#define RAY_MASK_SHADOW        GEOMETRY_MASK_GEOMETRY

struct PathTracerFrame
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
    float4 skyZenith;
    float4 skyHorizon;
    float2 projectionParams;
    float width;
    float height;
    uint sampleIndex;
    uint maxBounces;
    uint numLights;
    uint flags;
};

struct PathTracerLight
{
    float4 position;
    float4 diffuse;
    float4 specular;
    float4 attenuation;
    float4 spotDirection;
    float4 spotParams;
};

static float origin() { return 1.0f / 32.0f; }
static float float_scale() { return 1.0f / 65536.0f; }
static float int_scale() { return 256.0f; }

static float3 offset_ray( const float3 p, const float3 n )
{
    int3 of_i( int_scale() * n.x, int_scale() * n.y, int_scale() * n.z );
    float3 p_i(
        as_type<float>( as_type<int>( p.x ) + ( ( p.x < 0.0f ) ? -of_i.x : of_i.x ) ),
        as_type<float>( as_type<int>( p.y ) + ( ( p.y < 0.0f ) ? -of_i.y : of_i.y ) ),
        as_type<float>( as_type<int>( p.z ) + ( ( p.z < 0.0f ) ? -of_i.z : of_i.z ) ) );

    return float3( fabs( p.x ) < origin() ? p.x + float_scale() * n.x : p_i.x,
                   fabs( p.y ) < origin() ? p.y + float_scale() * n.y : p_i.y,
                   fabs( p.z ) < origin() ? p.z + float_scale() * n.z : p_i.z );
}

static uint wang_hash( uint seed )
{
    seed = ( seed ^ 61u ) ^ ( seed >> 16u );
    seed *= 9u;
    seed = seed ^ ( seed >> 4u );
    seed *= 0x27d4eb2du;
    seed = seed ^ ( seed >> 15u );
    return seed;
}

static float rand01( thread uint &seed )
{
    seed = wang_hash( seed );
    return (float)( seed & 0x00ffffffu ) / 16777216.0f;
}

static float3 cosine_sample_hemisphere( float2 xi )
{
    const float r = sqrt( xi.x );
    const float phi = 6.28318530718f * xi.y;
    return float3( r * cos( phi ), r * sin( phi ), sqrt( max( 0.0f, 1.0f - xi.x ) ) );
}

static float3 tangent_to_world( float3 v, float3 n )
{
    const float3 up = fabs( n.z ) < 0.999f ? float3( 0.0f, 0.0f, 1.0f ) : float3( 1.0f, 0.0f, 0.0f );
    const float3 tangent = normalize( cross( up, n ) );
    const float3 bitangent = cross( n, tangent );
    return normalize( tangent * v.x + bitangent * v.y + n * v.z );
}

static float3 sample_sky( float3 direction, constant PathTracerFrame &frame )
{
    const float t = saturate( direction.y * 0.5f + 0.5f );
    return mix( frame.skyHorizon.xyz, frame.skyZenith.xyz, t );
}

static float evaluate_light_visibility( float3 surfacePosition,
                                        float3 surfaceNormal,
                                        float3 lightDirection,
                                        float maxDistance,
                                        instance_acceleration_structure accelerationStructure )
{
    ray shadowRay;
    shadowRay.origin = offset_ray( surfacePosition, dot( surfaceNormal, lightDirection ) < 0.0f ? -surfaceNormal : surfaceNormal );
    shadowRay.direction = lightDirection;
    shadowRay.min_distance = 0.005f;
    shadowRay.max_distance = maxDistance;

    intersector<triangle_data, instancing> shadowIntersector;
    shadowIntersector.accept_any_intersection( true );
    typename intersector<triangle_data, instancing>::result_type shadowHit =
        shadowIntersector.intersect( shadowRay, accelerationStructure, RAY_MASK_SHADOW );

    return shadowHit.type == intersection_type::triangle ? 0.0f : 1.0f;
}

static float3 evaluate_direct_lighting( float3 surfacePosition,
                                        float3 surfaceNormal,
                                        constant PathTracerLight *lights,
                                        uint numLights,
                                        instance_acceleration_structure accelerationStructure )
{
    float3 result = float3( 0.0f );
    const uint maxSupportedLights = 16u;
    numLights = min( numLights, maxSupportedLights );

    for( uint lightIdx = 0u; lightIdx < numLights; ++lightIdx )
    {
        constant PathTracerLight &light = lights[lightIdx];
        const uint lightType = (uint)( light.spotParams.w + 0.5f );

        float3 lightDirection = float3( 0.0f, 1.0f, 0.0f );
        float maxDistance = INFINITY;
        float attenuation = 1.0f;

        if( lightType == 0u )
        {
            lightDirection = normalize( light.position.xyz );
        }
        else if( lightType == 1u || lightType == 2u )
        {
            const float3 toLight = light.position.xyz - surfacePosition;
            const float lightDistance = length( toLight );
            if( lightDistance <= 0.005f || lightDistance > light.attenuation.x )
                continue;

            lightDirection = toLight / lightDistance;
            maxDistance = max( lightDistance - 0.005f, 0.0f );
            attenuation = 1.0f / ( 0.5f + ( light.attenuation.y + light.attenuation.z * lightDistance ) * lightDistance );

            if( lightType == 2u )
            {
                const float spotCosAngle = dot( -lightDirection, normalize( light.spotDirection.xyz ) );
                if( spotCosAngle < light.spotParams.y )
                    continue;

                const float spotAtten = saturate( ( spotCosAngle - light.spotParams.y ) * light.spotParams.x );
                attenuation *= pow( spotAtten, light.spotParams.z );
            }
        }
        else
        {
            continue;
        }

        const float nDotL = saturate( dot( surfaceNormal, lightDirection ) );
        if( nDotL <= 0.0f )
            continue;

        const float visibility = evaluate_light_visibility( surfacePosition, surfaceNormal, lightDirection,
                                                            maxDistance, accelerationStructure );
        result += light.diffuse.xyz * attenuation * nDotL * visibility;
    }

    return result;
}

kernel void main_metal
(
    texture2d<float, access::read_write> accumulationTexture [[texture(UAV_SLOT_START)]],
    texture2d<float, access::write> radianceTexture [[texture(UAV_SLOT_START + 1)]],

    constant PathTracerFrame *frame [[buffer(0)]],
    constant PathTracerLight *lights [[buffer(1)]],

    instance_acceleration_structure accelerationStructure,
    intersection_function_table<triangle_data, instancing> intersectionFunctionTable,

    ushort3 gl_GlobalInvocationID [[thread_position_in_grid]]
)
{
    if( gl_GlobalInvocationID.x >= frame->width || gl_GlobalInvocationID.y >= frame->height )
        return;

    const uint2 pixelPos = uint2( gl_GlobalInvocationID.xy );
    uint seed = wang_hash( pixelPos.x + pixelPos.y * 1664525u + frame->sampleIndex * 1013904223u );
    const float2 jitter = float2( rand01( seed ), rand01( seed ) );
    const float2 uv = ( float2( pixelPos ) + jitter ) / float2( frame->width, frame->height );

    float3 rayDirection = mix( mix( frame->cameraCorner0.xyz, frame->cameraCorner2.xyz, uv.x ),
                               mix( frame->cameraCorner1.xyz, frame->cameraCorner3.xyz, uv.x ),
                               uv.y );
    rayDirection = normalize( rayDirection );

    ray pathRay;
    pathRay.origin = frame->cameraPos.xyz;
    pathRay.direction = rayDirection;
    pathRay.min_distance = 0.005f;
    pathRay.max_distance = INFINITY;

    intersector<triangle_data, instancing> pathIntersector;
    float3 throughput = float3( 1.0f );
    float3 radiance = float3( 0.0f );
    const uint bounceCount = max( frame->maxBounces, 1u );

    for( uint bounce = 0u; bounce < bounceCount; ++bounce )
    {
        typename intersector<triangle_data, instancing>::result_type hit =
            pathIntersector.intersect( pathRay, accelerationStructure, RAY_MASK_PRIMARY );

        if( hit.type != intersection_type::triangle )
        {
            radiance += throughput * sample_sky( pathRay.direction, *frame );
            break;
        }

        const float3 geometricNormal = -pathRay.direction;
        const float3 hitPosition = pathRay.origin + pathRay.direction * hit.distance;

        const float3 baseColor = float3( 0.8f );
        radiance += throughput * baseColor * evaluate_direct_lighting( hitPosition, geometricNormal,
                                                                       lights, frame->numLights,
                                                                       accelerationStructure );

        const float3 localDirection = cosine_sample_hemisphere( float2( rand01( seed ), rand01( seed ) ) );
        const float3 nextDirection = tangent_to_world( localDirection, geometricNormal );

        throughput *= baseColor;
        pathRay.origin = offset_ray( hitPosition, geometricNormal );
        pathRay.direction = nextDirection;
        pathRay.min_distance = 0.005f;
        pathRay.max_distance = INFINITY;

        if( bounce >= 2u )
        {
            const float continueProbability = clamp( max( throughput.x, max( throughput.y, throughput.z ) ), 0.05f, 0.95f );
            if( rand01( seed ) > continueProbability )
                break;
            throughput /= continueProbability;
        }
    }

    const bool resetAccumulation = frame->sampleIndex == 0u;
    const float4 previous = resetAccumulation ? float4( 0.0f ) : accumulationTexture.read( pixelPos );
    const float4 accumulated = previous + float4( radiance, 1.0f );
    accumulationTexture.write( accumulated, pixelPos );

    const float invSampleCount = 1.0f / max( accumulated.w, 1.0f );
    radianceTexture.write( float4( accumulated.xyz * invSampleCount, 1.0f ), pixelPos );
}
