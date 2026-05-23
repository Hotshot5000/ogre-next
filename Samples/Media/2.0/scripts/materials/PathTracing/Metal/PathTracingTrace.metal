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

struct PathTracerMaterial
{
    float4 baseColour_roughness;
    float4 fresnel_transparency;
    float4 emissive_flags;
    float4 diffuseTextureIdx_slice_hasTexture;
    float4 diffuseUvOffsetScale;
    float4 roughnessTextureIdx_slice_hasTexture;
    float4 normalTextureIdx_slice_hasTexture;
    float4 emissiveTextureIdx_slice_hasTexture;
    float4 reflectionTextureIdx_slice_hasTexture;
};

struct PathTracerGeometry
{
    float4 material_subMesh;
};

struct PathTracerTriangle
{
    float4 uv0_uv1;
    float4 uv2_normalX_normalY;
    float4 normalZ_flags;
    float4 tangent;
    float4 bitangent;
};

struct SurfaceMaterial
{
    float3 baseColour;
    float roughness;
    float3 fresnel;
    float transparency;
    float3 emissive;
    uint flags;
    int diffuseTextureIdx;
    uint diffuseTextureSlice;
    float4 diffuseUvOffsetScale;
    bool hasDiffuseTexture;
    bool manualSrgbDecode;
    int roughnessTextureIdx;
    uint roughnessTextureSlice;
    bool hasRoughnessTexture;
    int normalTextureIdx;
    uint normalTextureSlice;
    float normalMapWeight;
    bool hasNormalTexture;
    int emissiveTextureIdx;
    uint emissiveTextureSlice;
    bool hasEmissiveTexture;
    int reflectionTextureIdx;
    bool hasReflectionTexture;
    float specularWeight;
};

static SurfaceMaterial load_surface_material( uint instanceId,
                                              device const PathTracerMaterial *materials,
                                              device const PathTracerGeometry *geometryRecords )
{
    const PathTracerGeometry geometry = geometryRecords[instanceId];
    const uint materialIdx = (uint)( geometry.material_subMesh.x + 0.5f );
    const PathTracerMaterial material = materials[materialIdx];

    SurfaceMaterial surface;
    surface.baseColour = saturate( material.baseColour_roughness.xyz );
    surface.roughness = clamp( material.baseColour_roughness.w, 0.02f, 1.0f );
    const float3 fresnelInput = max( material.fresnel_transparency.xyz, float3( 0.0f ) );
    const float3 iorF0 = pow( ( fresnelInput - 1.0f ) / max( fresnelInput + 1.0f, float3( 1e-4f ) ),
                              float3( 2.0f ) );
    surface.fresnel = select( saturate( fresnelInput ), saturate( iorF0 ), fresnelInput > float3( 1.0f ) );
    surface.flags = (uint)( material.emissive_flags.w + 0.5f );
    const uint transparencyMode = surface.flags & 15u;
    surface.transparency = transparencyMode == 0u ? 1.0f : saturate( material.fresnel_transparency.w );
    surface.emissive = max( material.emissive_flags.xyz, float3( 0.0f ) );
    surface.diffuseTextureIdx = (int)( material.diffuseTextureIdx_slice_hasTexture.x + 0.5f );
    surface.diffuseTextureSlice = (uint)( material.diffuseTextureIdx_slice_hasTexture.y + 0.5f );
    surface.hasDiffuseTexture = material.diffuseTextureIdx_slice_hasTexture.z > 0.5f;
    surface.manualSrgbDecode = material.diffuseTextureIdx_slice_hasTexture.w > 0.5f;
    surface.diffuseUvOffsetScale = material.diffuseUvOffsetScale;
    surface.roughnessTextureIdx = (int)( material.roughnessTextureIdx_slice_hasTexture.x + 0.5f );
    surface.roughnessTextureSlice = (uint)( material.roughnessTextureIdx_slice_hasTexture.y + 0.5f );
    surface.hasRoughnessTexture = material.roughnessTextureIdx_slice_hasTexture.z > 0.5f;
    surface.normalTextureIdx = (int)( material.normalTextureIdx_slice_hasTexture.x + 0.5f );
    surface.normalTextureSlice = (uint)( material.normalTextureIdx_slice_hasTexture.y + 0.5f );
    surface.hasNormalTexture = material.normalTextureIdx_slice_hasTexture.z > 0.5f;
    surface.normalMapWeight = saturate( material.normalTextureIdx_slice_hasTexture.w );
    surface.emissiveTextureIdx = (int)( material.emissiveTextureIdx_slice_hasTexture.x + 0.5f );
    surface.emissiveTextureSlice = (uint)( material.emissiveTextureIdx_slice_hasTexture.y + 0.5f );
    surface.hasEmissiveTexture = material.emissiveTextureIdx_slice_hasTexture.z > 0.5f;
    surface.reflectionTextureIdx = (int)( material.reflectionTextureIdx_slice_hasTexture.x + 0.5f );
    surface.hasReflectionTexture = material.reflectionTextureIdx_slice_hasTexture.z > 0.5f;
    surface.specularWeight = material.reflectionTextureIdx_slice_hasTexture.w > 0.0f ?
        saturate( material.reflectionTextureIdx_slice_hasTexture.w ) : 1.0f;
    return surface;
}

