#include <metal_stdlib>
using namespace metal;

struct PathTracerAsInstanceInput
{
    uint accelerationStructureIndex;
    uint sourceInstanceIndex;
    uint active;
    uint tier;
    float4 lodBoundsCenterRadius;
    float4 boundsCenterRadius;
    float4 transformRow0;
    float4 transformRow1;
    float4 transformRow2;
    float4 transformRow3;
};

struct PathTracerPackedFloat4x3
{
    packed_float3 column0;
    packed_float3 column1;
    packed_float3 column2;
    packed_float3 column3;
};

struct PathTracerIndirectInstanceDescriptor
{
    PathTracerPackedFloat4x3 transformationMatrix;
    uint options;
    uint mask;
    uint intersectionFunctionTableOffset;
    uint userID;
    ulong accelerationStructureID;
};

struct PathTracerAsCullParams
{
    float4 cameraPositionAndMaxDistance;
    float4 cameraForwardAndNear;
    float4 cameraRightAndTanHalfFovX;
    float4 cameraUpAndTanHalfFovY;
    float4 cullOptions;
};

static inline bool pathtracer_is_instance_active( constant PathTracerAsInstanceInput &src,
                                                  constant PathTracerAsCullParams &cullParams )
{
    const float3 cameraPos = cullParams.cameraPositionAndMaxDistance.xyz;
    const float maxCullDistance = cullParams.cameraPositionAndMaxDistance.w;
    const uint cullMode = uint( cullParams.cullOptions.x + 0.5f );
    const bool useDistanceCull = cullMode == 1u || cullMode == 3u;
    const bool useFrustumCull = cullMode == 2u || cullMode == 3u;
    const float3 toBounds = src.boundsCenterRadius.xyz - cameraPos;
    const float radius = src.boundsCenterRadius.w;
    const float cullDistance = maxCullDistance + radius;
    const bool distanceVisible = !useDistanceCull || maxCullDistance <= 0.0f ||
                                 dot( toBounds, toBounds ) <= cullDistance * cullDistance;

    bool coneVisible = true;
    if( useFrustumCull )
    {
        const float3 cameraForward = cullParams.cameraForwardAndNear.xyz;
        const float3 cameraRight = cullParams.cameraRightAndTanHalfFovX.xyz;
        const float3 cameraUp = cullParams.cameraUpAndTanHalfFovY.xyz;
        const float depth = dot( toBounds, cameraForward );
        const float nearDistance = cullParams.cameraForwardAndNear.w;
        const float farDistance = maxCullDistance > 0.0f ? maxCullDistance : cullParams.cullOptions.z;
        const float projectedDepth = max( depth, 0.0f );
        const float reflectionConeExpansion = max( cullParams.cullOptions.y, 1.0f );
        const float tanHalfFovX = cullParams.cameraRightAndTanHalfFovX.w * reflectionConeExpansion;
        const float tanHalfFovY = cullParams.cameraUpAndTanHalfFovY.w * reflectionConeExpansion;
        const float horizontalDistance = abs( dot( toBounds, cameraRight ) );
        const float verticalDistance = abs( dot( toBounds, cameraUp ) );

        const bool depthVisible = depth + radius >= nearDistance &&
                                  ( farDistance <= 0.0f || depth - radius <= farDistance );
        const bool horizontalVisible = horizontalDistance <= projectedDepth * tanHalfFovX + radius;
        const bool verticalVisible = verticalDistance <= projectedDepth * tanHalfFovY + radius;
        coneVisible = depthVisible && horizontalVisible && verticalVisible;
    }

    return src.active != 0u && distanceVisible && coneVisible;
}

kernel void pathtracer_classify_indirect_as_instances(
    constant PathTracerAsInstanceInput *inputs [[buffer(0)]],
    device uint *instanceActiveFlags [[buffer(1)]],
    constant uint &numInstances [[buffer(2)]],
    constant PathTracerAsCullParams &cullParams [[buffer(3)]],
    uint tid [[thread_position_in_grid]] )
{
    if( tid < numInstances )
        instanceActiveFlags[tid] = pathtracer_is_instance_active( inputs[tid], cullParams ) ? 1u : 0u;
}

kernel void pathtracer_write_indirect_as_instances(
    constant PathTracerAsInstanceInput *inputs [[buffer(0)]],
    constant ulong *accelerationStructureIds [[buffer(1)]],
    device const uint *instanceActiveFlags [[buffer(2)]],
    device PathTracerIndirectInstanceDescriptor *descriptors [[buffer(3)]],
    device atomic_uint *instanceCount [[buffer(4)]],
    constant uint &numInstances [[buffer(5)]],
    uint tid [[thread_position_in_grid]] )
{
    if( tid != 0u )
        return;

    uint outCount = 0u;
    for( uint idx = 0u; idx < numInstances; ++idx )
    {
        if( instanceActiveFlags[idx] == 0u )
            continue;

        constant PathTracerAsInstanceInput &src = inputs[idx];
        device PathTracerIndirectInstanceDescriptor &dst = descriptors[outCount++];

        dst.accelerationStructureID = accelerationStructureIds[src.accelerationStructureIndex];
        dst.userID = src.sourceInstanceIndex;
        dst.options = 4u; // MTLAccelerationStructureInstanceOptionOpaque
        dst.mask = 1u;
        dst.intersectionFunctionTableOffset = 0u;

        dst.transformationMatrix.column0 = packed_float3( src.transformRow0.x, src.transformRow1.x,
                                                          src.transformRow2.x );
        dst.transformationMatrix.column1 = packed_float3( src.transformRow0.y, src.transformRow1.y,
                                                          src.transformRow2.y );
        dst.transformationMatrix.column2 = packed_float3( src.transformRow0.z, src.transformRow1.z,
                                                          src.transformRow2.z );
        dst.transformationMatrix.column3 = packed_float3( src.transformRow0.w, src.transformRow1.w,
                                                          src.transformRow2.w );
    }

    atomic_store_explicit( instanceCount, outCount, memory_order_relaxed );
}
