#include <metal_stdlib>
using namespace metal;

struct PathTracerAsInstanceInput
{
    uint accelerationStructureIndex;
    uint sourceInstanceIndex;
    uint active;
    // Decoded from the packed instanceTiers metadata stream on the CPU side.
    uint tier;
    uint previousTier;
    uint availableTiersMask;
    uint reserved0;
    uint reserved1;
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
    float4 lodOptions;
};

static inline uint pathtracer_choose_instance_tier( constant PathTracerAsInstanceInput &src,
                                                    constant PathTracerAsCullParams &cullParams )
{
    const bool hasSimplified = ( src.availableTiersMask & 0x2u ) != 0u;
    const bool hasProxy = ( src.availableTiersMask & 0x4u ) != 0u;
    const uint forcedLodOverride = uint( cullParams.lodOptions.x + 0.5f );
    if( forcedLodOverride == 3u )
        return hasProxy ? 2u : 0u;
    if( forcedLodOverride == 2u )
        return hasSimplified ? 1u : 0u;
    if( forcedLodOverride == 1u )
        return 0u;
    if( !hasSimplified && !hasProxy )
        return 0u;

    const float pixelDisplayRatio = cullParams.cullOptions.w;
    if( pixelDisplayRatio <= 1e-6f )
        return 0u;

    const float3 cameraPos = cullParams.cameraPositionAndMaxDistance.xyz;
    const float worldRadius = max( src.lodBoundsCenterRadius.w, 1e-4f );
    const float3 toLodBounds = src.lodBoundsCenterRadius.xyz - cameraPos;
    const float cameraDistanceSq = max( dot( toLodBounds, toLodBounds ), 1e-8f );

    const float proxyEnterPixels = 8.0f;
    const float proxyExitPixels = 12.0f;
    const float simplifiedEnterPixels = 32.0f;
    const float simplifiedExitPixels = 48.0f;
    const float diameterScale = ( worldRadius * 2.0f ) / pixelDisplayRatio;
    const float proxyEnterDistanceSq =
        ( diameterScale / proxyEnterPixels ) * ( diameterScale / proxyEnterPixels );
    const float proxyExitDistanceSq =
        ( diameterScale / proxyExitPixels ) * ( diameterScale / proxyExitPixels );
    const float simplifiedEnterDistanceSq =
        ( diameterScale / simplifiedEnterPixels ) * ( diameterScale / simplifiedEnterPixels );
    const float simplifiedExitDistanceSq =
        ( diameterScale / simplifiedExitPixels ) * ( diameterScale / simplifiedExitPixels );

    if( hasProxy )
    {
        if( src.previousTier == 2u && cameraDistanceSq > proxyExitDistanceSq )
            return 2u;
        if( src.previousTier != 2u && cameraDistanceSq > proxyEnterDistanceSq )
            return 2u;
    }

    if( hasSimplified )
    {
        if( src.previousTier == 1u && cameraDistanceSq > simplifiedExitDistanceSq )
            return 1u;
        if( src.previousTier != 1u && cameraDistanceSq > simplifiedEnterDistanceSq )
            return 1u;
    }

    return 0u;
}

static inline bool pathtracer_is_instance_active( constant PathTracerAsInstanceInput &src,
                                                  constant PathTracerAsCullParams &cullParams )
{
    if( src.active == 0u )
        return false;

    // Most candidates are rejected because they belong to a non-selected tier. Resolve the tier
    // first so we can avoid the more expensive distance/frustum math for those losers.
    const uint selectedTier = pathtracer_choose_instance_tier( src, cullParams );
    if( src.tier != selectedTier )
        return false;

    const float3 cameraPos = cullParams.cameraPositionAndMaxDistance.xyz;
    const float maxCullDistance = cullParams.cameraPositionAndMaxDistance.w;
    const uint cullMode = uint( cullParams.cullOptions.x + 0.5f );
    const bool useDistanceCull = cullMode == 1u || cullMode == 3u;
    const bool useFrustumCull = cullMode == 2u || cullMode == 3u;
    if( !useDistanceCull && !useFrustumCull )
        return true;

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

    return distanceVisible && coneVisible;
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
    device uint *blockSums [[buffer(2)]],
    device atomic_uint *instanceCount [[buffer(3)]],
    constant uint &numThreadgroups [[buffer(4)]],
    constant uint &writeTotalCount [[buffer(5)]],
    uint3 threadPositionInGrid [[thread_position_in_grid]],
    uint localTid [[thread_index_in_threadgroup]],
    uint3 threadgroupPositionInGrid [[threadgroup_position_in_grid]],
    uint3 threadsPerThreadgroup [[threads_per_threadgroup]] )
{
    threadgroup uint localScan[256];
    const uint tid = threadPositionInGrid.x;
    const uint localSize = threadsPerThreadgroup.x;
    const uint blockId = threadgroupPositionInGrid.x;
    const uint blockBase = blockId * localSize;
    const uint validCount = blockBase < numThreadgroups ? min( localSize, numThreadgroups - blockBase ) : 0u;
    const bool inRange = tid < numThreadgroups;
    const uint value = inRange ? threadgroupCounts[tid] : 0u;

    localScan[localTid] = value;
    threadgroup_barrier( mem_flags::mem_threadgroup );

    for( uint offset = 1u; offset < localSize; offset <<= 1u )
    {
        uint addend = 0u;
        if( localTid >= offset )
            addend = localScan[localTid - offset];
        threadgroup_barrier( mem_flags::mem_threadgroup );
        localScan[localTid] += addend;
        threadgroup_barrier( mem_flags::mem_threadgroup );
    }

    if( inRange )
        threadgroupOffsets[tid] = localScan[localTid] - value;

    if( validCount > 0u && localTid + 1u == validCount )
    {
        const uint blockSum = localScan[localTid];
        blockSums[blockId] = blockSum;
        if( writeTotalCount != 0u )
            atomic_store_explicit( instanceCount, blockSum, memory_order_relaxed );
    }
}

kernel void pathtracer_add_indirect_as_block_offsets(
    device uint *threadgroupOffsets [[buffer(0)]],
    device const uint *blockOffsets [[buffer(1)]],
    constant uint &numThreadgroups [[buffer(2)]],
    uint3 threadPositionInGrid [[thread_position_in_grid]],
    uint3 threadgroupPositionInGrid [[threadgroup_position_in_grid]] )
{
    const uint tid = threadPositionInGrid.x;
    if( tid >= numThreadgroups )
        return;

    threadgroupOffsets[tid] += blockOffsets[threadgroupPositionInGrid.x];
}

kernel void pathtracer_scatter_indirect_as_instances(
    constant PathTracerAsInstanceInput *inputs [[buffer(0)]],
    constant ulong *accelerationStructureIds [[buffer(1)]],
    device const uint *instanceActiveFlags [[buffer(2)]],
    device const uint *instanceLocalOffsets [[buffer(3)]],
    device const uint *threadgroupOffsets [[buffer(4)]],
    device PathTracerIndirectInstanceDescriptor *descriptors [[buffer(5)]],
    device uint *selectedCandidateIndices [[buffer(6)]],
    constant uint &numInstances [[buffer(7)]],
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
    // Store the stable candidate ID alongside the compacted descriptor. The TLAS instance slot may
    // change every frame after compaction, while candidate IDs are the durable keys for geometry,
    // triangle and material records uploaded by the CPU.
    selectedCandidateIndices[dstIndex] = src.sourceInstanceIndex;
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