static PathTracerTriangle load_triangle( uint instanceId,
                                         uint primitiveId,
                                         device const PathTracerGeometry *geometryRecords,
                                         device const PathTracerTriangle *triangleRecords )
{
    const PathTracerGeometry geometry = geometryRecords[instanceId];
    const uint triangleStart = (uint)( geometry.material_subMesh.w + 0.5f );
    return triangleRecords[triangleStart + primitiveId];
}

static float2 interpolate_uv( const PathTracerTriangle triangle, float2 barycentric )
{
    const float w = 1.0f - barycentric.x - barycentric.y;
    const float2 uv0 = triangle.uv0_uv1.xy;
    const float2 uv1 = triangle.uv0_uv1.zw;
    const float2 uv2 = triangle.uv2_normalX_normalY.xy;
    return uv0 * w + uv1 * barycentric.x + uv2 * barycentric.y;
}

static float3 load_triangle_normal( const PathTracerTriangle triangle, float3 fallbackNormal )
{
    const float3 normal = normalize( float3( triangle.uv2_normalX_normalY.zw,
                                             triangle.normalZ_flags.x ) );
    return all( isfinite( normal ) ) ? normal : fallbackNormal;
}

static float3 srgb_to_linear( float3 color )
{
    return select( color / 12.92f,
                   pow( ( color + 0.055f ) / 1.055f, float3( 2.4f ) ),
                   color > float3( 0.04045f ) );
}

static float2 material_uv( const SurfaceMaterial material,
                           const PathTracerTriangle triangle,
                           float2 barycentric )
{
    return interpolate_uv( triangle, barycentric ) * material.diffuseUvOffsetScale.zw +
           material.diffuseUvOffsetScale.xy;
}

static float3 sample_diffuse_texture( const SurfaceMaterial material,
                                      const PathTracerTriangle triangle,
                                      float2 barycentric,
                                      array<texture2d_array<float>, 8> diffuseTextures,
                                      sampler diffuseSampler )
{
    if( !material.hasDiffuseTexture || material.diffuseTextureIdx < 0 || material.diffuseTextureIdx >= 8 ||
        triangle.normalZ_flags.y < 0.5f )
    {
        return float3( 1.0f );
    }

    return diffuseTextures[material.diffuseTextureIdx].sample( diffuseSampler,
                                                               fract( material_uv( material, triangle,
                                                                                   barycentric ) ),
                                                               material.diffuseTextureSlice ).xyz;
}

static float sample_roughness_texture( const SurfaceMaterial material,
                                       const PathTracerTriangle triangle,
                                       float2 barycentric,
                                       array<texture2d_array<float>, 8> roughnessTextures,
                                       sampler roughnessSampler )
{
    if( !material.hasRoughnessTexture || material.roughnessTextureIdx < 0 ||
        material.roughnessTextureIdx >= 8 || triangle.normalZ_flags.y < 0.5f )
    {
        return 1.0f;
    }

    return roughnessTextures[material.roughnessTextureIdx].sample(
        roughnessSampler, fract( material_uv( material, triangle, barycentric ) ),
        material.roughnessTextureSlice ).x;
}

