/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2017 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "OgreVulkanStagingTexture.h"


#include "OgreVulkanUtils2.h"
#include "OgreVulkanTextureGpu.h"
#include "Vao/OgreVulkanVaoManager.h"
#include "OgreVulkanDevice.h"
#include "OgreVulkanUtils.h"

namespace Ogre
{
    VulkanStagingTexture::VulkanStagingTexture( VaoManager *vaoManager, PixelFormatGpu formatFamily,
                                                size_t size, VkDeviceMemory deviceMemory,
                                                VkBuffer buffer, VulkanDynamicBuffer *dynamicBuffer ) :
        StagingTextureBufferImpl( vaoManager, formatFamily, size, 0, 0 ),
        mDeviceMemory( deviceMemory ),
        mVboName( buffer ),
        // mDynamicBuffer( (uint8 *)OGRE_MALLOC_SIMD( size, MEMCATEGORY_RENDERSYS ) ),
        mDynamicBuffer( dynamicBuffer ),
        mMappedPtr( 0 ),
        mLastMappedPtr( 0 )
    {
        mMappedPtr = mDynamicBuffer;
        mLastMappedPtr = mMappedPtr;
    }
    //-----------------------------------------------------------------------------------
    VulkanStagingTexture::~VulkanStagingTexture()
    {
        if( mDynamicBuffer )
        {
            // OGRE_FREE_SIMD( mDynamicBuffer, MEMCATEGORY_RENDERSYS );
            mDynamicBuffer = 0;
        }
        mMappedPtr = 0;
    }
    //-----------------------------------------------------------------------------------
    bool VulkanStagingTexture::belongsToUs( const TextureBox &box )
    {
        return box.data >= mLastMappedPtr &&
               box.data <= static_cast<uint8 *>( mLastMappedPtr ) + mCurrentOffset;
    }
    //-----------------------------------------------------------------------------------
    void *RESTRICT_ALIAS_RETURN VulkanStagingTexture::mapRegionImplRawPtr( void )
    {
        return static_cast<uint8 *>( mMappedPtr ) + mCurrentOffset;
    }
    //-----------------------------------------------------------------------------------
    void VulkanStagingTexture::_unmapBuffer()
    {
        mMappedPtr = 0;
    }
    //-----------------------------------------------------------------------------------
    void VulkanStagingTexture::startMapRegion()
    {
        StagingTextureBufferImpl::startMapRegion();

        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );
        VulkanDevice *device = vaoManager->getDevice();

        VkDeviceSize size = alignMemory( mSize, device->mDeviceProperties.limits.nonCoherentAtomSize );

        VkResult result =
            vkMapMemory( device->mDevice, mDeviceMemory, mInternalBufferStart, size, 0, &mMappedPtr );
        checkVkResult( result, "vkMapMemory" );

        mLastMappedPtr = mMappedPtr;

        VkMappedMemoryRange memRange;
        makeVkStruct( memRange, VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE );
        memRange.memory = mDeviceMemory;
        memRange.offset = mInternalBufferStart;
        memRange.size = size;
        result = vkInvalidateMappedMemoryRanges( device->mDevice, 1, &memRange );
        checkVkResult( result, "vkInvalidateMappedMemoryRanges" );

        // mMappedPtr =
        //     mBufferInterface->map( mInternalBufferStart + mMappingStart, sizeBytes,
        //     MappingState::MS_UNMAPPED );

