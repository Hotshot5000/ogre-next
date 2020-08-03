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

#include "OgreVulkanTextureGpuManager.h"

#include "OgreVulkanAsyncTextureTicket.h"
#include "OgreVulkanMappings.h"
#include "OgreVulkanStagingTexture.h"
#include "OgreVulkanTextureGpu.h"
#include "OgreVulkanTextureGpuWindow.h"
#include "OgreVulkanUtils.h"

#include "Vao/OgreVulkanVaoManager.h"

#include "OgrePixelFormatGpuUtils.h"
#include "OgreVector2.h"

#include "OgreException.h"

namespace Ogre
{
    static const bool c_bSkipAliasable = true;

    VulkanTextureGpuManager::VulkanTextureGpuManager( VulkanVaoManager *vaoManager,
                                                      RenderSystem *renderSystem,
                                                      VulkanDevice *device ) :
        TextureGpuManager( vaoManager, renderSystem ),
        mDevice( device )
    {
        VkImageCreateInfo imageInfo;
        makeVkStruct( imageInfo, VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO );
        imageInfo.extent.width = 4u;
        imageInfo.mipLevels = 1u;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        // clang-format off
        const char *dummyNames[] =
        {
            "Dummy Unknown (2D)",
            "Dummy 1D 4x1",
            "Dummy 1DArray 4x1",
            "Dummy 2D 4x4",
            "Dummy 2DArray 4x4x1",
            "Dummy Cube 4x4",
            "Dummy CubeArray 4x4x1",
            "Dummy 3D 4x4x4"
        };
        // clang-format on

        // Must be large enough to hold the biggest transfer we'll do.
        uint8 c_whiteData[4 * 4 * 6 * 4];
        uint8 c_blackData[4 * 4 * 6 * 4];
        memset( c_whiteData, 0xff, sizeof( c_whiteData ) );
        memset( c_blackData, 0x00, sizeof( c_blackData ) );

        const uint32 bytesPerPixel =
            static_cast<uint32>( PixelFormatGpuUtils::getBytesPerPixel( PFG_RGBA8_UNORM ) );
        TextureBox whiteBox( 4u, 4u, 1u, 6u, bytesPerPixel, bytesPerPixel * 4u,
                             bytesPerPixel * 4u * 4u );
        whiteBox.data = c_whiteData;
        TextureBox blackBox( whiteBox );
        blackBox.data = c_blackData;

        // Use a VulkanStagingTexture but we will manually handle the upload.
        // The barriers are easy because there is no work using any of this data
        VulkanStagingTexture *stagingTex =
            vaoManager->createStagingTexture( PFG_RGBA8_UNORM, sizeof( c_whiteData ) * 2u );

        stagingTex->startMapRegion();
        TextureBox box = stagingTex->mapRegion( 4u, 4u, 1u, 6u * 2u, PFG_RGBA8_UNORM );
        // Don't manipulate box.sliceStart in normal code. We're doing this because we have
        // control on how VulkanStagingTexture works (but we don't control how the other APIs work)
        box.copyFrom( whiteBox );
        box.sliceStart = 6u;
        box.copyFrom( blackBox );
        box.sliceStart = 0u;
        stagingTex->stopMapRegion();

        VkBuffer stagingBuffVboName = stagingTex->_getVboName();

        for( size_t i = 1u; i < TextureTypes::Type3D + 1u; ++i )
        {
            if( c_bSkipAliasable && ( i == TextureTypes::Type1DArray || i == TextureTypes::Type2DArray ||
                                      i == TextureTypes::TypeCubeArray ) )
            {
                continue;
            }

            if( i == TextureTypes::Type3D )
            {
                imageInfo.extent.depth = 4u;
                imageInfo.imageType = VK_IMAGE_TYPE_3D;
            }
            else
            {
                imageInfo.extent.depth = 1u;
                imageInfo.imageType = VK_IMAGE_TYPE_2D;
            }
            if( i == TextureTypes::Type1D || i == TextureTypes::Type1DArray )
            {
                imageInfo.extent.height = 1u;
                imageInfo.imageType = VK_IMAGE_TYPE_1D;
            }
            else
            {
                imageInfo.extent.height = 4u;
            }

            if( i == TextureTypes::TypeCube || i == TextureTypes::TypeCubeArray )
            {
                imageInfo.arrayLayers = 6u;
                imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            }
            else
            {
                imageInfo.arrayLayers = 1u;
                imageInfo.flags = 0;
            }

            VkResult imageResult =
                vkCreateImage( device->mDevice, &imageInfo, 0, &mBlankTexture[i].vkImage );
            checkVkResult( imageResult, "vkCreateImage" );

            setObjectName( device->mDevice, (uint64_t)mBlankTexture[i].vkImage,
                           VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, dummyNames[i] );

            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements( device->mDevice, mBlankTexture[i].vkImage, &memRequirements );

            VkDeviceMemory deviceMemory = vaoManager->allocateTexture(
                memRequirements, mBlankTexture[i].texMemIdx, mBlankTexture[i].vboPoolIdx,
                mBlankTexture[i].internalBufferStart );

            VkResult result = vkBindImageMemory( device->mDevice, mBlankTexture[i].vkImage, deviceMemory,
                                                 mBlankTexture[i].internalBufferStart );
            checkVkResult( result, "vkBindImageMemory" );
        }

        size_t barrierCount = 0u;
        VkImageMemoryBarrier imageMemBarrier[TextureTypes::Type3D + 1u];
        for( size_t i = 1u; i < TextureTypes::Type3D + 1u; ++i )
        {
            if( c_bSkipAliasable && ( i == TextureTypes::Type1DArray || i == TextureTypes::Type2DArray ||
                                      i == TextureTypes::TypeCubeArray ) )
            {
                continue;
            }

            makeVkStruct( imageMemBarrier[barrierCount], VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER );
            imageMemBarrier[barrierCount].image = mBlankTexture[i].vkImage;
            imageMemBarrier[barrierCount].srcAccessMask = 0;
            imageMemBarrier[barrierCount].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemBarrier[barrierCount].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageMemBarrier[barrierCount].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageMemBarrier[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageMemBarrier[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageMemBarrier[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageMemBarrier[barrierCount].subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
            imageMemBarrier[barrierCount].subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
            ++barrierCount;
        }

        vkCmdPipelineBarrier( device->mGraphicsQueue.mCurrentCmdBuffer,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0u,
                              0, 0u, 0, static_cast<uint32>( barrierCount ), imageMemBarrier );

        for( size_t i = 1u; i < TextureTypes::Type3D + 1u; ++i )
        {
            if( c_bSkipAliasable && ( i == TextureTypes::Type1DArray || i == TextureTypes::Type2DArray ||
                                      i == TextureTypes::TypeCubeArray ) )
            {
                continue;
            }

            size_t bufStart = 0u;
            if( i == TextureTypes::TypeCube || i == TextureTypes::TypeCubeArray )
                bufStart = whiteBox.bytesPerImage * 6u;  // Point at start of black

            VkBufferImageCopy region;
            memset( &region, 0, sizeof( region ) );

            region.bufferOffset = stagingTex->_getInternalBufferStart() + bufStart;
            region.bufferRowLength = static_cast<uint32_t>( whiteBox.bytesPerRow );
            region.bufferImageHeight = static_cast<uint32_t>( whiteBox.bytesPerImage );

            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            if( i == TextureTypes::TypeCube || i == TextureTypes::TypeCubeArray )
                region.imageSubresource.layerCount = 6u;
            else
                region.imageSubresource.layerCount = 1u;

            region.imageExtent.width = 4u;
            if( i == TextureTypes::Type3D )
                region.imageExtent.depth = 4u;
            else
                region.imageExtent.depth = 1u;
            if( i == TextureTypes::Type1D || i == TextureTypes::Type1DArray )
                region.imageExtent.height = 1u;
            else
                region.imageExtent.height = 4u;

            vkCmdCopyBufferToImage( device->mGraphicsQueue.mCurrentCmdBuffer, stagingBuffVboName,
                                    mBlankTexture[i].vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u,
                                    &region );
        }

        barrierCount = 0u;
        for( size_t i = 1u; i < TextureTypes::Type3D + 1u; ++i )
        {
            if( c_bSkipAliasable && ( i == TextureTypes::Type1DArray || i == TextureTypes::Type2DArray ||
                                      i == TextureTypes::TypeCubeArray ) )
            {
                continue;
            }

            imageMemBarrier[barrierCount].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemBarrier[barrierCount].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            imageMemBarrier[barrierCount].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageMemBarrier[barrierCount].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ++barrierCount;
        }

        vkCmdPipelineBarrier( device->mGraphicsQueue.mCurrentCmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0u, 0, 0u, 0,
                              static_cast<uint32>( barrierCount ), imageMemBarrier );

        mBlankTexture[TextureTypes::Unknown] = mBlankTexture[TextureTypes::Type2D];
        if( c_bSkipAliasable )
        {
            mBlankTexture[TextureTypes::Type1DArray] = mBlankTexture[TextureTypes::Type1D];
            mBlankTexture[TextureTypes::Type2DArray] = mBlankTexture[TextureTypes::Type2D];
            mBlankTexture[TextureTypes::TypeCubeArray] = mBlankTexture[TextureTypes::TypeCube];
        }

        mBlankTexture[0].defaultView = 0;

        for( size_t i = 1u; i < TextureTypes::Type3D + 1u; ++i )
        {
            VkImageViewCreateInfo imageViewCi;
            makeVkStruct( imageViewCi, VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO );

            imageViewCi.image = mBlankTexture[i].vkImage;
            imageViewCi.viewType = VulkanMappings::get( static_cast<TextureTypes::TextureTypes>( i ) );
            imageViewCi.format = VK_FORMAT_R8G8B8A8_UNORM;

            imageViewCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageViewCi.subresourceRange.baseMipLevel = 0u;
            imageViewCi.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
            imageViewCi.subresourceRange.baseArrayLayer = 0U;
            imageViewCi.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

            VkResult result =
                vkCreateImageView( device->mDevice, &imageViewCi, 0, &mBlankTexture[i].defaultView );
            checkVkResult( result, "vkCreateImageView" );
        }
    }
    //-----------------------------------------------------------------------------------
    VulkanTextureGpuManager::~VulkanTextureGpuManager()
    {
        destroyAll();

        for( size_t i = 1u; i < TextureTypes::Type3D + 1u; ++i )
        {
            vkDestroyImageView( mDevice->mDevice, mBlankTexture[i].defaultView, 0 );
            mBlankTexture[i].defaultView = 0;

            if( c_bSkipAliasable && ( i == TextureTypes::Type1DArray || i == TextureTypes::Type2DArray ||
                                      i == TextureTypes::TypeCubeArray ) )
            {
                mBlankTexture[i].vkImage = 0;
            }
            else
            {
                vkDestroyImage( mDevice->mDevice, mBlankTexture[i].vkImage, 0 );
                mBlankTexture[i].vkImage = 0;
            }
        }
    }
    //-----------------------------------------------------------------------------------
    TextureGpu *VulkanTextureGpuManager::createTextureGpuWindow( VulkanWindow *window )
    {
        return OGRE_NEW VulkanTextureGpuWindow( GpuPageOutStrategy::Discard, mVaoManager,
                                                "RenderWindow",                      //
                                                TextureFlags::NotTexture |           //
                                                    TextureFlags::RenderToTexture |  //
                                                    TextureFlags::RenderWindowSpecific |
                                                    TextureFlags::RequiresTextureFlipping,
                                                TextureTypes::Type2D, this, window );
    }
    //-----------------------------------------------------------------------------------
    TextureGpu *VulkanTextureGpuManager::createWindowDepthBuffer( void )
    {
        return OGRE_NEW VulkanTextureGpuRenderTarget( GpuPageOutStrategy::Discard, mVaoManager,
                                                      "RenderWindow DepthBuffer",          //
                                                      TextureFlags::NotTexture |           //
                                                          TextureFlags::RenderToTexture |  //
                                                          TextureFlags::RenderWindowSpecific,
                                                      TextureTypes::Type2D, this );
    }
    //-----------------------------------------------------------------------------------
    TextureGpu *VulkanTextureGpuManager::createTextureImpl(
        GpuPageOutStrategy::GpuPageOutStrategy pageOutStrategy, IdString name, uint32 textureFlags,
        TextureTypes::TextureTypes initialType )
    {
        VulkanTextureGpu *retVal = 0;
        if( textureFlags & TextureFlags::RenderToTexture )
        {
            retVal = OGRE_NEW VulkanTextureGpuRenderTarget(
                pageOutStrategy, mVaoManager, name, textureFlags | TextureFlags::RequiresTextureFlipping,
                initialType, this );
        }
        else
        {
            retVal = OGRE_NEW VulkanTextureGpu( pageOutStrategy, mVaoManager, name, textureFlags,
                                                initialType, this );
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    StagingTexture *VulkanTextureGpuManager::createStagingTextureImpl( uint32 width, uint32 height,
                                                                       uint32 depth, uint32 slices,
                                                                       PixelFormatGpu pixelFormat )
    {
        const uint32 rowAlignment = 4u;
        const size_t sizeBytes =
            PixelFormatGpuUtils::getSizeBytes( width, height, depth, slices, pixelFormat, rowAlignment );

        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );
        VulkanStagingTexture *retVal =
            vaoManager->createStagingTexture( PixelFormatGpuUtils::getFamily( pixelFormat ), sizeBytes );
        return retVal;
    }
    //-----------------------------------------------------------------------------------
    void VulkanTextureGpuManager::destroyStagingTextureImpl( StagingTexture *stagingTexture )
    {
        OGRE_ASSERT_HIGH( dynamic_cast<VulkanStagingTexture *>( stagingTexture ) );

        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );
        vaoManager->destroyStagingTexture( static_cast<VulkanStagingTexture *>( stagingTexture ) );
    }
    //-----------------------------------------------------------------------------------
    AsyncTextureTicket *VulkanTextureGpuManager::createAsyncTextureTicketImpl(
        uint32 width, uint32 height, uint32 depthOrSlices, TextureTypes::TextureTypes textureType,
        PixelFormatGpu pixelFormatFamily )
    {
        return OGRE_NEW VulkanAsyncTextureTicket( width, height, depthOrSlices, textureType,
                                                  pixelFormatFamily );
    }
    //-----------------------------------------------------------------------------------
    VkImage VulkanTextureGpuManager::getBlankTextureVulkanName(
        TextureTypes::TextureTypes textureType ) const
    {
        return mBlankTexture[textureType].vkImage;
    }
    //-----------------------------------------------------------------------------------
    VkImageView VulkanTextureGpuManager::getBlankTextureView(
        TextureTypes::TextureTypes textureType ) const
    {
        return mBlankTexture[textureType].defaultView;
    }
}  // namespace Ogre