static float3 sample_emissive_texture( const SurfaceMaterial material,
                                       const PathTracerTriangle triangle,
                                       float2 barycentric,
                                       array<texture2d_array<float>, 8> emissiveTextures,
                                       sampler emissiveSampler )
{
    if( !material.hasEmissiveTexture || material.emissiveTextureIdx < 0 ||
        material.emissiveTextureIdx >= 8 || triangle.normalZ_flags.y < 0.5f )
    {
        return float3( 1.0f );
    }

    return emissiveTextures[material.emissiveTextureIdx].sample(
        emissiveSampler, fract( material_uv( material, triangle, barycentric ) ),
        material.emissiveTextureSlice ).xyz;
}

static float3 apply_normal_texture( const SurfaceMaterial material,
                                    const PathTracerTriangle triangle,
                                    float2 barycentric,
                                    float3 geometricNormal,
                                    array<texture2d_array<float>, 8> normalTextures,
                                    sampler normalSampler )
{
    if( !material.hasNormalTexture || material.normalTextureIdx < 0 || material.normalTextureIdx >= 8 ||
        triangle.normalZ_flags.y < 0.5f || material.normalMapWeight <= 0.0f )
    {
        return geometricNormal;
    }

    const float4 normalSample = normalTextures[material.normalTextureIdx].sample(
        normalSampler, fract( material_uv( material, triangle, barycentric ) ),
        material.normalTextureSlice );
    const float normalY = normalSample.w < 0.999f ? normalSample.w : normalSample.y;
    const float2 tangentSampleXY = float2( normalSample.x, normalY ) * 2.0f - 1.0f;
    if( dot( tangentSampleXY, tangentSampleXY ) > 0.98f )
        return geometricNormal;
    const float3 tangentSample = normalize(
        float3( tangentSampleXY, sqrt( max( 0.0f, 1.0f - dot( tangentSampleXY, tangentSampleXY ) ) ) ) );

    float3 tangent = normalize( triangle.tangent.xyz );
    if( !all( isfinite( tangent ) ) || dot( tangent, tangent ) < 1e-5f )
        return geometricNormal;
    tangent = normalize( tangent - geometricNormal * dot( tangent, geometricNormal ) );

    float3 bitangent = normalize( triangle.bitangent.xyz );
    if( !all( isfinite( bitangent ) ) || dot( bitangent, bitangent ) < 1e-5f )
        bitangent = normalize( cross( geometricNormal, tangent ) );
    bitangent = dot( bitangent, cross( geometricNormal, tangent ) ) < 0.0f ? -bitangent : bitangent;

    const float3 mappedNormal = normalize( tangent * tangentSample.x + bitangent * tangentSample.y +
                                           geometricNormal * tangentSample.z );
    if( !all( isfinite( mappedNormal ) ) || dot( mappedNormal, geometricNormal ) <= 0.0f )
        return geometricNormal;

    const float3 blendedNormal = normalize( mix( geometricNormal, mappedNormal,
                                                material.normalMapWeight ) );
    return dot( blendedNormal, geometricNormal ) > 0.0f ? blendedNormal : geometricNormal;
}

static float3 sample_reflection_texture( const SurfaceMaterial material,
                                         float3 direction,
                                         array<texturecube<float>, 4> reflectionTextures,
                                         sampler reflectionSampler )
{
    if( !material.hasReflectionTexture || material.reflectionTextureIdx < 0 ||
        material.reflectionTextureIdx >= 4 )
    {
        return float3( 0.0f );
    }

    return reflectionTextures[material.reflectionTextureIdx].sample( reflectionSampler,
                                                                    normalize( direction ) ).xyz;
}

static float fresnel_schlick_luminance( float3 f0, float cosTheta )
{
    const float3 f = f0 + ( 1.0f - f0 ) * pow( saturate( 1.0f - cosTheta ), 5.0f );
    return saturate( dot( f, float3( 0.2126f, 0.7152f, 0.0722f ) ) );
}

