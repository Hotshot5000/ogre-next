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
    device uint *instanceLocalOffsets [[buffer(2)]],
    device uint *threadgroupCounts [[buffer(3)]],
    constant uint &numInstances [[buffer(4)]],
    constant PathTracerAsCullParams &cullParams [[buffer(5)]],
    uint3 threadPositionInGrid [[thread_position_in_grid]],
    uint localTid [[thread_index_in_threadgroup]],
    uint3 threadgroupPositionInGrid [[threadgroup_position_in_grid]],
    uint3 threadsPerThreadgroup [[threads_per_threadgroup]] )
{
    threadgroup uint localScan[256];
    const uint tid = threadPositionInGrid.x;
    const uint threadgroupId = threadgroupPositionInGrid.x;
    const uint threadsPerGroup = threadsPerThreadgroup.x;
    const bool inRange = tid < numInstances;
    const uint active = inRange && pathtracer_is_instance_active( inputs[tid], cullParams ) ? 1u : 0u;

    localScan[localTid] = active;
    threadgroup_barrier( mem_flags::mem_threadgroup );

    for( uint offset = 1u; offset < threadsPerGroup; offset <<= 1u )
    {
        uint value = 0u;
        if( localTid >= offset )
            value = localScan[localTid - offset];
        threadgroup_barrier( mem_flags::mem_threadgroup );
        localScan[localTid] += value;
        threadgroup_barrier( mem_flags::mem_threadgroup );
    }

    if( inRange )
    {
        instanceActiveFlags[tid] = active;
        instanceLocalOffsets[tid] = active != 0u ? localScan[localTid] - 1u : 0u;
    }

    if( localTid + 1u == threadsPerGroup )
        threadgroupCounts[threadgroupId] = localScan[localTid];
}

kernel void pathtracer_prefix_indirect_as_threadgroups(
    device const uint *threadgroupCounts [[buffer(0)]],
    device uint *threadgroupOffsets [[buffer(1)]],
    device atomic_uint *instanceCount [[buffer(2)]],
    constant uint &numThreadgroups [[buffer(3)]],
    uint tid [[thread_position_in_grid]] )
{
    if( tid != 0u )
        return;

    uint prefix = 0u;
    for( uint groupIdx = 0u; groupIdx < numThreadgroups; ++groupIdx )
    {
        threadgroupOffsets[groupIdx] = prefix;
        prefix += threadgroupCounts[groupIdx];
    }

    atomic_store_explicit( instanceCount, prefix, memory_order_relaxed );
}

kernel void pathtracer_scatter_indirect_as_instances(
    constant PathTracerAsInstanceInput *inputs [[buffer(0)]],
    constant ulong *accelerationStructureIds [[buffer(1)]],
    device const uint *instanceActiveFlags [[buffer(2)]],
    device const uint *instanceLocalOffsets [[buffer(3)]],
    device const uint *threadgroupOffsets [[buffer(4)]],
    device PathTracerIndirectInstanceDescriptor *descriptors [[buffer(5)]],
    constant uint &numInstances [[buffer(6)]],
    uint3 threadPositionInGrid [[thread_position_in_grid]],
    uint3 threadgroupPositionInGrid [[threadgroup_position_in_grid]] )
{
    const uint tid = threadPositionInGrid.x;
    if( tid >= numInstances || instanceActiveFlags[tid] == 0u )
        return;

    const uint dstIndex = threadgroupOffsets[threadgroupPositionInGrid.x] + instanceLocalOffsets[tid];
    constant PathTracerAsInstanceInput &src = inputs[tid];
    device PathTracerIndirectInstanceDescriptor &dst = descriptors[dstIndex];

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
