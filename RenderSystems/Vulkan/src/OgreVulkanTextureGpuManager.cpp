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
#include "OgreVulkanStagingTexture.h"
#include "OgreVulkanTextureGpu.h"
#include "OgreVulkanTextureGpuWindow.h"

#include "Vao/OgreVulkanVaoManager.h"

#include "OgrePixelFormatGpuUtils.h"
#include "OgreVector2.h"

#include "OgreException.h"

namespace Ogre
{
    VulkanTextureGpuManager::VulkanTextureGpuManager( VaoManager *vaoManager,
                                                      RenderSystem *renderSystem,
                                                      VulkanDevice *device ) :
        TextureGpuManager( vaoManager, renderSystem ),
        mDevice( device )
    {
//         MTLTextureDescriptor *desc = [MTLTextureDescriptor new];
//         desc.mipmapLevelCount = 1u;
//         desc.width = 4u;
//         desc.height = 4u;
//         desc.depth = 1u;
//         desc.arrayLength = 1u;
//         desc.pixelFormat = MTLPixelFormatRGBA8Unorm;
//         desc.sampleCount = 1u;
//         desc.usage = MTLTextureUsageShaderRead;
//
//         const MTLTextureType textureTypes[] = {
//             MTLTextureType2D,
//             MTLTextureType1D,
//             MTLTextureType1DArray,
//             MTLTextureType2D,
//             MTLTextureType2DArray,
//             MTLTextureTypeCube,
// #if OGRE_PLATFORM != OGRE_PLATFORM_APPLE_IOS
//             MTLTextureTypeCubeArray,
// #else
//             MTLTextureTypeCube,
// #endif
//             MTLTextureType3D
//         };
//
//         const char *dummyNames[] = {
//             "Dummy Unknown (2D)",
//             "Dummy 1D 4x1",
//             "Dummy 1DArray 4x1",
//             "Dummy 2D 4x4",
//             "Dummy 2DArray 4x4x1",
//             "Dummy Cube 4x4",
// #if OGRE_PLATFORM != OGRE_PLATFORM_APPLE_IOS
//             "Dummy CubeArray 4x4x1",
// #else
//             "Dummy CubeArray 4x4x1 (Emulated w/ Cube)",
// #endif
//             "Dummy 3D 4x4x4"
//         };
//
//         // Must be large enough to hold the biggest transfer we'll do.
//         uint8 c_whiteData[4 * 4 * 6 * 4];
//         uint8 c_blackData[4 * 4 * 6 * 4];
//         memset( c_whiteData, 0xff, sizeof( c_whiteData ) );
//         memset( c_blackData, 0x00, sizeof( c_blackData ) );
//
//         for( int i = 1; i <= TextureTypes::Type3D; ++i )
//         {
//             if( i == TextureTypes::Type3D )
//                 desc.depth = 4u;
//             else
//                 desc.depth = 1u;
//             if( i == TextureTypes::Type1D || i == TextureTypes::Type1DArray )
//                 desc.height = 1u;
//             else
//                 desc.height = 4u;
//
//             desc.textureType = textureTypes[i];
//             mBlankTexture[i] = [device->mDevice newTextureWithDescriptor:desc];
//             if( !mBlankTexture[i] )
//             {
//                 OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, "Out of GPU memory or driver refused.",
//                              "MetalTextureGpuManager::MetalTextureGpuManager" );
//             }
//             mBlankTexture[i].label = [NSString stringWithUTF8String:dummyNames[i]];
//
//             if( i == TextureTypes::TypeCube || i == TextureTypes::TypeCubeArray )
//             {
//                 for( uint32 j = 0; j < 6u; ++j )
//                 {
//                     [mBlankTexture[i] replaceRegion:MTLRegionMake2D( 0, 0, 4u, 4u )
//                                         mipmapLevel:0
//                                               slice:j
//                                           withBytes:c_blackData
//                                         bytesPerRow:4u * 4u
//                                       bytesPerImage:4u * 4u * 4u];
//                 }
//             }
//             else
//             {
//                 [mBlankTexture[i] replaceRegion:MTLRegionMake3D( 0, 0, 0, 4u, desc.height, desc.depth )
//                                     mipmapLevel:0
//                                           slice:0
//                                       withBytes:c_whiteData
//                                     bytesPerRow:4u * 4u
//                                   bytesPerImage:4u * 4u * 4u];
//             }
//         }
//
//         mBlankTexture[TextureTypes::Unknown] = mBlankTexture[TextureTypes::Type2D];
    }
    //-----------------------------------------------------------------------------------
    VulkanTextureGpuManager::~VulkanTextureGpuManager() { destroyAll(); }
    //-----------------------------------------------------------------------------------
    TextureGpu *VulkanTextureGpuManager::createTextureGpuWindow( VulkanWindow *window )
    {
        return OGRE_NEW VulkanTextureGpuWindow( GpuPageOutStrategy::Discard, mVaoManager,
                                                "RenderWindow",                      //
                                                TextureFlags::NotTexture |           //
                                                    TextureFlags::RenderToTexture |  //
                                                    TextureFlags::RenderWindowSpecific,
                                                TextureTypes::Type2D, this, window );
    }
    //-----------------------------------------------------------------------------------
    TextureGpu *VulkanTextureGpuManager::createWindowDepthBuffer( void )
    {
        return OGRE_NEW VulkanTextureGpuRenderTarget( GpuPageOutStrategy::Discard, mVaoManager,
                                                "RenderWindow DepthBuffer",                      //
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
            retVal = OGRE_NEW VulkanTextureGpuRenderTarget( pageOutStrategy, mVaoManager, name, textureFlags,
                initialType, this );
        }
        else
        {
            retVal = OGRE_NEW VulkanTextureGpu( pageOutStrategy, mVaoManager, name,
                                                textureFlags,
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
        return vaoManager->createStagingTexture( PixelFormatGpuUtils::getFamily( pixelFormat ), sizeBytes );
    }
    //-----------------------------------------------------------------------------------
    void VulkanTextureGpuManager::destroyStagingTextureImpl( StagingTexture *stagingTexture )
    {
        // Do nothing, caller will delete stagingTexture.
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
        return mBlankTexture[textureType];
    }
    //-----------------------------------------------------------------------------------
    VkImageView VulkanTextureGpuManager::getBlankTextureViewVulkanName(
        TextureTypes::TextureTypes textureType ) const
    {
        return mBlankTextureView[textureType];
    }
}  // namespace Ogre