static bool is_transparent_surface( const SurfaceMaterial material )
{
    const uint transparencyMode = material.flags & 15u;
    return transparencyMode == 1u || transparencyMode == 3u || material.transparency < 0.995f;
}

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

struct DirectLighting
{
    float3 diffuse;
    float3 specular;
};

static float3 fresnel_schlick( float3 f0, float cosTheta )
{
    return f0 + ( 1.0f - f0 ) * pow( saturate( 1.0f - cosTheta ), 5.0f );
}

static float ggx_distribution( float nDotH, float roughness )
{
    const float a = max( roughness * roughness, 0.002f );
    const float a2 = a * a;
    const float denom = nDotH * nDotH * ( a2 - 1.0f ) + 1.0f;
    return a2 / max( 3.14159265359f * denom * denom, 1e-5f );
}

static float smith_ggx_visibility( float nDotV, float nDotL, float roughness )
{
    const float a = max( roughness * roughness, 0.002f );
    const float ggxV = nDotL * sqrt( nDotV * nDotV * ( 1.0f - a ) + a );
    const float ggxL = nDotV * sqrt( nDotL * nDotL * ( 1.0f - a ) + a );
    return 0.5f / max( ggxV + ggxL, 1e-5f );
}

static float evaluate_light_visibility( float3 surfacePosition,
                                        float3 surfaceNormal,
                                        float3 lightDirection,
                                        float maxDistance,
                                        uint currentInstanceId,
                                        instance_acceleration_structure accelerationStructure,
                                        device const PathTracerMaterial *materials,
                                        device const PathTracerGeometry *geometryRecords )
{
    ray shadowRay;
    shadowRay.origin = offset_ray( surfacePosition, dot( surfaceNormal, lightDirection ) < 0.0f ? -surfaceNormal : surfaceNormal );
    shadowRay.direction = lightDirection;
    shadowRay.min_distance = 0.005f;
    shadowRay.max_distance = maxDistance;

    intersector<triangle_data, instancing> shadowIntersector;
    float visibility = 1.0f;

    for( uint step = 0u; step < 8u; ++step )
    {
        typename intersector<triangle_data, instancing>::result_type shadowHit =
            shadowIntersector.intersect( shadowRay, accelerationStructure, RAY_MASK_SHADOW );

        if( shadowHit.type != intersection_type::triangle )
            return visibility;

        if( shadowHit.instance_id == currentInstanceId && shadowHit.distance <= 0.02f )
        {
            const float consumedDistance = shadowHit.distance + 0.02f;
            if( consumedDistance >= shadowRay.max_distance )
                return visibility;

            shadowRay.origin += shadowRay.direction * consumedDistance;
            shadowRay.max_distance -= consumedDistance;
            shadowRay.min_distance = 0.005f;
            continue;
        }

        const SurfaceMaterial blocker = load_surface_material( shadowHit.instance_id,
                                                               materials, geometryRecords );
        if( !is_transparent_surface( blocker ) )
            return 0.0f;

        const float transmission = saturate( 1.0f - blocker.transparency );
        if( transmission <= 0.02f )
            return 0.0f;

        visibility *= transmission;
        if( visibility <= 0.02f )
            return 0.0f;

        const float consumedDistance = shadowHit.distance + 0.01f;
        if( consumedDistance >= shadowRay.max_distance )
            return visibility;

        shadowRay.origin += shadowRay.direction * consumedDistance;
        shadowRay.max_distance -= consumedDistance;
        shadowRay.min_distance = 0.005f;
    }

    return visibility;
}

