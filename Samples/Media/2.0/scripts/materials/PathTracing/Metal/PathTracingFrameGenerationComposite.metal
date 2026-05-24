#include <metal_stdlib>
using namespace metal;

struct VOut
{
    float4 position [[position]];
    float2 uv;
};

vertex VOut framegen_composite_vs( uint vertexId [[vertex_id]] )
{
    const float2 pos = vertexId == 0u ? float2( -1.0f, -1.0f ) :
                       ( vertexId == 1u ? float2( 3.0f, -1.0f ) : float2( -1.0f, 3.0f ) );

    VOut out;
    out.position = float4( pos, 0.0f, 1.0f );
    out.uv = float2( pos.x * 0.5f + 0.5f, 0.5f - pos.y * 0.5f );
    return out;
}

fragment float4 framegen_composite_ps( VOut in [[stage_in]],
                                       texture2d<float> generatedTexture [[texture(0)]],
                                       texture2d<float> currentSceneTexture [[texture(1)]],
                                       texture2d<float> currentPresentedTexture [[texture(2)]],
                                       sampler samplerState [[sampler(0)]] )
{
    const float4 generated = generatedTexture.sample( samplerState, in.uv );
    const float4 currentScene = currentSceneTexture.sample( samplerState, in.uv );
    const float4 currentPresented = currentPresentedTexture.sample( samplerState, in.uv );
    const float3 delta = abs( currentPresented.rgb - currentScene.rgb );
    const float overlayMask = max( delta.r, max( delta.g, delta.b ) ) > ( 2.0f / 255.0f ) ? 1.0f : 0.0f;
    return mix( generated, currentPresented, overlayMask );
}