        // mMappedPtr = mVulkanDataPtr + mInternalBufferStart + mMappingStart;

    }
    //-----------------------------------------------------------------------------------
    void VulkanStagingTexture::stopMapRegion()
    {
        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );
        VulkanDevice *device = vaoManager->getDevice();

        VkCommandBuffer cmdBuffer = device->mGraphicsQueue.mCurrentCmdBuffer;

        VkDeviceSize size = alignMemory( mSize, device->mDeviceProperties.limits.nonCoherentAtomSize );

        // vkBindBufferMemory( device->mDevice, mVboName, mDeviceMemory, 0 );

        VkMappedMemoryRange memRange;
        makeVkStruct( memRange, VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE );
        memRange.memory = mDeviceMemory;
        memRange.offset = mInternalBufferStart;
        memRange.size = size;
        VkResult result = vkFlushMappedMemoryRanges( device->mDevice, 1, &memRange );
        checkVkResult( result, "VkMappedMemoryRange" );

        vkUnmapMemory( device->mDevice, mDeviceMemory );

        mMappedPtr = 0;

        StagingTextureBufferImpl::stopMapRegion();
    }
    //-----------------------------------------------------------------------------------
    void VulkanStagingTexture::upload( const TextureBox &srcBox, TextureGpu *dstTexture, uint8 mipLevel,
                                       const TextureBox *cpuSrcBox, const TextureBox *dstBox,
                                       bool skipSysRamCopy )
    {
        StagingTextureBufferImpl::upload( srcBox, dstTexture, mipLevel, cpuSrcBox, dstBox,
                                          skipSysRamCopy );

        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );
        VulkanDevice *device = vaoManager->getDevice();

        size_t bytesPerRow = srcBox.bytesPerRow;
        size_t bytesPerImage = srcBox.bytesPerImage;

        // PVR textures must have 0 on these.
        if( mFormatFamily == PFG_PVRTC2_2BPP || mFormatFamily == PFG_PVRTC2_2BPP_SRGB ||
            mFormatFamily == PFG_PVRTC2_4BPP || mFormatFamily == PFG_PVRTC2_4BPP_SRGB ||
            mFormatFamily == PFG_PVRTC_RGB2 || mFormatFamily == PFG_PVRTC_RGB2_SRGB ||
            mFormatFamily == PFG_PVRTC_RGB4 || mFormatFamily == PFG_PVRTC_RGB4_SRGB ||
            mFormatFamily == PFG_PVRTC_RGBA2 || mFormatFamily == PFG_PVRTC_RGBA2_SRGB ||
            mFormatFamily == PFG_PVRTC_RGBA4 || mFormatFamily == PFG_PVRTC_RGBA4_SRGB )
        {
            bytesPerRow = 0;
            bytesPerImage = 0;
        }

        // Recommended by documentation to set this value to 0 for non-3D textures.
        if( dstTexture->getTextureType() != TextureTypes::Type3D )
            bytesPerImage = 0;

        assert( dynamic_cast<VulkanTextureGpu *>( dstTexture ) );
        VulkanTextureGpu *dstTextureVulkan = static_cast<VulkanTextureGpu *>( dstTexture );

        const size_t srcOffset =
            reinterpret_cast<uint8 *>( srcBox.data ) - reinterpret_cast<uint8 *>( mLastMappedPtr );
        const uint32 destinationSlice =
            ( dstBox ? dstBox->sliceStart : 0 ) + dstTexture->getInternalSliceStart();
        uint32 xPos = static_cast<uint32>( dstBox ? dstBox->x : 0 );
        uint32 yPos = static_cast<uint32>( dstBox ? dstBox->y : 0 );
        uint32 zPos = static_cast<uint32>( dstBox ? dstBox->z : 0 );


        for( uint32 i = 0; i < srcBox.numSlices; ++i )
        {
            VkCommandBuffer commandBuffer = beginSingleTimeCommands( device );
            VkBufferImageCopy region;
            region.bufferOffset = srcOffset + srcBox.bytesPerImage * i;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;

            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mipLevel;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;

            region.imageOffset.x = xPos;
            region.imageOffset.y = yPos;
            region.imageOffset.z = zPos;
            region.imageExtent = { srcBox.width, srcBox.height, srcBox.depth };
            vkCmdCopyBufferToImage( device->mGraphicsQueue.mCurrentCmdBuffer, mVboName,
                                    dstTextureVulkan->getFinalTextureName(),
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

            endSingleTimeCommands( device, commandBuffer );

            commandBuffer = beginSingleTimeCommands( device );

            // The sub resource range describes the regions of the image that will be transitioned using
            // the
            // memory barriers below
            VkImageSubresourceRange subresource_range = {};
            // Image only contains color data
            // if( PixelFormatGpuUtils::isDepth( mPixelFormat ) )
            // {
            //     subresource_range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            //     if( PixelFormatGpuUtils::isStencil( mPixelFormat ) )
            //         subresource_range.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            // }
            // else
            subresource_range.aspectMask = region.imageSubresource.aspectMask;
            // Start at first mip level
            subresource_range.baseMipLevel = region.imageSubresource.mipLevel;
            // We will transition on all mip levels
            subresource_range.levelCount = dstTextureVulkan->getNumMipmaps() - subresource_range.baseMipLevel;
            // The 2D texture only has one layer
            subresource_range.layerCount = region.imageSubresource.layerCount;

            set_image_layout( device->mGraphicsQueue.mCurrentCmdBuffer,
                              dstTextureVulkan->getFinalTextureName(),
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresource_range,
                              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT );

            endSingleTimeCommands( device, commandBuffer );
            // [blitEncoder copyFromBuffer:mVboName
            //                sourceOffset:srcOffset + srcBox.bytesPerImage * i
            //           sourceBytesPerRow:bytesPerRow
            //         sourceBytesPerImage:bytesPerImage
            //                  sourceSize:MTLSizeMake( srcBox.width, srcBox.height, srcBox.depth )
            //                   toTexture:dstTextureVulkan->getFinalTextureName()
            //            destinationSlice:destinationSlice + i
            //            destinationLevel:mipLevel
            //           destinationOrigin:MTLOriginMake( xPos, yPos, zPos )];
        }

    }
}  // namespace Ogre