static DirectLighting evaluate_direct_lighting( float3 surfacePosition,
                                                float3 surfaceNormal,
                                                float3 shadowNormal,
                                                float3 viewDirection,
                                                float3 fresnelColor,
                                                float roughness,
                                                uint currentInstanceId,
                                                constant PathTracerLight *lights,
                                                uint numLights,
                                                instance_acceleration_structure accelerationStructure,
                                                device const PathTracerMaterial *materials,
                                                device const PathTracerGeometry *geometryRecords )
{
    DirectLighting result;
    result.diffuse = float3( 0.0f );
    result.specular = float3( 0.0f );
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

        const float visibility = evaluate_light_visibility( surfacePosition, shadowNormal, lightDirection,
                                                            maxDistance, currentInstanceId,
                                                            accelerationStructure, materials,
                                                            geometryRecords );
        const float geometricNDotL = saturate( dot( shadowNormal, lightDirection ) );
        if( geometricNDotL <= 0.0f )
            continue;

        // Avoid normal-map/tangent discontinuities completely removing a light on surfaces
        // that geometrically face it. Reflections and path bounces still use surfaceNormal.
        const float shadingNDotL = saturate( dot( surfaceNormal, lightDirection ) );
        const float nDotL = max( shadingNDotL, geometricNDotL * 0.5f );

        const float lightScale = attenuation * visibility;
        const float3 halfVector = normalize( lightDirection + viewDirection );
        const float geometricNDotV = saturate( dot( shadowNormal, viewDirection ) );
        const float nDotV = max( saturate( dot( surfaceNormal, viewDirection ) ),
                                 geometricNDotV * 0.5f );
        const float nDotH = saturate( dot( surfaceNormal, halfVector ) );
        const float vDotH = saturate( dot( viewDirection, halfVector ) );
        const float3 fresnel = fresnel_schlick( fresnelColor, vDotH );
        const float diffuseEnergy = 1.0f - saturate( dot( fresnel, float3( 0.2126f, 0.7152f, 0.0722f ) ) );
        result.diffuse += light.diffuse.xyz * lightScale * nDotL * diffuseEnergy;

        const float specularTerm = min( ggx_distribution( nDotH, roughness ) *
                                        smith_ggx_visibility( nDotV, nDotL, roughness ) * nDotL,
                                        4.0f );
        result.specular += light.specular.xyz * lightScale * specularTerm * fresnel;
    }

    return result;
}

