#include <metal_stdlib>
using namespace metal;

struct PS_INPUT
{
    float2 uv0;
};

static float3 tonemapAcesApprox( float3 color )
{
    color *= 0.6f;
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate( ( color * ( a * color + b ) ) / ( color * ( c * color + d ) + e ) );
}

fragment float4 main_metal
(
    PS_INPUT inPs [[stage_in]],
    texture2d<float> radianceTexture [[texture(0)]],
    sampler samplerState0 [[sampler(0)]]
)
{
    float3 color = max( radianceTexture.sample( samplerState0, inPs.uv0 ).xyz, float3( 0.0f ) );
    color = tonemapAcesApprox( color );
    color = pow( color, float3( 1.0f / 2.2f ) );
    return float4( color, 1.0f );
}
