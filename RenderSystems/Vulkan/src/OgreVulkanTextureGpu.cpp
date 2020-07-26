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

#include "OgreVulkanTextureGpu.h"

#include "OgrePixelFormatGpuUtils.h"

#include "OgreException.h"
#include "OgreVector2.h"
#include "OgreVulkanMappings.h"
#include "OgreVulkanTextureGpuManager.h"
#include "OgreVulkanUtils.h"

namespace Ogre
{
    VulkanTextureGpu::VulkanTextureGpu( GpuPageOutStrategy::GpuPageOutStrategy pageOutStrategy,
                                        VaoManager *vaoManager, IdString name, uint32 textureFlags,
                                        TextureTypes::TextureTypes initialType,
                                        TextureGpuManager *textureManager ) :
        TextureGpu( pageOutStrategy, vaoManager, name, textureFlags, initialType, textureManager ),
        mDisplayTextureName( 0 ),
        mFinalTextureName( 0 ),
        mMsaaFramebufferName( 0 ),
        mCurrLayout( VK_IMAGE_LAYOUT_UNDEFINED ),
        mNextLayout( VK_IMAGE_LAYOUT_UNDEFINED )
    {
    }
    //-----------------------------------------------------------------------------------
    VulkanTextureGpu::~VulkanTextureGpu() { destroyInternalResourcesImpl(); }
    //-----------------------------------------------------------------------------------
    void VulkanTextureGpu::createInternalResourcesImpl( void )
    {
        if( mPixelFormat == PFG_NULL )
            return;  // Nothing to do

        VkImageCreateInfo imageInfo;
        makeVkStruct( imageInfo, VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO );
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = getVulkanTextureType();
        imageInfo.extent.width = mWidth;
        imageInfo.extent.height = mHeight;
        imageInfo.extent.depth = getDepth();
        imageInfo.mipLevels = mNumMipmaps;
        imageInfo.arrayLayers = getNumSlices();
        imageInfo.format = VulkanMappings::get( mPixelFormat );
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if( hasMsaaExplicitResolves() )
            imageInfo.samples = static_cast<VkSampleCountFlagBits>( mMsaa );
        else
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.flags = 0;

        if( mTextureType == TextureTypes::TypeCube )
            imageInfo.arrayLayers /= 6u;

        if( isRenderToTexture() )
        {
            imageInfo.usage |= PixelFormatGpuUtils::isDepth( mPixelFormat )
                                   ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                   : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        if( isUav() )
        {
            imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }

        String textureName = getNameStr();

        VulkanTextureGpuManager *textureManager =
            static_cast<VulkanTextureGpuManager *>( mTextureManager );
        VulkanDevice *device = textureManager->getDevice();

        VkResult imageResult = vkCreateImage( device->mDevice, &imageInfo, 0, &mFinalTextureName );
        checkVkResult( imageResult, "createInternalResourcesImpl" );

        setObjectName( device->mDevice, (uint64_t)mFinalTextureName,
                       VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, textureName.c_str() );

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements( device->mDevice, mFinalTextureName, &memRequirements );

        VkMemoryAllocateInfo allocInfo;
        makeVkStruct( allocInfo, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO );
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex =
            findMemoryType( device->mPhysicalDevice, device->mDeviceMemoryProperties,
                            memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

        if( vkAllocateMemory( device->mDevice, &allocInfo, 0, &mTextureImageMemory ) != VK_SUCCESS )
        {
            OGRE_EXCEPT( Exception::ERR_INVALID_STATE, "Could not allocate memory",
                         "VulkanTextureGpu::createInternalResourcesImpl" );
        }

        // TODO use one large buffer and multiple offsets
        VkDeviceSize offset = 0;
        offset = alignMemory( offset, memRequirements.alignment );

        if( vkBindImageMemory( device->mDevice, mFinalTextureName, mTextureImageMemory, offset ) !=
            VK_SUCCESS )
        {
            OGRE_EXCEPT( Exception::ERR_INVALID_STATE, "Could not allocate bind image to memory",
                         "VulkanTextureGpu::createInternalResourcesImpl" );
        }

        if( mMsaa > 1u && !hasMsaaExplicitResolves() )
        {
        }
    }
    //-----------------------------------------------------------------------------------
    void VulkanTextureGpu::destroyInternalResourcesImpl( void )
    {
        VulkanTextureGpuManager *textureManager =
            static_cast<VulkanTextureGpuManager *>( mTextureManager );
        VulkanDevice *device = textureManager->getDevice();
        if( mFinalTextureName )
        {
            vkDestroyImage( device->mDevice, mFinalTextureName, 0 );
        }

        _setToDisplayDummyTexture();
    }
    //-----------------------------------------------------------------------------------
    VkImageSubresourceRange VulkanTextureGpu::getFullSubresourceRange( void ) const
    {
        VkImageSubresourceRange retVal;
        retVal.aspectMask = VulkanMappings::getImageAspect( mPixelFormat );
        retVal.baseMipLevel = 0u;
        retVal.levelCount = VK_REMAINING_MIP_LEVELS;
        retVal.baseArrayLayer = 0u;
        retVal.layerCount = VK_REMAINING_ARRAY_LAYERS;
        return retVal;
    }
    //-----------------------------------------------------------------------------------
    void VulkanTextureGpu::getSubsampleLocations( vector<Vector2>::type locations )
    {
        uint8 msaaCount = mMsaa;
        locations.reserve( msaaCount );
        for( size_t i = 0; i < msaaCount; ++i )
            locations.push_back( Vector2( 0, 0 ) );
    }
    //-----------------------------------------------------------------------------------
    void VulkanTextureGpu::notifyDataIsReady( void )
    {
        assert( mResidencyStatus == GpuResidency::Resident );
        assert( mFinalTextureName || mPixelFormat == PFG_NULL );

        VulkanTextureGpuManager *textureManager =
            static_cast<VulkanTextureGpuManager *>( mTextureManager );
        VulkanDevice *device = textureManager->getDevice();

        mDisplayTextureName = getView();

        notifyAllListenersTextureChanged( TextureGpuListener::ReadyForRendering );
    }
    //-----------------------------------------------------------------------------------
    void VulkanTextureGpu::setTextureType( TextureTypes::TextureTypes textureType ) {}
    //-----------------------------------------------------------------------------------
    void VulkanTextureGpu::copyTo( TextureGpu *dst, const TextureBox &dstBox, uint8 dstMipLevel,
                                   const TextureBox &srcBox, uint8 srcMipLevel )
    {
    }
    //-----------------------------------------------------------------------------------
    void VulkanTextureGpu::_autogenerateMipmaps( void ) {}
    //-----------------------------------------------------------------------------------
    void VulkanTextureGpu::_setToDisplayDummyTexture( void )
    {
        if( !mTextureManager )
        {
            assert( isRenderWindowSpecific() );
            return;  // This can happen if we're a window and we're on shutdown
        }

        VulkanTextureGpuManager *textureManagerVulkan =
            static_cast<VulkanTextureGpuManager *>( mTextureManager );
        if( hasAutomaticBatching() )
        {
            mDisplayTextureName =
                textureManagerVulkan->getBlankTextureViewVulkanName( TextureTypes::Type2DArray );
        }
        else
        {
            mDisplayTextureName = textureManagerVulkan->getBlankTextureViewVulkanName( mTextureType );
        }
    }
    //-----------------------------------------------------------------------------------
    bool VulkanTextureGpu::_isDataReadyImpl( void ) const { return mDisplayTextureName != 0; }
    //-----------------------------------------------------------------------------------
    VkImageType VulkanTextureGpu::getVulkanTextureType( void ) const
    {
        // clang-format off
        switch( mTextureType )
        {
        case TextureTypes::Unknown:         return VK_IMAGE_TYPE_2D;
        case TextureTypes::Type1D:          return VK_IMAGE_TYPE_1D;
        case TextureTypes::Type1DArray:     return VK_IMAGE_TYPE_1D;
        case TextureTypes::Type2D:          return VK_IMAGE_TYPE_2D;
        case TextureTypes::Type2DArray:     return VK_IMAGE_TYPE_2D;
        case TextureTypes::TypeCube:        return VK_IMAGE_TYPE_3D;
        case TextureTypes::TypeCubeArray:   return VK_IMAGE_TYPE_3D;
        case TextureTypes::Type3D:          return VK_IMAGE_TYPE_3D;
        }
        // clang-format on

        return VK_IMAGE_TYPE_2D;
    }
    //-----------------------------------------------------------------------------------
    VkImageViewType VulkanTextureGpu::getVulkanTextureViewType( void ) const
    {
        // clang-format off
        switch( mTextureType )
        {
        case TextureTypes::Unknown:         return VK_IMAGE_VIEW_TYPE_2D;
        case TextureTypes::Type1D:          return VK_IMAGE_VIEW_TYPE_1D;
        case TextureTypes::Type1DArray:     return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        case TextureTypes::Type2D:          return VK_IMAGE_VIEW_TYPE_2D;
        case TextureTypes::Type2DArray:     return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case TextureTypes::TypeCube:        return VK_IMAGE_VIEW_TYPE_CUBE;
        case TextureTypes::TypeCubeArray:   return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        case TextureTypes::Type3D:          return VK_IMAGE_VIEW_TYPE_3D;
        }
        // clang-format on

        return VK_IMAGE_VIEW_TYPE_2D;
    }
    //-----------------------------------------------------------------------------------
    VkImageView VulkanTextureGpu::getView( PixelFormatGpu pixelFormat, uint8 mipLevel, uint8 numMipmaps,
                                           uint16 arraySlice, bool cubemapsAs2DArrays, bool forUav )
    {
        if( pixelFormat == PFG_UNKNOWN )
        {
            pixelFormat = mPixelFormat;
            if( forUav )
                pixelFormat = PixelFormatGpuUtils::getEquivalentLinear( pixelFormat );
        }
        VkImageViewType texType = this->getVulkanTextureViewType();

        if( mMsaa > 1u && hasMsaaExplicitResolves() )
            texType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        // MTLTextureType2DMultisample;

        if( cubemapsAs2DArrays &&
            ( mTextureType == TextureTypes::TypeCube || mTextureType == TextureTypes::TypeCubeArray ) )
        {
            texType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        }

        if( !numMipmaps )
            numMipmaps = mNumMipmaps - mipLevel;

        OGRE_ASSERT_LOW( numMipmaps <= mNumMipmaps - mipLevel &&
                         "Asking for more mipmaps than the texture has!" );

        VulkanTextureGpuManager *textureManager =
            static_cast<VulkanTextureGpuManager *>( mTextureManager );
        VulkanDevice *device = textureManager->getDevice();

        VkImageViewCreateInfo imageViewCi;
        makeVkStruct( imageViewCi, VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO );
        imageViewCi.image = mFinalTextureName;
        imageViewCi.viewType = texType;
        imageViewCi.format = VulkanMappings::get( pixelFormat );

        imageViewCi.subresourceRange.aspectMask = VulkanMappings::getImageAspect( pixelFormat );
        imageViewCi.subresourceRange.baseMipLevel = mipLevel;
        imageViewCi.subresourceRange.levelCount = numMipmaps;
        imageViewCi.subresourceRange.baseArrayLayer = arraySlice;
        imageViewCi.subresourceRange.layerCount = this->getNumSlices() - arraySlice;

        VkImageView imageView;
        VkResult result = vkCreateImageView( device->mDevice, &imageViewCi, 0, &imageView );
        checkVkResult( result, "VulkanTextureGpu::getView" );

        return imageView;
    }
    //-----------------------------------------------------------------------------------
    VkImageView VulkanTextureGpu::getView( DescriptorSetTexture2::TextureSlot texSlot )
    {
        return getView( texSlot.pixelFormat, texSlot.mipmapLevel, texSlot.numMipmaps,
                        texSlot.textureArrayIndex, texSlot.cubemapsAs2DArrays, false );
    }
    //-----------------------------------------------------------------------------------
    VkImageView VulkanTextureGpu::getView( DescriptorSetUav::TextureSlot texSlot )
    {
        return getView( texSlot.pixelFormat, texSlot.mipmapLevel, 1u, texSlot.textureArrayIndex, false,
                        true );
    }
    //-----------------------------------------------------------------------------------
    VkImageView VulkanTextureGpu::getView( void )
    {
        return getView( mPixelFormat, 0, 1, 0, false, false );
    }
    //-----------------------------------------------------------------------------------
    void VulkanTextureGpu::destroyView( VkImageView imageView )
    {
        VulkanTextureGpuManager *textureManager =
            static_cast<VulkanTextureGpuManager *>( mTextureManager );
        VulkanDevice *device = textureManager->getDevice();
        vkDestroyImageView( device->mDevice, imageView, 0 );
    }
    //-----------------------------------------------------------------------------------
    VkImageMemoryBarrier VulkanTextureGpu::getImageMemoryBarrier( void ) const
    {
        VkImageMemoryBarrier imageMemBarrier;
        makeVkStruct( imageMemBarrier, VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER );
        imageMemBarrier.image = mFinalTextureName;
        imageMemBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemBarrier.subresourceRange.aspectMask = VulkanMappings::getImageAspect( mPixelFormat );
        imageMemBarrier.subresourceRange.baseMipLevel = 0u;
        imageMemBarrier.subresourceRange.levelCount = mNumMipmaps;
        imageMemBarrier.subresourceRange.baseArrayLayer = mInternalSliceStart;
        imageMemBarrier.subresourceRange.layerCount = getNumSlices();
        return imageMemBarrier;
    }
    //-----------------------------------------------------------------------------------
    VulkanTextureGpuRenderTarget::VulkanTextureGpuRenderTarget(
        GpuPageOutStrategy::GpuPageOutStrategy pageOutStrategy, VaoManager *vaoManager, IdString name,
        uint32 textureFlags, TextureTypes::TextureTypes initialType,
        TextureGpuManager *textureManager ) :
        VulkanTextureGpu( pageOutStrategy, vaoManager, name, textureFlags, initialType, textureManager ),
        mDepthBufferPoolId( 1u ),
        mPreferDepthTexture( false ),
        mDesiredDepthBufferFormat( PFG_UNKNOWN )
    {
    }
    //-----------------------------------------------------------------------------------
    void VulkanTextureGpuRenderTarget::_setDepthBufferDefaults( uint16 depthBufferPoolId,
                                                                bool preferDepthTexture,
                                                                PixelFormatGpu desiredDepthBufferFormat )
    {
        assert( isRenderToTexture() );
        mDepthBufferPoolId = depthBufferPoolId;
        mPreferDepthTexture = preferDepthTexture;
        mDesiredDepthBufferFormat = desiredDepthBufferFormat;
    }
    //-----------------------------------------------------------------------------------
    uint16 VulkanTextureGpuRenderTarget::getDepthBufferPoolId( void ) const
    {
        return mDepthBufferPoolId;
    }
    //-----------------------------------------------------------------------------------
    bool VulkanTextureGpuRenderTarget::getPreferDepthTexture( void ) const
    {
        return mPreferDepthTexture;
    }
    //-----------------------------------------------------------------------------------
    PixelFormatGpu VulkanTextureGpuRenderTarget::getDesiredDepthBufferFormat( void ) const
    {
        return mDesiredDepthBufferFormat;
    }
}  // namespace Ogre