kernel void main_metal
(
    texture2d<float, access::read_write> accumulationTexture [[texture(UAV_SLOT_START)]],
    texture2d<float, access::write> radianceTexture [[texture(UAV_SLOT_START + 1)]],

    constant PathTracerFrame *frame [[buffer(0)]],
    constant PathTracerLight *lights [[buffer(1)]],
    device const PathTracerMaterial *materials [[buffer(TEX_SLOT_START + 0)]],
    device const PathTracerGeometry *geometryRecords [[buffer(TEX_SLOT_START + 1)]],
    device const PathTracerTriangle *triangleRecords [[buffer(TEX_SLOT_START + 2)]],
    array<texture2d_array<float>, 8> diffuseTextures [[texture(10)]],
    array<texture2d_array<float>, 8> roughnessTextures [[texture(18)]],
    array<texture2d_array<float>, 8> normalTextures [[texture(26)]],
    array<texture2d_array<float>, 8> emissiveTextures [[texture(34)]],
    array<texturecube<float>, 4> reflectionTextures [[texture(42)]],
    sampler diffuseSampler [[sampler(10)]],

    instance_acceleration_structure accelerationStructure,
    intersection_function_table<triangle_data, instancing> intersectionFunctionTable,

    uint3 gl_GlobalInvocationID [[thread_position_in_grid]]
)
{
    const uint2 outputSize = uint2( radianceTexture.get_width(), radianceTexture.get_height() );
    if( gl_GlobalInvocationID.x >= outputSize.x || gl_GlobalInvocationID.y >= outputSize.y )
        return;

    const uint2 pixelPos = uint2( gl_GlobalInvocationID.xy );
    uint seed = wang_hash( pixelPos.x + pixelPos.y * 1664525u + frame->sampleIndex * 1013904223u );
    const float jitterX = rand01( seed );
    const float jitterY = rand01( seed );
    const float2 jitter = float2( jitterX, jitterY );
    const float2 uv = ( float2( pixelPos ) + jitter ) / float2( outputSize );

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
            const float skyScale = bounce == 0u ? 1.0f : 0.08f;
            radiance += throughput * sample_sky( pathRay.direction, *frame ) * skyScale;
            break;
        }

        const PathTracerTriangle triangle = load_triangle( hit.instance_id, hit.primitive_id,
                                                           geometryRecords, triangleRecords );
        const float3 rawNormal = load_triangle_normal( triangle, -pathRay.direction );
        const bool frontFacing = dot( pathRay.direction, rawNormal ) < 0.0f;
        const float3 geometricNormal = frontFacing ? rawNormal : -rawNormal;
        const float3 hitPosition = pathRay.origin + pathRay.direction * hit.distance;
        const SurfaceMaterial material = load_surface_material( hit.instance_id, materials, geometryRecords );

        const float opacity = material.transparency;
        const float3 textureColour = sample_diffuse_texture( material, triangle,
                                                             hit.triangle_barycentric_coord,
                                                             diffuseTextures, diffuseSampler );
        const float roughnessTexture = sample_roughness_texture( material, triangle,
                                                                 hit.triangle_barycentric_coord,
                                                                 roughnessTextures, diffuseSampler );
        const float mappedRoughness = material.hasRoughnessTexture ?
            max( material.roughness, roughnessTexture ) : material.roughness;
        const float roughness = clamp( mappedRoughness, 0.35f, 1.0f );
        const float3 shadingNormal = apply_normal_texture( material, triangle,
                                                           hit.triangle_barycentric_coord,
                                                           geometricNormal, normalTextures,
                                                           diffuseSampler );
        const float3 baseColor = material.baseColour * textureColour * opacity;
        const float3 emissiveTexture = sample_emissive_texture( material, triangle,
                                                                hit.triangle_barycentric_coord,
                                                                emissiveTextures, diffuseSampler );
        radiance += throughput * material.emissive * emissiveTexture;

        const float3 viewDirection = -pathRay.direction;
        const float3 bounceNormal = shadingNormal;
        const float nDotV = saturate( dot( shadingNormal, viewDirection ) );
        const float bounceNDotV = saturate( dot( bounceNormal, viewDirection ) );
        const float3 materialFresnel = material.fresnel * material.specularWeight;
        const float3 fresnelColor = fresnel_schlick( materialFresnel, nDotV );
        const DirectLighting directLighting = evaluate_direct_lighting( hitPosition, shadingNormal,
                                                                        geometricNormal,
                                                                        viewDirection, materialFresnel,
                                                                        roughness, hit.instance_id,
                                                                        lights, frame->numLights,
                                                                        accelerationStructure,
                                                                        materials, geometryRecords );
        const float specularLuminance = dot( fresnelColor, float3( 0.2126f, 0.7152f, 0.0722f ) );
        if( is_transparent_surface( material ) )
        {
            const float transmission = saturate( 1.0f - opacity );
            radiance += throughput * directLighting.specular;
            if( opacity > 0.001f )
            {
                const float3 skyDiffuse = sample_sky( shadingNormal, *frame ) * 0.025f;
                radiance += throughput * ( baseColor * 0.318309886f ) *
                            ( directLighting.diffuse + skyDiffuse ) * opacity * 0.25f;
            }

            const float reflectProbability = clamp( fresnel_schlick_luminance( materialFresnel, bounceNDotV ) *
                                                    ( 1.0f - roughness * 0.65f ),
                                                    0.01f, 0.55f );
            float3 nextDirection;
            float3 nextWeight;
            if( rand01( seed ) < reflectProbability )
            {
                const float3 reflectedDirection = reflect( pathRay.direction, bounceNormal );
                const float roughSampleX = rand01( seed );
                const float roughSampleY = rand01( seed );
                const float3 roughDirection = tangent_to_world(
                    cosine_sample_hemisphere( float2( roughSampleX, roughSampleY ) ), bounceNormal );
                nextDirection = normalize( mix( reflectedDirection, roughDirection, roughness * roughness ) );
                nextWeight = fresnelColor / reflectProbability;
            }
            else
            {
                const float eta = frontFacing ? ( 1.0f / 1.45f ) : 1.45f;
                float3 refractedDirection = refract( pathRay.direction, geometricNormal, eta );
                if( dot( refractedDirection, refractedDirection ) <= 1e-5f )
                {
                    nextDirection = reflect( pathRay.direction, geometricNormal );
                    nextWeight = fresnelColor;
                }
                else
                {
                    nextDirection = normalize( refractedDirection );
                    const float3 tint = mix( float3( 1.0f ), saturate( material.baseColour ),
                                             0.06f * max( transmission, 0.25f ) );
                    nextWeight = tint * max( transmission, 0.05f ) * 0.25f /
                                 max( 1.0f - reflectProbability, 0.05f );
                }
            }

            throughput *= min( nextWeight, float3( 1.0f ) );
            pathRay.origin = offset_ray( hitPosition,
                                         dot( nextDirection, geometricNormal ) < 0.0f ? -geometricNormal :
                                                                                        geometricNormal );
            pathRay.direction = nextDirection;
            pathRay.min_distance = 0.005f;
            pathRay.max_distance = INFINITY;
            continue;
        }

        const float3 skyDiffuse = sample_sky( shadingNormal, *frame ) * 0.08f;
        radiance += throughput * ( baseColor * 0.318309886f ) * ( directLighting.diffuse + skyDiffuse );
        radiance += throughput * directLighting.specular * opacity;
        if( material.hasReflectionTexture )
        {
            const float3 reflectionDirection = reflect( pathRay.direction, shadingNormal );
            const float reflectionWeight = saturate( specularLuminance ) *
                                           ( 1.0f - roughness * 0.95f ) * 0.20f;
            radiance += throughput * sample_reflection_texture( material, reflectionDirection,
                                                                reflectionTextures, diffuseSampler ) *
                        fresnelColor * reflectionWeight * opacity;
        }

        const float diffuseLuminance = dot( baseColor, float3( 0.2126f, 0.7152f, 0.0722f ) );
        const float diffuseBsdfWeight = diffuseLuminance * ( 1.0f - specularLuminance );
        const float specularBsdfWeight = specularLuminance * ( 1.0f - roughness ) * 0.20f;
        const float bsdfWeightSum = max( diffuseBsdfWeight + specularBsdfWeight, 1e-4f );
        const float specularProbability = clamp( specularBsdfWeight / bsdfWeightSum, 0.005f, 0.08f );
        float3 nextDirection;
        float3 bounceWeight;
        if( rand01( seed ) < specularProbability )
        {
            const float3 reflectedDirection = reflect( pathRay.direction, bounceNormal );
            const float roughSampleX = rand01( seed );
            const float roughSampleY = rand01( seed );
            const float3 roughDirection = tangent_to_world(
                cosine_sample_hemisphere( float2( roughSampleX, roughSampleY ) ), bounceNormal );
            nextDirection = normalize( mix( reflectedDirection, roughDirection, roughness * roughness ) );
            bounceWeight = fresnelColor / max( specularProbability, 1e-4f );
        }
        else
        {
            const float diffuseSampleX = rand01( seed );
            const float diffuseSampleY = rand01( seed );
            const float3 localDirection = cosine_sample_hemisphere( float2( diffuseSampleX, diffuseSampleY ) );
            nextDirection = tangent_to_world( localDirection, bounceNormal );
            bounceWeight = baseColor * ( 1.0f - specularLuminance ) * 0.82f /
                           max( 1.0f - specularProbability, 1e-4f );
        }

        throughput *= min( bounceWeight, float3( 1.0f ) );
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

    radiance = min( max( radiance, float3( 0.0f ) ), float3( 32.0f ) );

    const bool resetAccumulation = frame->sampleIndex == 0u;
    const float4 previous = resetAccumulation ? float4( 0.0f ) : accumulationTexture.read( pixelPos );
    const float4 accumulated = previous + float4( radiance, 1.0f );
    accumulationTexture.write( accumulated, pixelPos );

    const float sampleCount = max( accumulated.w, 1.0f );
    const float invSampleCount = 1.0f / sampleCount;
    radianceTexture.write( float4( accumulated.xyz * invSampleCount, sampleCount ), pixelPos );
}
