#include <metal_stdlib>
using namespace metal;

struct PS_INPUT
{
    float2 uv0;
};

static float luminance( float3 color )
{
    return dot( color, float3( 0.2126f, 0.7152f, 0.0722f ) );
}

static float3 tonemapReinhard( float3 color )
{
    color *= 0.125f;
    color = color / ( color + float3( 1.0f ) );
    return saturate( color );
}

static float3 denoiseRadiance( texture2d<float> radianceTexture,
                               sampler samplerState,
                               float2 uv,
                               float sampleCount )
{
    const float3 center = max( radianceTexture.sample( samplerState, uv ).xyz, float3( 0.0f ) );
    const float denoiseStrength = saturate( ( 96.0f - sampleCount ) / 96.0f );
    if( denoiseStrength <= 0.001f )
        return center;

    const float2 texelSize = 1.0f / float2( radianceTexture.get_width(), radianceTexture.get_height() );
    const float centerLum = luminance( center );
    const float colorSigma = mix( 0.04f, 0.18f, denoiseStrength );
    const float spatialSigma = mix( 0.75f, 1.65f, denoiseStrength );

    float3 accum = center;
    float weightSum = 1.0f;

    for( int y = -2; y <= 2; ++y )
    {
        for( int x = -2; x <= 2; ++x )
        {
            if( x == 0 && y == 0 )
                continue;

            const float2 pixelOffset = float2( x, y );
            const float3 sampleColor = max( radianceTexture.sample( samplerState,
                                                                     uv + pixelOffset * texelSize ).xyz,
                                             float3( 0.0f ) );
            const float spatialWeight = exp( -dot( pixelOffset, pixelOffset ) /
                                             ( 2.0f * spatialSigma * spatialSigma ) );
            const float colorDelta = abs( luminance( sampleColor ) - centerLum );
            const float colorWeight = exp( -( colorDelta * colorDelta ) /
                                           ( 2.0f * colorSigma * colorSigma ) );
            const float weight = spatialWeight * colorWeight;
            accum += sampleColor * weight;
            weightSum += weight;
        }
    }

    const float3 filtered = accum / max( weightSum, 1e-4f );
    return mix( center, filtered, denoiseStrength * 0.85f );
}

fragment float4 main_metal
(
    PS_INPUT inPs [[stage_in]],
    texture2d<float> radianceTexture [[texture(0)]],
    sampler samplerState0 [[sampler(0)]],
    constant float &useFallbackDenoiser [[buffer(PARAMETER_SLOT)]]
)
{
    const float4 centerSample = radianceTexture.sample( samplerState0, inPs.uv0 );
    const float sampleCount = max( centerSample.w, 1.0f );
    float3 color = max( centerSample.xyz, float3( 0.0f ) );
    if( useFallbackDenoiser > 0.5f )
        color = denoiseRadiance( radianceTexture, samplerState0, inPs.uv0, sampleCount );
    color = tonemapReinhard( color );
    color = pow( color, float3( 1.0f / 2.2f ) );
    return float4( color, 1.0f );
}
