/*
  -----------------------------------------------------------------------------
  This source file is part of OGRE
  (Object-oriented Graphics Rendering Engine)
  For the latest info, see http://www.ogre3d.org

Copyright (c) 2000-2014 Torus Knot Software Ltd

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

#include "OgreVulkanRenderSystem.h"

#include "OgreRenderPassDescriptor.h"
#include "OgreVulkanCache.h"
#include "OgreVulkanDescriptors.h"
#include "OgreVulkanDevice.h"
#include "OgreVulkanGpuProgramManager.h"
#include "OgreVulkanMappings.h"
#include "OgreVulkanProgramFactory.h"
#include "OgreVulkanRenderPassDescriptor.h"
#include "OgreVulkanTextureGpuManager.h"
#include "OgreVulkanUtils.h"
#include "OgreVulkanWindow.h"
#include "Vao/OgreVulkanVaoManager.h"

#include "Vao/OgreVulkanVertexArrayObject.h"
#include "OgreDefaultHardwareBufferManager.h"

#include "CommandBuffer/OgreCbDrawCall.h"

#include "OgreVulkanHlmsPso.h"
#include "Vao/OgreVulkanConstBufferPacked.h"
#include "Vao/OgreIndirectBufferPacked.h"
#include "Vao/OgreVulkanBufferInterface.h"

//#include "Windowing/X11/OgreVulkanXcbWindow.h"

#include "OgreVulkanDescriptorPool.h"
#include "OgreVulkanDescriptorSet.h"
#include "OgreVulkanHardwareIndexBuffer.h"
#include "OgreVulkanHardwareVertexBuffer.h"
#include "OgreVulkanUtils2.h"
#include "Windowing/win32/OgreVulkanWin32Window.h"

#define TODO_check_layers_exist

#define TODO_vertex_format
#define TODO_addVpCount_to_passpso

namespace Ogre
{
    /*static bool checkLayers( const FastArray<layer_properties> &layer_props,
                             const FastArray<const char *> &layer_names )
    {
        uint32_t check_count = layer_names.size();
        uint32_t layer_count = layer_props.size();
        for( uint32_t i = 0; i < check_count; i++ )
        {
            VkBool32 found = 0;
            for( uint32_t j = 0; j < layer_count; j++ )
            {
                if( !strcmp( layer_names[i], layer_props[j].properties.layerName ) )
                {
                    found = 1;
                }
            }
            if( !found )
            {
                std::cout << "Cannot find layer: " << layer_names[i] << std::endl;
                return 0;
            }
        }
        return 1;
    }*/
    VKAPI_ATTR VkBool32 VKAPI_CALL dbgFunc( VkFlags msgFlags, VkDebugReportObjectTypeEXT objType,
                                            uint64_t srcObject, size_t location, int32_t msgCode,
                                            const char *pLayerPrefix, const char *pMsg, void *pUserData )
    {
        // clang-format off
        char *message = (char *)malloc(strlen(pMsg) + 100);

        assert(message);

        if (msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
            sprintf(message, "INFORMATION: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        } else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
            sprintf(message, "WARNING: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        } else if (msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
            sprintf(message, "PERFORMANCE WARNING: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        } else if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
            sprintf(message, "ERROR: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        } else if (msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
            sprintf(message, "DEBUG: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        } else {
            sprintf(message, "INFORMATION: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        }

        LogManager::getSingleton().logMessage( message );

        free(message);

        // clang-format on

        /*
         * false indicates that layer should not bail-out of an
         * API call that had validation failures. This may mean that the
         * app dies inside the driver due to invalid parameter(s).
         * That's what would happen without validation layers, so we'll
         * keep that behavior here.
         */
        // return false;
        return true;
    }

    //-------------------------------------------------------------------------
    VulkanRenderSystem::VulkanRenderSystem() :
        RenderSystem(),
        mInitialized( false ),
        mHardwareBufferManager( 0 ),
        mShaderManager( 0 ),
        mVulkanProgramFactory( 0 ),
        mVkInstance( 0 ),
        mAutoParamsBufferIdx( 0 ),
        mCurrentAutoParamsBufferPtr( 0 ),
        mCurrentAutoParamsBufferSpaceLeft( 0 ),
        mActiveDevice( 0 ),
        mDevice( 0 ),
        mCache( 0 ),
        mPso( 0 ),
        mEntriesToFlush( 0u ),
        mVpChanged( false ),
        CreateDebugReportCallback( 0 ),
        DestroyDebugReportCallback( 0 ),
        mDebugReportCallback( 0 )
    {
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::shutdown( void )
    {
        OGRE_DELETE mCache;
        mCache = 0;

        OGRE_DELETE mShaderManager;
        mShaderManager = 0;

        OGRE_DELETE mVulkanProgramFactory;
        mVulkanProgramFactory = 0;

        OGRE_DELETE mHardwareBufferManager;
        mHardwareBufferManager = 0;

        delete mDevice;
        mDevice = 0;

        if( mDebugReportCallback )
        {
            DestroyDebugReportCallback( mVkInstance, mDebugReportCallback, 0 );
            mDebugReportCallback = 0;
        }

        if( mVkInstance )
        {
            vkDestroyInstance( mVkInstance, 0 );
            mVkInstance = 0;
        }
    }
    //-------------------------------------------------------------------------
    const String &VulkanRenderSystem::getName( void ) const
    {
        static String strName( "Vulkan Rendering Subsystem" );
        return strName;
    }
    //-------------------------------------------------------------------------
    const String &VulkanRenderSystem::getFriendlyName( void ) const
    {
        static String strName( "Vulkan_RS" );
        return strName;
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::addInstanceDebugCallback( void )
    {
        CreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(
            mVkInstance, "vkCreateDebugReportCallbackEXT" );
        DestroyDebugReportCallback = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(
            mVkInstance, "vkDestroyDebugReportCallbackEXT" );
        if( !CreateDebugReportCallback )
        {
            LogManager::getSingleton().logMessage(
                "[Vulkan] GetProcAddr: Unable to find vkCreateDebugReportCallbackEXT. "
                "Debug reporting won't be available" );
            return;
        }
        if( !DestroyDebugReportCallback )
        {
            LogManager::getSingleton().logMessage(
                "[Vulkan] GetProcAddr: Unable to find vkDestroyDebugReportCallbackEXT. "
                "Debug reporting won't be available" );
            return;
        }
        // DebugReportMessage =
        //    (PFN_vkDebugReportMessageEXT)vkGetInstanceProcAddr( mVkInstance, "vkDebugReportMessageEXT"
        //    );
        // if( !DebugReportMessage )
        //{
        //    LogManager::getSingleton().logMessage(
        //        "[Vulkan] GetProcAddr: Unable to find DebugReportMessage. "
        //        "Debug reporting won't be available" );
        //}

        VkDebugReportCallbackCreateInfoEXT dbgCreateInfo;
        PFN_vkDebugReportCallbackEXT callback;
        makeVkStruct( dbgCreateInfo, VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT );
        callback = dbgFunc;
        dbgCreateInfo.pfnCallback = callback;
        dbgCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
                              VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        VkResult result =
            CreateDebugReportCallback( mVkInstance, &dbgCreateInfo, 0, &mDebugReportCallback );
        switch( result )
        {
        case VK_SUCCESS:
            break;
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            OGRE_VK_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, result,
                            "CreateDebugReportCallback: out of host memory",
                            "VulkanDevice::addInstanceDebugCallback" );
        default:
            OGRE_VK_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, result, "vkCreateDebugReportCallbackEXT",
                            "VulkanDevice::addInstanceDebugCallback" );
        }
    }
    //-------------------------------------------------------------------------
    HardwareOcclusionQuery *VulkanRenderSystem::createHardwareOcclusionQuery( void )
    {
        return 0;  // TODO
    }
    //-------------------------------------------------------------------------
    RenderSystemCapabilities *VulkanRenderSystem::createRenderSystemCapabilities( void ) const
    {
        RenderSystemCapabilities *rsc = new RenderSystemCapabilities();
        rsc->setRenderSystemName( getName() );

        // We would like to save the device properties for the device capabilities limits.
        // These limits are needed for buffers' binding alignments.
        VkPhysicalDeviceProperties *vkProperties = const_cast<VkPhysicalDeviceProperties *>( &mActiveDevice->mDeviceProperties );
        vkGetPhysicalDeviceProperties( mActiveDevice->mPhysicalDevice, vkProperties );

        VkPhysicalDeviceProperties &properties = mActiveDevice->mDeviceProperties;

        rsc->setDeviceName( properties.deviceName );

        switch( properties.vendorID )
        {
        case 0x10DE:
        {
            rsc->setVendor( GPU_NVIDIA );
            // 10 bits = major version (up to r1023)
            // 8 bits = minor version (up to 255)
            // 8 bits = secondary branch version/build version (up to 255)
            // 6 bits = tertiary branch/build version (up to 63)

            DriverVersion driverVersion;
            driverVersion.major = ( properties.driverVersion >> 22u ) & 0x3ff;
            driverVersion.minor = ( properties.driverVersion >> 14u ) & 0x0ff;
            driverVersion.release = ( properties.driverVersion >> 6u ) & 0x0ff;
            driverVersion.build = ( properties.driverVersion ) & 0x003f;
            rsc->setDriverVersion( driverVersion );
        }
        break;
        case 0x1002:
            rsc->setVendor( GPU_AMD );
            break;
        case 0x8086:
            rsc->setVendor( GPU_INTEL );
            break;
        case 0x13B5:
            rsc->setVendor( GPU_ARM );  // Mali
            break;
        case 0x5143:
            rsc->setVendor( GPU_QUALCOMM );
            break;
        case 0x1010:
            rsc->setVendor( GPU_IMGTEC );  // PowerVR
            break;
        }

        if( rsc->getVendor() != GPU_NVIDIA )
        {
            // Generic version routine that matches SaschaWillems's VulkanCapsViewer
            DriverVersion driverVersion;
            driverVersion.major = ( properties.driverVersion >> 22u ) & 0x3ff;
            driverVersion.minor = ( properties.driverVersion >> 12u ) & 0x3ff;
            driverVersion.release = ( properties.driverVersion ) & 0xfff;
            driverVersion.build = 0;
            rsc->setDriverVersion( driverVersion );
        }

        rsc->setCapability( RSC_HWSTENCIL );
        rsc->setStencilBufferBitDepth( 8 );
        rsc->setNumTextureUnits( 16 );
        rsc->setCapability( RSC_ANISOTROPY );
        rsc->setCapability( RSC_AUTOMIPMAP );
        rsc->setCapability( RSC_BLENDING );
        rsc->setCapability( RSC_DOT3 );
        rsc->setCapability( RSC_CUBEMAPPING );
        rsc->setCapability( RSC_TEXTURE_COMPRESSION );
        rsc->setCapability( RSC_TEXTURE_COMPRESSION_DXT );
        rsc->setCapability( RSC_VBO );
        rsc->setCapability( RSC_TWO_SIDED_STENCIL );
        rsc->setCapability( RSC_STENCIL_WRAP );
        rsc->setCapability( RSC_USER_CLIP_PLANES );
        rsc->setCapability( RSC_VERTEX_FORMAT_UBYTE4 );
        rsc->setCapability( RSC_INFINITE_FAR_PLANE );
        rsc->setCapability( RSC_TEXTURE_3D );
        rsc->setCapability( RSC_NON_POWER_OF_2_TEXTURES );
        rsc->setNonPOW2TexturesLimited( false );
        rsc->setCapability( RSC_HWRENDER_TO_TEXTURE );
        rsc->setCapability( RSC_TEXTURE_FLOAT );
        rsc->setCapability( RSC_POINT_SPRITES );
        rsc->setCapability( RSC_POINT_EXTENDED_PARAMETERS );
        rsc->setCapability( RSC_TEXTURE_2D_ARRAY );
        rsc->setCapability( RSC_CONST_BUFFER_SLOTS_IN_SHADER );
        rsc->setMaxPointSize( 256 );

        rsc->setMaximumResolutions( 16384, 4096, 16384 );

        rsc->setVertexProgramConstantFloatCount( 256u );
        rsc->setVertexProgramConstantIntCount( 256u );
        rsc->setVertexProgramConstantBoolCount( 256u );
        rsc->setGeometryProgramConstantFloatCount( 256u );
        rsc->setGeometryProgramConstantIntCount( 256u );
        rsc->setGeometryProgramConstantBoolCount( 256u );
        rsc->setFragmentProgramConstantFloatCount( 256u );
        rsc->setFragmentProgramConstantIntCount( 256u );
        rsc->setFragmentProgramConstantBoolCount( 256u );
        rsc->setTessellationHullProgramConstantFloatCount( 256u );
        rsc->setTessellationHullProgramConstantIntCount( 256u );
        rsc->setTessellationHullProgramConstantBoolCount( 256u );
        rsc->setTessellationDomainProgramConstantFloatCount( 256u );
        rsc->setTessellationDomainProgramConstantIntCount( 256u );
        rsc->setTessellationDomainProgramConstantBoolCount( 256u );
        rsc->setComputeProgramConstantFloatCount( 256u );
        rsc->setComputeProgramConstantIntCount( 256u );
        rsc->setComputeProgramConstantBoolCount( 256u );

        rsc->addShaderProfile( "glsl-vulkan" );

        return rsc;
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::reinitialise( void )
    {
        this->shutdown();
        this->_initialise( true );
    }
    //-------------------------------------------------------------------------
    Window *VulkanRenderSystem::_initialise( bool autoCreateWindow, const String &windowTitle )
    {
        Window *autoWindow = 0;
        if( autoCreateWindow )
            autoWindow = _createRenderWindow( windowTitle, 1280u, 720u, false );
        RenderSystem::_initialise( autoCreateWindow, windowTitle );

        return autoWindow;
    }
    //-------------------------------------------------------------------------
    Window *VulkanRenderSystem::_createRenderWindow( const String &name, uint32 width, uint32 height,
                                                     bool fullScreen,
                                                     const NameValuePairList *miscParams )
    {
        FastArray<const char *> reqInstanceExtensions;
        VulkanWindow *win =
            OGRE_NEW OgreVulkanWin32Window( reqInstanceExtensions, name, width, height, fullScreen );
        mWindows.insert( win );

        if( !mInitialized )
        {
            if( miscParams )
            {
                NameValuePairList::const_iterator itOption = miscParams->find( "reverse_depth" );
                if( itOption != miscParams->end() )
                    mReverseDepth = StringConverter::parseBool( itOption->second, true );
            }

            TODO_check_layers_exist;
            FastArray<const char *> instanceLayers;
#if OGRE_DEBUG_MODE >= OGRE_DEBUG_HIGH
            instanceLayers.push_back( "VK_LAYER_KHRONOS_validation" );
            reqInstanceExtensions.push_back( VK_EXT_DEBUG_REPORT_EXTENSION_NAME );
#endif
            const uint8 dynBufferMultiplier = 3u;

            mVkInstance =
                VulkanDevice::createInstance( name, reqInstanceExtensions, instanceLayers, dbgFunc );
#if OGRE_DEBUG_MODE >= OGRE_DEBUG_HIGH
            addInstanceDebugCallback();
#endif
            mDevice = new VulkanDevice( mVkInstance, 0u, this );
            mActiveDevice = mDevice;

            mRealCapabilities = createRenderSystemCapabilities();
            mCurrentCapabilities = mRealCapabilities;

            initialiseFromRenderSystemCapabilities( mCurrentCapabilities, 0 );

            mNativeShadingLanguageVersion = 450;

            FastArray<const char *> deviceExtensions;
            mDevice->createDevice( deviceExtensions, 0u, 0u );

            mHardwareBufferManager = new v1::DefaultHardwareBufferManager();
            VulkanVaoManager *vaoManager = OGRE_NEW VulkanVaoManager( dynBufferMultiplier, mDevice );
            mVaoManager = vaoManager;
            mTextureGpuManager = OGRE_NEW VulkanTextureGpuManager( mVaoManager, this );

            mActiveDevice->mVaoManager = vaoManager;
            mActiveDevice->initQueues();
            vaoManager->initDrawIdVertexBuffer();
            mInitialized = true;
        }

        win->_setDevice( mActiveDevice );
        win->_initialize( mTextureGpuManager );

        return win;
    }
    //-------------------------------------------------------------------------
    String VulkanRenderSystem::getErrorDescription( long errorNumber ) const { return BLANKSTRING; }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_useLights( const LightList &lights, unsigned short limit ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setWorldMatrix( const Matrix4 &m ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setViewMatrix( const Matrix4 &m ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setProjectionMatrix( const Matrix4 &m ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setSurfaceParams( const ColourValue &ambient, const ColourValue &diffuse,
                                                const ColourValue &specular, const ColourValue &emissive,
                                                Real shininess, TrackVertexColourType tracking )
    {
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setPointSpritesEnabled( bool enabled ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setPointParameters( Real size, bool attenuationEnabled, Real constant,
                                                  Real linear, Real quadratic, Real minSize,
                                                  Real maxSize )
    {
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::flushUAVs( void )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " flushUAVs " ) );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setCurrentDeviceFromTexture( TextureGpu *texture ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setTexture( size_t unit, TextureGpu *texPtr )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " _setTexture " ) );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setTextures( uint32 slotStart, const DescriptorSetTexture *set,
                                           uint32 hazardousTexIdx )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " _setTextures DescriptorSetTexture " ) );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setTextures( uint32 slotStart, const DescriptorSetTexture2 *set )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " _setTextures DescriptorSetTexture2 " ) );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setSamplers( uint32 slotStart, const DescriptorSetSampler *set )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " _setSamplers " ) );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setTexturesCS( uint32 slotStart, const DescriptorSetTexture *set ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setTexturesCS( uint32 slotStart, const DescriptorSetTexture2 *set ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setSamplersCS( uint32 slotStart, const DescriptorSetSampler *set ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setUavCS( uint32 slotStart, const DescriptorSetUav *set ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setTextureCoordCalculation( size_t unit, TexCoordCalcMethod m,
                                                          const Frustum *frustum )
    {
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setTextureBlendMode( size_t unit, const LayerBlendModeEx &bm ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setTextureMatrix( size_t unit, const Matrix4 &xform ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setIndirectBuffer( IndirectBufferPacked *indirectBuffer )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " * _setIndirectBuffer: " ) +
                                    std::to_string( indirectBuffer->getBufferPackedType() ) );
        }
        if( mVaoManager->supportsIndirectBuffers() )
        {
            if( indirectBuffer )
            {
                VulkanBufferInterface *bufferInterface =
                    static_cast<VulkanBufferInterface *>( indirectBuffer->getBufferInterface() );
                mIndirectBuffer = bufferInterface->getVboName();
            }
            else
            {
                mIndirectBuffer = 0;
            }
        }
        else
        {
            if( indirectBuffer )
                mSwIndirectBufferPtr = indirectBuffer->getSwBufferPtr();
            else
                mSwIndirectBufferPtr = 0;
        }
    }
    //-------------------------------------------------------------------------
    RenderPassDescriptor *VulkanRenderSystem::createRenderPassDescriptor( void )
    {
        VulkanRenderPassDescriptor *retVal =
            OGRE_NEW VulkanRenderPassDescriptor( &mDevice->mGraphicsQueue, this );
        mRenderPassDescs.insert( retVal );
        return retVal;
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_beginFrame( void ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_endFrame( void ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_endFrameOnce( void )
    {
        RenderSystem::_endFrameOnce();
        endRenderPassDescriptor();
        mActiveDevice->commitAndNextCommandBuffer( true );
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setHlmsSamplerblock( uint8 texUnit, const HlmsSamplerblock *Samplerblock )
    {
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setPipelineStateObject( const HlmsPso *pso )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( " * _setPipelineStateObject: pso " );
        }

        VkCommandBuffer cmdBuffer = mActiveDevice->mGraphicsQueue.mCurrentCmdBuffer;
        assert( pso->rsData );
        VulkanHlmsPso *vulkanPso = reinterpret_cast<VulkanHlmsPso *>( pso->rsData );
        vkCmdBindPipeline( cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           vulkanPso->pso );
        mPso = vulkanPso;
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setComputePso( const HlmsComputePso *pso ) {}
    //-------------------------------------------------------------------------
    VertexElementType VulkanRenderSystem::getColourVertexElementType( void ) const
    {
        return VET_COLOUR_ARGB;
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_dispatch( const HlmsComputePso &pso )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( " * _dispatch: pso " );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setVertexArrayObject( const VertexArrayObject *vao )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " * _setVertexArrayObject: vaoName " ) +
                                    std::to_string( vao->getVaoName() ) );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::flushDescriptorState( VkPipelineBindPoint pipeline_bind_point,
                                                   const VulkanConstBufferPacked &constBuffer, 
                                                   const size_t bindOffset, const size_t bytesToWrite,
        const unordered_map<unsigned, VulkanConstantDefinitionBindingParam>::type &shaderBindings )
    {
        VulkanHlmsPso *pso = mPso;

        // std::unordered_set<uint32_t> update_descriptor_sets;
        //
        // DescriptorSetLayoutArray::iterator itor = pso->descriptorSets.begin();
        // DescriptorSetLayoutArray::iterator end = pso->descriptorSets.end();
        //
        // while( itor != end )
        // {
        //     VkDescriptorSetLayout &descSet = *itor;
        //
        //     update_descriptor_sets.emplace( descSet );
        //     ++itor;
        // }
        //
        // if( update_descriptor_sets.empty() )
        //     return;

        const VulkanBufferInterface *bufferInterface = static_cast<const VulkanBufferInterface *>(constBuffer.getBufferInterface());

        BindingMap<VkDescriptorBufferInfo> buffer_infos;
        BindingMap<VkDescriptorImageInfo> image_infos;

        DescriptorSetLayoutBindingArray::const_iterator bindingArraySetItor = pso->descriptorLayoutBindingSets.begin();
        DescriptorSetLayoutBindingArray::const_iterator bindingArraySetEnd = pso->descriptorLayoutBindingSets.end();

        // uint32 set = 0;

        // size_t currentOffset = bindOffset;

        while( bindingArraySetItor != bindingArraySetEnd )
        {
            const FastArray<struct VkDescriptorSetLayoutBinding> bindings = *bindingArraySetItor;

            FastArray<struct VkDescriptorSetLayoutBinding>::const_iterator bindingsItor = bindings.begin();
            FastArray<struct VkDescriptorSetLayoutBinding>::const_iterator bindingsItorEnd =
                bindings.end();

            // uint32 arrayElement = 0;

            while( bindingsItor != bindingsItorEnd )
            {
                const VkDescriptorSetLayoutBinding &binding = *bindingsItor;

                if( is_buffer_descriptor_type(binding.descriptorType) )
                {

                    VkDescriptorBufferInfo buffer_info;

                    VulkanConstantDefinitionBindingParam bindingParam;
                    unordered_map<unsigned, VulkanConstantDefinitionBindingParam>::type::const_iterator constantDefinitionBinding = 
                        shaderBindings.find( binding.binding );
                    if( constantDefinitionBinding == shaderBindings.end() )
                    {
                        ++bindingsItor;
                        continue;
                    }
                    else
                    {
                        bindingParam = ( *constantDefinitionBinding ).second;
                    }

                    buffer_info.buffer = bufferInterface->getVboName();
                    buffer_info.offset = bindingParam.offset;
                    buffer_info.range = bindingParam.size;

                    // currentOffset += bytesToWrite;

                    // if( is_dynamic_buffer_descriptor_type( binding_info->descriptorType ) )
                    // {
                    //     dynamic_offsets.push_back( to_u32( buffer_info.offset ) );
                    //
                    //     buffer_info.offset = 0;
                    // }

                    buffer_infos[binding.binding][0] = buffer_info;

                }
                // else if( image_view != nullptr || sampler != VK_NULL_HANDLE )
                // {
                //     // Can be null for input attachments
                //     VkDescriptorImageInfo image_info{};
                //     image_info.sampler = sampler ? sampler->get_handle() : VK_NULL_HANDLE;
                //     image_info.imageView = image_view->get_handle();
                //
                //     if( image_view != nullptr )
                //     {
                //         // Add image layout info based on descriptor type
                //         switch( binding.descriptorType )
                //         {
                //         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                //         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                //             if( is_depth_stencil_format( image_view->get_format() ) )
                //             {
                //                 image_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                //             }
                //             else
                //             {
                //                 image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                //             }
                //             break;
                //         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                //             image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                //             break;
                //
                //         default:
                //             continue;
                //         }
                //     }
                //
                //     image_infos[binding.binding][0] = std::move( image_info );
                // }

                ++bindingsItor;
                // ++arrayElement;
            }

            ++bindingArraySetItor;
            // ++set;
        }

        VulkanDescriptorPool *descriptorPool =
            new VulkanDescriptorPool( mDevice->mDevice, pso->descriptorLayoutSets[0] );

        VulkanDescriptorSet *descriptorSet =
            new VulkanDescriptorSet( mDevice->mDevice, pso->descriptorLayoutSets[0], *descriptorPool, buffer_infos, image_infos );

        VkDescriptorSet descriptorSetHandle = descriptorSet->get_handle();

        // Bind descriptor set
        vkCmdBindDescriptorSets( mDevice->mGraphicsQueue.mCurrentCmdBuffer, pipeline_bind_point, pso->pipelineLayout,
                                 0, 1, &descriptorSetHandle,
                                 0, 0);

        // const auto &pipeline_layout = pipeline_state.get_pipeline_layout();
        //
        //
        //
        // // Iterate over the shader sets to check if they have already been bound
        // // If they have, add the set so that the command buffer later updates it
        // for( auto &set_it : pipeline_layout.get_shader_sets() )
        // {
        //     uint32_t descriptor_set_id = set_it.first;
        //
        //     auto descriptor_set_layout_it =
        //         descriptor_set_layout_binding_state.find( descriptor_set_id );
        //
        //     if( descriptor_set_layout_it != descriptor_set_layout_binding_state.end() )
        //     {
        //         if( descriptor_set_layout_it->second->get_handle() !=
        //             pipeline_layout.get_descriptor_set_layout( descriptor_set_id ).get_handle() )
        //         {
        //             update_descriptor_sets.emplace( descriptor_set_id );
        //         }
        //     }
        // }
        //
        // // Validate that the bound descriptor set layouts exist in the pipeline layout
        // for( auto set_it = descriptor_set_layout_binding_state.begin();
        //      set_it != descriptor_set_layout_binding_state.end(); )
        // {
        //     if( !pipeline_layout.has_descriptor_set_layout( set_it->first ) )
        //     {
        //         set_it = descriptor_set_layout_binding_state.erase( set_it );
        //     }
        //     else
        //     {
        //         ++set_it;
        //     }
        // }
        //
        // // Check if a descriptor set needs to be created
        // if( resource_binding_state.is_dirty() || !update_descriptor_sets.empty() )
        // {
        //     resource_binding_state.clear_dirty();
        //
        //     // Iterate over all of the resource sets bound by the command buffer
        //     for( auto &resource_set_it : resource_binding_state.get_resource_sets() )
        //     {
        //         uint32_t descriptor_set_id = resource_set_it.first;
        //         auto &resource_set = resource_set_it.second;
        //
        //         // Don't update resource set if it's not in the update list OR its state hasn't changed
        //         if( !resource_set.is_dirty() && ( update_descriptor_sets.find( descriptor_set_id ) ==
        //                                           update_descriptor_sets.end() ) )
        //         {
        //             continue;
        //         }
        //
        //         // Clear dirty flag for resource set
        //         resource_binding_state.clear_dirty( descriptor_set_id );
        //
        //         // Skip resource set if a descriptor set layout doesn't exist for it
        //         if( !pipeline_layout.has_descriptor_set_layout( descriptor_set_id ) )
        //         {
        //             continue;
        //         }
        //
        //         auto &descriptor_set_layout =
        //             pipeline_layout.get_descriptor_set_layout( descriptor_set_id );
        //
        //         // Make descriptor set layout bound for current set
        //         descriptor_set_layout_binding_state[descriptor_set_id] = &descriptor_set_layout;
        //
        //         BindingMap<VkDescriptorBufferInfo> buffer_infos;
        //         BindingMap<VkDescriptorImageInfo> image_infos;
        //
        //         std::vector<uint32_t> dynamic_offsets;
        //
        //         // Iterate over all resource bindings
        //         for( auto &binding_it : resource_set.get_resource_bindings() )
        //         {
        //             auto binding_index = binding_it.first;
        //             auto &binding_resources = binding_it.second;
        //
        //             // Check if binding exists in the pipeline layout
        //             if( auto binding_info = descriptor_set_layout.get_layout_binding( binding_index ) )
        //             {
        //                 // Iterate over all binding resources
        //                 for( auto &element_it : binding_resources )
        //                 {
        //                     auto array_element = element_it.first;
        //                     auto &resource_info = element_it.second;
        //
        //                     // Pointer references
        //                     auto &buffer = resource_info.buffer;
        //                     auto &sampler = resource_info.sampler;
        //                     auto &image_view = resource_info.image_view;
        //
        //                     // Get buffer info
        //                     if( buffer != nullptr &&
        //                         is_buffer_descriptor_type( binding_info->descriptorType ) )
        //                     {
        //                         VkDescriptorBufferInfo buffer_info{};
        //
        //                         buffer_info.buffer = resource_info.buffer->get_handle();
        //                         buffer_info.offset = resource_info.offset;
        //                         buffer_info.range = resource_info.range;
        //
        //                         if( is_dynamic_buffer_descriptor_type( binding_info->descriptorType ) )
        //                         {
        //                             dynamic_offsets.push_back( to_u32( buffer_info.offset ) );
        //
        //                             buffer_info.offset = 0;
        //                         }
        //
        //                         buffer_infos[binding_index][array_element] = buffer_info;
        //                     }
        //
        //                     // Get image info
        //                     else if( image_view != nullptr || sampler != VK_NULL_HANDLE )
        //                     {
        //                         // Can be null for input attachments
        //                         VkDescriptorImageInfo image_info{};
        //                         image_info.sampler = sampler ? sampler->get_handle() : VK_NULL_HANDLE;
        //                         image_info.imageView = image_view->get_handle();
        //
        //                         if( image_view != nullptr )
        //                         {
        //                             // Add image layout info based on descriptor type
        //                             switch( binding_info->descriptorType )
        //                             {
        //                             case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        //                             case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        //                                 if( is_depth_stencil_format( image_view->get_format() ) )
        //                                 {
        //                                     image_info.imageLayout =
        //                                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        //                                 }
        //                                 else
        //                                 {
        //                                     image_info.imageLayout =
        //                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        //                                 }
        //                                 break;
        //                             case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        //                                 image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        //                                 break;
        //
        //                             default:
        //                                 continue;
        //                             }
        //                         }
        //
        //                         image_infos[binding_index][array_element] = std::move( image_info );
        //                     }
        //                 }
        //             }
        //         }
        //
        //         auto &descriptor_set = command_pool.get_render_frame()->request_descriptor_set(
        //             descriptor_set_layout, buffer_infos, image_infos, command_pool.get_thread_index() );
        //
        //         VkDescriptorSet descriptor_set_handle = descriptor_set.get_handle();
        //
        //         // Bind descriptor set
        //         vkCmdBindDescriptorSets( get_handle(), pipeline_bind_point, pipeline_layout.get_handle(),
        //                                  descriptor_set_id, 1, &descriptor_set_handle,
        //                                  to_u32( dynamic_offsets.size() ), dynamic_offsets.data() );
        //     }
        // }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::bindDescriptorSet()
    {
        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );

        BindingMap<VkDescriptorBufferInfo> buffer_infos;
        BindingMap<VkBufferView> buffer_views;
        BindingMap<VkDescriptorImageInfo> image_infos;

        const std::vector<VulkanConstBufferPacked *> &constBuffers = vaoManager->getConstBuffers();
        std::vector<VulkanConstBufferPacked *>::const_iterator constBuffersIt = constBuffers.begin();
        std::vector<VulkanConstBufferPacked *>::const_iterator constBuffersEnd = constBuffers.end();

        while( constBuffersIt != constBuffersEnd )
        {
            VulkanConstBufferPacked *const constBuffer = *constBuffersIt;
            if( constBuffer->isDirty() )
            {
                const VkDescriptorBufferInfo &bufferInfo = constBuffer->getBufferInfo();
                uint16 binding = constBuffer->getCurrentBinding();
                buffer_infos[binding][0] = bufferInfo;
                constBuffer->resetDirty();
            }
            ++constBuffersIt;
        }

        const std::vector<VulkanTexBufferPacked *> &texBuffers = vaoManager->getTexBuffersPacked();
        std::vector<VulkanTexBufferPacked *>::const_iterator texBuffersIt = texBuffers.begin();
        std::vector<VulkanTexBufferPacked *>::const_iterator texBuffersEnd = texBuffers.end();

        while( texBuffersIt != texBuffersEnd )
        {
            VulkanTexBufferPacked *const texBuffer = *texBuffersIt;
            if( texBuffer->isDirty() )
            {
                VkBufferView bufferView = texBuffer->getBufferView();
                uint16 binding = texBuffer->getCurrentBinding();
                buffer_views[binding][0] = bufferView;
                texBuffer->resetDirty();
            }
            ++texBuffersIt;
        }

        VulkanHlmsPso *pso = mPso;

        VulkanDescriptorPool *descriptorPool =
            new VulkanDescriptorPool( mDevice->mDevice, pso->descriptorLayoutSets[0] );

        VulkanDescriptorSet *descriptorSet =
            new VulkanDescriptorSet( mDevice->mDevice, pso->descriptorLayoutSets[0], *descriptorPool,
                                     buffer_infos, image_infos, buffer_views );

        VkDescriptorSet descriptorSetHandle = descriptorSet->get_handle();

        // Bind descriptor set
        vkCmdBindDescriptorSets( mDevice->mGraphicsQueue.mCurrentCmdBuffer,
                                 VK_PIPELINE_BIND_POINT_GRAPHICS, pso->pipelineLayout, 0, 1,
                                 &descriptorSetHandle, 0, 0 );
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_render( const CbDrawCallIndexed *cmd )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " * _render: CbDrawCallIndexed " ) +
                                    std::to_string( cmd->vao->getVaoName() ) );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_render( const CbDrawCallStrip *cmd )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " * _render: CbDrawCallStrip " ) +
                                    std::to_string( cmd->vao->getVaoName() ) );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_renderEmulated( const CbDrawCallIndexed *cmd )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " * _renderEmulated: CbDrawCallIndexed " ) +
                                    std::to_string( cmd->vao->getVaoName() ) );
        }

        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );

        bindDescriptorSet();

        const VulkanVertexArrayObject *vao = static_cast<const VulkanVertexArrayObject *>( cmd->vao );

        // Calculate bytesPerVertexBuffer & numVertexBuffers which is the same for all draws in this cmd
        size_t bytesPerVertexBuffer[15];
        size_t numVertexBuffers = 0;
        const VertexBufferPackedVec &vertexBuffersPackedVec = vao->getVertexBuffers();
        VertexBufferPackedVec::const_iterator itor = vertexBuffersPackedVec.begin();
        VertexBufferPackedVec::const_iterator end = vertexBuffersPackedVec.end();

        while( itor != end )
        {
            bytesPerVertexBuffer[numVertexBuffers] = ( *itor )->getBytesPerElement();
            ++numVertexBuffers;
            ++itor;
        }

        const VulkanBufferInterface *bufferInterface =
            static_cast<const VulkanBufferInterface *>( vao->getIndexBuffer()->getBufferInterface() );
        CbDrawIndexed *drawCmd = reinterpret_cast<CbDrawIndexed *>( mSwIndirectBufferPtr +
                                                                    (size_t)cmd->indirectBufferOffset );

        const size_t bytesPerIndexElement = vao->getIndexBuffer()->getBytesPerElement();
        
        VkCommandBuffer cmdBuffer = mActiveDevice->mGraphicsQueue.mCurrentCmdBuffer;

        vkCmdBindIndexBuffer( cmdBuffer, bufferInterface->getVboName(), 0, VK_INDEX_TYPE_UINT16 );

        VulkanBufferInterface *bufIntf =
            static_cast<VulkanBufferInterface *>( vaoManager->getDrawId()->getBufferInterface() );

        for( uint32 i = cmd->numDraws; i--; )
        {
            std::vector<VkBuffer> vertexBuffers;
            std::vector<VkDeviceSize> offsets;

            vertexBuffers.resize( numVertexBuffers + 1 );
            offsets.resize( numVertexBuffers + 1 );

            for( size_t j = 0; j < numVertexBuffers; ++j )
            {
                VulkanBufferInterface *bufIntf = static_cast<VulkanBufferInterface *>( vertexBuffersPackedVec[j]->getBufferInterface() );
                vertexBuffers[j] = bufIntf->getVboName();
                offsets[j] = drawCmd->baseVertex * bytesPerVertexBuffer[j];
            }
            vertexBuffers[numVertexBuffers] = bufIntf->getVboName();
            offsets[numVertexBuffers] = 0;
            vkCmdBindVertexBuffers( cmdBuffer, 0, numVertexBuffers + 1, vertexBuffers.data(),
                                    offsets.data() );

            // vaoManager->bindDrawIdVertexBuffer( cmdBuffer );

            vkCmdDrawIndexed( cmdBuffer, drawCmd->primCount, drawCmd->instanceCount,
                              drawCmd->firstVertexIndex, drawCmd->baseVertex, drawCmd->baseInstance );
            ++drawCmd;
        }
        
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_renderEmulated( const CbDrawCallStrip *cmd )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " * _renderEmulated: CbDrawCallStrip " ) +
                                    std::to_string( cmd->vao->getVaoName() ) );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_setRenderOperation( const v1::CbRenderOp *cmd )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String(" v1 * _render: CbRenderOp " ) );
        }

        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );

        VkCommandBuffer cmdBuffer = mActiveDevice->mGraphicsQueue.mCurrentCmdBuffer;

        VkBuffer vulkanVertexBuffers[15];
        int offsets[15];
        memset( offsets, 0, sizeof( offsets ) );

        size_t maxUsedSlot = 0;
        const v1::VertexBufferBinding::VertexBufferBindingMap &binds =
            cmd->vertexData->vertexBufferBinding->getBindings();
        v1::VertexBufferBinding::VertexBufferBindingMap::const_iterator itor = binds.begin();
        v1::VertexBufferBinding::VertexBufferBindingMap::const_iterator end = binds.end();

        while( itor != end )
        {
            v1::VulkanHardwareVertexBuffer *metalBuffer =
                reinterpret_cast<v1::VulkanHardwareVertexBuffer *>( itor->second.get() );

            const size_t slot = itor->first;
#if OGRE_DEBUG_MODE
            assert( slot < 15u );
#endif
            size_t offsetStart;
            vulkanVertexBuffers[slot] = metalBuffer->getBufferName( offsetStart );
            offsets[slot] = offsetStart;
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
            offsets[slot] += cmd->vertexData->vertexStart * metalBuffer->getVertexSize();
#endif
            ++itor;
            maxUsedSlot = std::max( maxUsedSlot, slot + 1u );
        }

        // [mActiveRenderEncoder setVertexBuffers:metalVertexBuffers
        //                                offsets:offsets
        //                              withRange:NSMakeRange( 0, maxUsedSlot )];

        mCurrentIndexBuffer = cmd->indexData;
        mCurrentVertexBuffer = cmd->vertexData;
        mCurrentPrimType = std::min( VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                                     static_cast<VkPrimitiveTopology>( cmd->operationType - 1u ) );

        // vkCmdBindIndexBuffer( cmdBuffer, cmd->indexData->indexBuffer->getVboName(), 0, VK_INDEX_TYPE_UINT16 );
        //
        // VulkanBufferInterface *bufIntf =
        //     static_cast<VulkanBufferInterface *>( vaoManager->getDrawId()->getBufferInterface() );
        //
        // for( uint32 i = cmd->numDraws; i--; )
        // {
        //     std::vector<VkBuffer> vertexBuffers;
        //     std::vector<VkDeviceSize> offsets;
        //
        //     vertexBuffers.resize( numVertexBuffers + 1 );
        //     offsets.resize( numVertexBuffers + 1 );
        //
        //     for( size_t j = 0; j < numVertexBuffers; ++j )
        //     {
        //         VulkanBufferInterface *bufIntf = static_cast<VulkanBufferInterface *>(
        //             vertexBuffersPackedVec[j]->getBufferInterface() );
        //         vertexBuffers[j] = bufIntf->getVboName();
        //         offsets[j] = drawCmd->baseVertex * bytesPerVertexBuffer[j];
        //     }
        //     vertexBuffers[numVertexBuffers] = bufIntf->getVboName();
        //     offsets[numVertexBuffers] = 0;
        //     vkCmdBindVertexBuffers( cmdBuffer, 0, numVertexBuffers + 1, vertexBuffers.data(),
        //                             offsets.data() );
        //
        //     // vaoManager->bindDrawIdVertexBuffer( cmdBuffer );
        //
        //     vkCmdDrawIndexed( cmdBuffer, drawCmd->primCount, drawCmd->instanceCount,
        //                       drawCmd->firstVertexIndex, drawCmd->baseVertex, drawCmd->baseInstance );
        //     ++drawCmd;
        // }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_render( const v1::CbDrawCallIndexed *cmd )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " v1 * _render: CbDrawCallIndexed " ) );
        }

        const VkPrimitiveTopology indexType =
            static_cast<VkPrimitiveTopology>( mCurrentIndexBuffer->indexBuffer->getType() );

        // Get index buffer stuff which is the same for all draws in this cmd
        const size_t bytesPerIndexElement = mCurrentIndexBuffer->indexBuffer->getIndexSize();

        size_t offsetStart;
        v1::VulkanHardwareIndexBuffer *metalBuffer =
            static_cast<v1::VulkanHardwareIndexBuffer *>( mCurrentIndexBuffer->indexBuffer.get() );
        VkBuffer indexBuffer = metalBuffer->getBufferName( offsetStart );

#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
#    if OGRE_DEBUG_MODE
        assert( ( ( cmd->firstVertexIndex * bytesPerIndexElement ) & 0x03 ) == 0 &&
                "Index Buffer must be aligned to 4 bytes. If you're messing with "
                "IndexBuffer::indexStart, you've entered an invalid "
                "indexStart; not supported by the Metal API." );
#    endif

        // Setup baseInstance.
        // [mActiveRenderEncoder setVertexBufferOffset:cmd->baseInstance * sizeof( uint32 ) atIndex:15];
        //
        // [mActiveRenderEncoder
        //     drawIndexedPrimitives:mCurrentPrimType
        //                indexCount:cmd->primCount
        //                 indexType:indexType
        //               indexBuffer:indexBuffer
        //         indexBufferOffset:cmd->firstVertexIndex * bytesPerIndexElement + offsetStart
        //             instanceCount:cmd->instanceCount];
#else
        // [mActiveRenderEncoder
        //     drawIndexedPrimitives:mCurrentPrimType
        //                indexCount:cmd->primCount
        //                 indexType:indexType
        //               indexBuffer:indexBuffer
        //         indexBufferOffset:cmd->firstVertexIndex * bytesPerIndexElement + offsetStart
        //             instanceCount:cmd->instanceCount
        //                baseVertex:mCurrentVertexBuffer->vertexStart
        //              baseInstance:cmd->baseInstance];
#endif
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_render( const v1::CbDrawCallStrip *cmd )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String(" v1 * _render: CbDrawCallStrip " ) );
        }

        VulkanVaoManager *vaoManager = static_cast<VulkanVaoManager *>( mVaoManager );

        bindDescriptorSet();

        #if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
        // Setup baseInstance.
        // [mActiveRenderEncoder setVertexBufferOffset:cmd->baseInstance * sizeof( uint32 ) atIndex:15];
        // [mActiveRenderEncoder
        //     drawPrimitives:mCurrentPrimType
        //        vertexStart:0 /*cmd->firstVertexIndex already handled in _setRenderOperation*/
        //        vertexCount:cmd->primCount
        //      instanceCount:cmd->instanceCount];
#else
        // [mActiveRenderEncoder drawPrimitives:mCurrentPrimType
        //                          vertexStart:cmd->firstVertexIndex
        //                          vertexCount:cmd->primCount
        //                        instanceCount:cmd->instanceCount
        //                         baseInstance:cmd->baseInstance];
#endif
    }

    void VulkanRenderSystem::_render( const v1::RenderOperation &op )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " * _render: RenderOperation " ) +
                                    std::to_string( op.operationType ) );
        }

        // Call super class.
        RenderSystem::_render( op );

        const size_t numberOfInstances = op.numberOfInstances;
        const bool hasInstanceData = mCurrentVertexBuffer->vertexBufferBinding->getHasInstanceData();

        // Render to screen!
        if( op.useIndexes )
        {
            do
            {
                // Update derived depth bias.
                if( mDerivedDepthBias && mCurrentPassIterationNum > 0 )
                {
                    const float biasSign = mReverseDepth ? 1.0f : -1.0f;
                    // [mActiveRenderEncoder
                    //     setDepthBias:( mDerivedDepthBiasBase +
                    //                    mDerivedDepthBiasMultiplier * mCurrentPassIterationNum ) *
                    //                  biasSign
                    //       slopeScale:mDerivedDepthBiasSlopeScale * biasSign
                    //            clamp:0.0f];
                }

                const VkPrimitiveTopology indexType =
                    static_cast<VkPrimitiveTopology>( mCurrentIndexBuffer->indexBuffer->getType() );

                // Get index buffer stuff which is the same for all draws in this cmd
                const size_t bytesPerIndexElement = mCurrentIndexBuffer->indexBuffer->getIndexSize();

                size_t offsetStart;
                v1::VulkanHardwareIndexBuffer *metalBuffer =
                    static_cast<v1::VulkanHardwareIndexBuffer *>( mCurrentIndexBuffer->indexBuffer.get() );
                VkBuffer indexBuffer =
                    metalBuffer->getBufferName( offsetStart );

#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
#    if OGRE_DEBUG_MODE
                assert( ( ( mCurrentIndexBuffer->indexStart * bytesPerIndexElement ) & 0x03 ) == 0 &&
                        "Index Buffer must be aligned to 4 bytes. If you're messing with "
                        "IndexBuffer::indexStart, you've entered an invalid "
                        "indexStart; not supported by the Metal API." );
#    endif

                // [mActiveRenderEncoder
                //     drawIndexedPrimitives:mCurrentPrimType
                //                indexCount:mCurrentIndexBuffer->indexCount
                //                 indexType:indexType
                //               indexBuffer:indexBuffer
                //         indexBufferOffset:mCurrentIndexBuffer->indexStart * bytesPerIndexElement +
                //                           offsetStart
                //             instanceCount:numberOfInstances];
#else
                // [mActiveRenderEncoder
                //     drawIndexedPrimitives:mCurrentPrimType
                //                indexCount:mCurrentIndexBuffer->indexCount
                //                 indexType:indexType
                //               indexBuffer:indexBuffer
                //         indexBufferOffset:mCurrentIndexBuffer->indexStart * bytesPerIndexElement +
                //                           offsetStart
                //             instanceCount:numberOfInstances
                //                baseVertex:mCurrentVertexBuffer->vertexStart
                //              baseInstance:0];
#endif
            } while( updatePassIterationRenderState() );
        }
        else
        {
            do
            {
                // Update derived depth bias.
                if( mDerivedDepthBias && mCurrentPassIterationNum > 0 )
                {
                    const float biasSign = mReverseDepth ? 1.0f : -1.0f;
                    // [mActiveRenderEncoder
                    //     setDepthBias:( mDerivedDepthBiasBase +
                    //                    mDerivedDepthBiasMultiplier * mCurrentPassIterationNum ) *
                    //                  biasSign
                    //       slopeScale:mDerivedDepthBiasSlopeScale * biasSign
                    //            clamp:0.0f];
                }

#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
                const uint32 vertexStart = 0;
#else
                const uint32 vertexStart = static_cast<uint32>( mCurrentVertexBuffer->vertexStart );
#endif

                if( hasInstanceData )
                {
                    // [mActiveRenderEncoder drawPrimitives:mCurrentPrimType
                    //                          vertexStart:vertexStart
                    //                          vertexCount:mCurrentVertexBuffer->vertexCount
                    //                        instanceCount:numberOfInstances];
                }
                else
                {
                    // [mActiveRenderEncoder drawPrimitives:mCurrentPrimType
                    //                          vertexStart:vertexStart
                    //                          vertexCount:mCurrentVertexBuffer->vertexCount];
                }
            } while( updatePassIterationRenderState() );
        }
    }

    //-------------------------------------------------------------------------
    void VulkanRenderSystem::bindGpuProgramParameters( GpuProgramType gptype,
                                                       GpuProgramParametersSharedPtr params,
                                                       uint16 variabilityMask )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " * bindGpuProgramParameters:  " ) );
        }
        VulkanProgram *shader = 0;
        switch( gptype )
        {
        case GPT_VERTEX_PROGRAM:
            mActiveVertexGpuProgramParameters = params;
            shader = mPso->vertexShader;
            break;
        case GPT_FRAGMENT_PROGRAM:
            mActiveFragmentGpuProgramParameters = params;
            shader = mPso->pixelShader;
            break;
        case GPT_GEOMETRY_PROGRAM:
            mActiveGeometryGpuProgramParameters = params;
            OGRE_EXCEPT( Exception::ERR_NOT_IMPLEMENTED, "Geometry Shaders are not supported in Vulkan.",
                         "VulkanRenderSystem::bindGpuProgramParameters" );
            break;
        case GPT_HULL_PROGRAM:
            mActiveTessellationHullGpuProgramParameters = params;
            OGRE_EXCEPT( Exception::ERR_NOT_IMPLEMENTED, "Tesselation is different in Vulkan.",
                         "VulkanRenderSystem::bindGpuProgramParameters" );
            break;
        case GPT_DOMAIN_PROGRAM:
            mActiveTessellationDomainGpuProgramParameters = params;
            OGRE_EXCEPT( Exception::ERR_NOT_IMPLEMENTED, "Tesselation is different in Vulkan.",
                         "VulkanRenderSystem::bindGpuProgramParameters" );
            break;
        case GPT_COMPUTE_PROGRAM:
            // mActiveComputeGpuProgramParameters = params;
            // shader = static_cast<VulkanProgram *>( mComputePso->computeShader->_getBindingDelegate() );
            break;
        }

        size_t bytesToWrite = shader->getBufferRequiredSize();  // + mPso->pixelShader->getBufferRequiredSize();
        if( shader && bytesToWrite > 0 )
        {
            if( mCurrentAutoParamsBufferSpaceLeft < bytesToWrite )
            {
                if( mAutoParamsBufferIdx >= mAutoParamsBuffer.size() )
                {
                    ConstBufferPacked *constBuffer =
                        mVaoManager->createConstBuffer( std::max<size_t>( 512u * 1024u, bytesToWrite ),
                                                        BT_DYNAMIC_PERSISTENT, 0, false );
                    mAutoParamsBuffer.push_back( constBuffer );
                }

                ConstBufferPacked *constBuffer = mAutoParamsBuffer[mAutoParamsBufferIdx];
                mCurrentAutoParamsBufferPtr =
                    reinterpret_cast<uint8 *>( constBuffer->map( 0, constBuffer->getNumElements() ) );
                mCurrentAutoParamsBufferSpaceLeft = constBuffer->getTotalSizeBytes();

                ++mAutoParamsBufferIdx;
            }

            shader->updateBuffers( params, mCurrentAutoParamsBufferPtr );

            assert(
                dynamic_cast<VulkanConstBufferPacked *>( mAutoParamsBuffer[mAutoParamsBufferIdx - 1u] ) );

            VulkanConstBufferPacked *constBuffer =
                static_cast<VulkanConstBufferPacked *>( mAutoParamsBuffer[mAutoParamsBufferIdx - 1u] );
            const size_t bindOffset =
                constBuffer->getTotalSizeBytes() - mCurrentAutoParamsBufferSpaceLeft;

            // const unordered_map<unsigned, VulkanConstantDefinitionBindingParam>::type &vertexShaderBindings = 
            //     shader->getConstantDefsBindingParams();
            //
            // flushDescriptorState( VK_PIPELINE_BIND_POINT_GRAPHICS, *constBuffer, bindOffset,
            //                       bytesToWrite, vertexShaderBindings );
            VkCommandBuffer cmdBuffer = mActiveDevice->mGraphicsQueue.mCurrentCmdBuffer;
            switch( gptype )
            {
            case GPT_VERTEX_PROGRAM:
            case GPT_FRAGMENT_PROGRAM:
            case GPT_COMPUTE_PROGRAM:
                constBuffer->bindBuffer( OGRE_VULKAN_PARAMETER_SLOT - OGRE_VULKAN_CONST_SLOT_START,
                                           bindOffset );
                break;
            case GPT_GEOMETRY_PROGRAM:
            case GPT_HULL_PROGRAM:
            case GPT_DOMAIN_PROGRAM:
                break;
            }

            mCurrentAutoParamsBufferPtr += bytesToWrite;

            const uint8 *oldBufferPos = mCurrentAutoParamsBufferPtr;
            mCurrentAutoParamsBufferPtr = reinterpret_cast<uint8 *>(
                alignToNextMultiple( reinterpret_cast<uintptr_t>( mCurrentAutoParamsBufferPtr ),
                                     mVaoManager->getConstBufferAlignment() ) );
            bytesToWrite += mCurrentAutoParamsBufferPtr - oldBufferPos;

            // We know that bytesToWrite <= mCurrentAutoParamsBufferSpaceLeft, but that was
            // before padding. After padding this may no longer hold true.
            mCurrentAutoParamsBufferSpaceLeft -=
                std::min( mCurrentAutoParamsBufferSpaceLeft, bytesToWrite );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::bindGpuProgramPassIterationParameters( GpuProgramType gptype ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::clearFrameBuffer( RenderPassDescriptor *renderPassDesc,
                                               TextureGpu *anyTarget, uint8 mipLevel )
    {
    }
    //-------------------------------------------------------------------------
    Real VulkanRenderSystem::getHorizontalTexelOffset( void ) { return 0.0f; }
    //-------------------------------------------------------------------------
    Real VulkanRenderSystem::getVerticalTexelOffset( void ) { return 0.0f; }
    //-------------------------------------------------------------------------
    Real VulkanRenderSystem::getMinimumDepthInputValue( void ) { return 0.0f; }
    //-------------------------------------------------------------------------
    Real VulkanRenderSystem::getMaximumDepthInputValue( void ) { return 1.0f; }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::preExtraThreadsStarted() {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::postExtraThreadsStarted() {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::registerThread() {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::unregisterThread() {}
    //-------------------------------------------------------------------------
    const PixelFormatToShaderType *VulkanRenderSystem::getPixelFormatToShaderType( void ) const
    {
        return &mPixelFormatToShaderType;
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::flushCommands( void ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::beginProfileEvent( const String &eventName ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::endProfileEvent( void ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::markProfileEvent( const String &event ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::initGPUProfiling( void ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::deinitGPUProfiling( void ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::beginGPUSampleProfile( const String &name, uint32 *hashCache ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::endGPUSampleProfile( const String &name ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::setClipPlanesImpl( const PlaneList &clipPlanes ) {}
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::initialiseFromRenderSystemCapabilities( RenderSystemCapabilities *caps,
                                                                     Window *primary )
    {
        mShaderManager = OGRE_NEW VulkanGpuProgramManager( mActiveDevice );
        mVulkanProgramFactory = OGRE_NEW VulkanProgramFactory( mActiveDevice );
        HighLevelGpuProgramManager::getSingleton().addFactory( mVulkanProgramFactory );

        mCache = OGRE_NEW VulkanCache( mActiveDevice );

        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            caps->log( defaultLog );
            defaultLog->logMessage( " * Using Reverse Z: " +
                                    StringConverter::toString( mReverseDepth, true ) );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::beginRenderPassDescriptor( RenderPassDescriptor *desc,
                                                        TextureGpu *anyTarget, uint8 mipLevel,
                                                        const Vector4 *viewportSizes,
                                                        const Vector4 *scissors, uint32 numViewports,
                                                        bool overlaysEnabled, bool warnIfRtvWasFlushed )
    {
        if( desc->mInformationOnly && desc->hasSameAttachments( mCurrentRenderPassDescriptor ) )
            return;

        const int oldWidth = mCurrentRenderViewport[0].getActualWidth();
        const int oldHeight = mCurrentRenderViewport[0].getActualHeight();
        const int oldX = mCurrentRenderViewport[0].getActualLeft();
        const int oldY = mCurrentRenderViewport[0].getActualTop();

        VulkanRenderPassDescriptor *currPassDesc =
            static_cast<VulkanRenderPassDescriptor *>( mCurrentRenderPassDescriptor );

        RenderSystem::beginRenderPassDescriptor( desc, anyTarget, mipLevel, viewportSizes, scissors,
                                                 numViewports, overlaysEnabled, warnIfRtvWasFlushed );

        // Calculate the new "lower-left" corner of the viewport to compare with the old one
        const int w = mCurrentRenderViewport[0].getActualWidth();
        const int h = mCurrentRenderViewport[0].getActualHeight();
        const int x = mCurrentRenderViewport[0].getActualLeft();
        const int y = mCurrentRenderViewport[0].getActualTop();

        const bool vpChanged =
            oldX != x || oldY != y || oldWidth != w || oldHeight != h || numViewports > 1u;

        VulkanRenderPassDescriptor *newPassDesc = static_cast<VulkanRenderPassDescriptor *>( desc );

        // Determine whether:
        //  1. We need to store current active RenderPassDescriptor
        //  2. We need to perform clears when loading the new RenderPassDescriptor
        uint32 entriesToFlush = 0;
        if( currPassDesc )
        {
            entriesToFlush = currPassDesc->willSwitchTo( newPassDesc, warnIfRtvWasFlushed );

            if( entriesToFlush != 0 )
                currPassDesc->performStoreActions();
        }
        else
        {
            entriesToFlush = RenderPassDescriptor::All;
        }

        mEntriesToFlush = entriesToFlush;
        mVpChanged = vpChanged;
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::executeRenderPassDescriptorDelayedActions( void )
    {
        if( mEntriesToFlush )
        {
            VulkanRenderPassDescriptor *newPassDesc =
                static_cast<VulkanRenderPassDescriptor *>( mCurrentRenderPassDescriptor );

            newPassDesc->performLoadActions();

#if VULKAN_DISABLED
            [mActiveRenderEncoder setFrontFacingWinding:MTLWindingCounterClockwise];

            if( mStencilEnabled )
                [mActiveRenderEncoder setStencilReferenceValue:mStencilRefValue];
#endif
        }

        flushUAVs();

        const uint32 numViewports = mMaxBoundViewports;

        // If we flushed, viewport and scissor settings got reset.
        if( mVpChanged || numViewports > 1u )
        {
            VkViewport vkVp[16];
            for( size_t i = 0; i < numViewports; ++i )
            {
                vkVp[i].x = mCurrentRenderViewport[i].getActualLeft();
                vkVp[i].y = mCurrentRenderViewport[i].getActualTop();
                vkVp[i].width = mCurrentRenderViewport[i].getActualWidth();
                vkVp[i].height = mCurrentRenderViewport[i].getActualHeight();
                vkVp[i].minDepth = 0;
                vkVp[i].maxDepth = 1;
            }

            vkCmdSetViewport( mDevice->mGraphicsQueue.mCurrentCmdBuffer, 0u, numViewports, vkVp );
        }

        if( mVpChanged || numViewports > 1u )
        {
            VkRect2D scissorRect[16];
            for( size_t i = 0; i < numViewports; ++i )
            {
                scissorRect[i].offset.x = mCurrentRenderViewport[i].getScissorActualLeft();
                scissorRect[i].offset.y = mCurrentRenderViewport[i].getScissorActualTop();
                scissorRect[i].extent.width =
                    static_cast<uint32>( mCurrentRenderViewport[i].getScissorActualWidth() );
                scissorRect[i].extent.height =
                    static_cast<uint32>( mCurrentRenderViewport[i].getScissorActualHeight() );
            }

            vkCmdSetScissor( mDevice->mGraphicsQueue.mCurrentCmdBuffer, 0u, numViewports, scissorRect );
        }

        mEntriesToFlush = 0;
        mVpChanged = false;
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::endRenderPassDescriptor( void )
    {
        if( mCurrentRenderPassDescriptor )
        {
            VulkanRenderPassDescriptor *passDesc =
                static_cast<VulkanRenderPassDescriptor *>( mCurrentRenderPassDescriptor );
            passDesc->performStoreActions();

            mEntriesToFlush = 0;
            mVpChanged = false;

            RenderSystem::endRenderPassDescriptor();
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::notifySwapchainCreated( VulkanWindow *window )
    {
        RenderPassDescriptorSet::const_iterator itor = mRenderPassDescs.begin();
        RenderPassDescriptorSet::const_iterator endt = mRenderPassDescs.end();

        while( itor != endt )
        {
            OGRE_ASSERT_HIGH( dynamic_cast<VulkanRenderPassDescriptor *>( *itor ) );
            VulkanRenderPassDescriptor *renderPassDesc =
                static_cast<VulkanRenderPassDescriptor *>( *itor );
            renderPassDesc->notifySwapchainCreated( window );
            ++itor;
        }
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::notifySwapchainDestroyed( VulkanWindow *window )
    {
        RenderPassDescriptorSet::const_iterator itor = mRenderPassDescs.begin();
        RenderPassDescriptorSet::const_iterator endt = mRenderPassDescs.end();

        while( itor != endt )
        {
            OGRE_ASSERT_HIGH( dynamic_cast<VulkanRenderPassDescriptor *>( *itor ) );
            VulkanRenderPassDescriptor *renderPassDesc =
                static_cast<VulkanRenderPassDescriptor *>( *itor );
            renderPassDesc->notifySwapchainDestroyed( window );
            ++itor;
        };
    }
    //-------------------------------------------------------------------------
    VkRenderPass VulkanRenderSystem::getVkRenderPass( HlmsPassPso passPso, uint8 &outMrtCount )
    {
        uint8 mrtCount = 0;
        for( int i = 0; i < OGRE_MAX_MULTIPLE_RENDER_TARGETS; ++i )
        {
            if( passPso.colourFormat[i] != PFG_NULL )
                mrtCount = static_cast<uint8>( i ) + 1u;
        }
        outMrtCount = mrtCount;

        bool usesResolveAttachments = false;

        uint32 attachmentIdx = 0u;
        uint32 numColourAttachments = 0u;
        VkAttachmentDescription attachments[OGRE_MAX_MULTIPLE_RENDER_TARGETS * 2u + 2u];

        VkAttachmentReference colourAttachRefs[OGRE_MAX_MULTIPLE_RENDER_TARGETS];
        VkAttachmentReference resolveAttachRefs[OGRE_MAX_MULTIPLE_RENDER_TARGETS];
        VkAttachmentReference depthAttachRef;

        memset( attachments, 0, sizeof( attachments ) );
        memset( colourAttachRefs, 0, sizeof( colourAttachRefs ) );
        memset( resolveAttachRefs, 0, sizeof( resolveAttachRefs ) );
        memset( &depthAttachRef, 0, sizeof( depthAttachRef ) );

        for( size_t i = 0; i < mrtCount; ++i )
        {
            resolveAttachRefs[numColourAttachments].attachment = VK_ATTACHMENT_UNUSED;
            resolveAttachRefs[numColourAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            if( passPso.colourFormat[i] != PFG_NULL )
            {
                colourAttachRefs[numColourAttachments].attachment = attachmentIdx;
                colourAttachRefs[numColourAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                ++numColourAttachments;

                attachments[attachmentIdx].samples =
                    static_cast<VkSampleCountFlagBits>( passPso.multisampleCount );
                attachments[attachmentIdx].format = VulkanMappings::get( passPso.colourFormat[i] );
                attachments[attachmentIdx].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                attachments[attachmentIdx].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                ++attachmentIdx;

                if( passPso.resolveColourFormat[i] != PFG_NULL )
                {
                    usesResolveAttachments = true;

                    attachments[attachmentIdx].samples = VK_SAMPLE_COUNT_1_BIT;
                    attachments[attachmentIdx].format =
                        VulkanMappings::get( passPso.resolveColourFormat[i] );
                    attachments[attachmentIdx].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    attachments[attachmentIdx].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    ++attachmentIdx;

                    resolveAttachRefs[numColourAttachments].attachment = attachmentIdx;
                    resolveAttachRefs[numColourAttachments].layout =
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    ++numColourAttachments;
                }
            }
        }

        if( passPso.depthFormat != PFG_NULL )
        {
            attachments[attachmentIdx].format = VulkanMappings::get( passPso.depthFormat );
            attachments[attachmentIdx].samples =
                static_cast<VkSampleCountFlagBits>( passPso.multisampleCount );
            attachments[attachmentIdx].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachments[attachmentIdx].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            depthAttachRef.attachment = attachmentIdx;
            depthAttachRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            ++attachmentIdx;
        }

        VkSubpassDescription subpass;
        memset( &subpass, 0, sizeof( subpass ) );
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.inputAttachmentCount = 0u;
        subpass.colorAttachmentCount = numColourAttachments;
        subpass.pColorAttachments = colourAttachRefs;
        subpass.pResolveAttachments = usesResolveAttachments ? resolveAttachRefs : 0;
        subpass.pDepthStencilAttachment = ( passPso.depthFormat != PFG_NULL ) ? &depthAttachRef : 0;

        VkRenderPassCreateInfo renderPassCreateInfo;
        makeVkStruct( renderPassCreateInfo, VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO );
        renderPassCreateInfo.attachmentCount = attachmentIdx;
        renderPassCreateInfo.pAttachments = attachments;
        renderPassCreateInfo.subpassCount = 1u;
        renderPassCreateInfo.pSubpasses = &subpass;

        VkRenderPass retVal = mCache->getRenderPass( renderPassCreateInfo );
        return retVal;
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_hlmsPipelineStateObjectCreated( HlmsPso *newPso )
    {
        size_t numShaderStages = 0u;
        VkPipelineShaderStageCreateInfo shaderStages[GPT_COMPUTE_PROGRAM];

        DescriptorSetLayoutBindingArray descriptorLayoutBindingSets;

        VulkanProgram *vertexShader = 0;
        VulkanProgram *pixelShader = 0;

        if( !newPso->vertexShader.isNull() )
        {
            vertexShader =
                static_cast<VulkanProgram *>( newPso->vertexShader->_getBindingDelegate() );
            vertexShader->fillPipelineShaderStageCi( shaderStages[numShaderStages++] );
            VulkanDescriptors::generateAndMergeDescriptorSets( vertexShader, descriptorLayoutBindingSets );
        }

        if( !newPso->geometryShader.isNull() )
        {
            VulkanProgram *shader =
                static_cast<VulkanProgram *>( newPso->geometryShader->_getBindingDelegate() );
            shader->fillPipelineShaderStageCi( shaderStages[numShaderStages++] );
            VulkanDescriptors::generateAndMergeDescriptorSets( shader, descriptorLayoutBindingSets );
        }

        if( !newPso->tesselationHullShader.isNull() )
        {
            VulkanProgram *shader =
                static_cast<VulkanProgram *>( newPso->tesselationHullShader->_getBindingDelegate() );
            shader->fillPipelineShaderStageCi( shaderStages[numShaderStages++] );
            VulkanDescriptors::generateAndMergeDescriptorSets( shader, descriptorLayoutBindingSets );
        }

        if( !newPso->tesselationDomainShader.isNull() )
        {
            VulkanProgram *shader =
                static_cast<VulkanProgram *>( newPso->tesselationDomainShader->_getBindingDelegate() );
            shader->fillPipelineShaderStageCi( shaderStages[numShaderStages++] );
            VulkanDescriptors::generateAndMergeDescriptorSets( shader, descriptorLayoutBindingSets );
        }

        if( !newPso->pixelShader.isNull() )
        {
            pixelShader =
                static_cast<VulkanProgram *>( newPso->pixelShader->_getBindingDelegate() );
            pixelShader->fillPipelineShaderStageCi( shaderStages[numShaderStages++] );
            VulkanDescriptors::generateAndMergeDescriptorSets( pixelShader, descriptorLayoutBindingSets );
        }

        VulkanDescriptors::optimizeDescriptorSets( descriptorLayoutBindingSets );

        DescriptorSetLayoutArray sets;
        VkPipelineLayout layout = VulkanDescriptors::generateVkDescriptorSets( descriptorLayoutBindingSets, sets );

        VkPipelineVertexInputStateCreateInfo vertexFormatCi;
        makeVkStruct( vertexFormatCi, VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO );
        VkVertexInputBindingDescription binding_description[2];
        std::vector<VkVertexInputAttributeDescription> attribute_descriptions;
        TODO_vertex_format;
        if( !newPso->vertexShader.isNull() )
        {
            VulkanProgram *shader =
                static_cast<VulkanProgram *>( newPso->vertexShader->_getBindingDelegate() );
            VulkanDescriptors::generateVertexInputBindings( shader, newPso, binding_description,
                                                            attribute_descriptions );
            vertexFormatCi.vertexBindingDescriptionCount = 2;
            vertexFormatCi.vertexAttributeDescriptionCount =
                static_cast<uint32_t>( attribute_descriptions.size() );
            vertexFormatCi.pVertexBindingDescriptions = binding_description;
            vertexFormatCi.pVertexAttributeDescriptions = attribute_descriptions.data();
        }

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyCi;
        makeVkStruct( inputAssemblyCi, VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO );
        inputAssemblyCi.topology = VulkanMappings::get( newPso->operationType );
        inputAssemblyCi.primitiveRestartEnable = newPso->enablePrimitiveRestart;

        VkPipelineTessellationStateCreateInfo tessStateCi;
        makeVkStruct( tessStateCi, VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO );

        VkPipelineViewportStateCreateInfo viewportStateCi;
        makeVkStruct( viewportStateCi, VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO );
        TODO_addVpCount_to_passpso;
        viewportStateCi.viewportCount = 1u;
        viewportStateCi.scissorCount = 1u;

        VkPipelineRasterizationStateCreateInfo rasterState;
        makeVkStruct( rasterState, VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO );
        rasterState.polygonMode = VulkanMappings::get( newPso->macroblock->mPolygonMode );
        rasterState.cullMode = VulkanMappings::get( newPso->macroblock->mCullMode );
        rasterState.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterState.depthBiasEnable = newPso->macroblock->mDepthBiasConstant != 0.0f;
        rasterState.depthBiasConstantFactor = newPso->macroblock->mDepthBiasConstant;
        rasterState.depthBiasClamp = 0.0f;
        rasterState.depthBiasSlopeFactor = newPso->macroblock->mDepthBiasSlopeScale;
        rasterState.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo mssCi;
        makeVkStruct( mssCi, VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO );
        mssCi.rasterizationSamples = static_cast<VkSampleCountFlagBits>( newPso->pass.multisampleCount );
        mssCi.alphaToCoverageEnable = newPso->blendblock->mAlphaToCoverageEnabled;

        VkPipelineDepthStencilStateCreateInfo depthStencilStateCi;
        makeVkStruct( depthStencilStateCi, VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO );
        depthStencilStateCi.depthTestEnable = newPso->macroblock->mDepthCheck;
        depthStencilStateCi.depthWriteEnable = newPso->macroblock->mDepthWrite;
        depthStencilStateCi.depthCompareOp = VulkanMappings::get( newPso->macroblock->mDepthFunc );
        depthStencilStateCi.stencilTestEnable = newPso->pass.stencilParams.enabled;
        if( newPso->pass.stencilParams.enabled )
        {
            depthStencilStateCi.front.failOp =
                VulkanMappings::get( newPso->pass.stencilParams.stencilFront.stencilFailOp );
            depthStencilStateCi.front.passOp =
                VulkanMappings::get( newPso->pass.stencilParams.stencilFront.stencilPassOp );
            depthStencilStateCi.front.depthFailOp =
                VulkanMappings::get( newPso->pass.stencilParams.stencilFront.stencilDepthFailOp );
            depthStencilStateCi.front.compareOp =
                VulkanMappings::get( newPso->pass.stencilParams.stencilFront.compareOp );
            depthStencilStateCi.front.compareMask = newPso->pass.stencilParams.readMask;
            depthStencilStateCi.front.writeMask = newPso->pass.stencilParams.writeMask;
            depthStencilStateCi.front.reference = 0;  // Dynamic state

            depthStencilStateCi.back.failOp =
                VulkanMappings::get( newPso->pass.stencilParams.stencilBack.stencilFailOp );
            depthStencilStateCi.back.passOp =
                VulkanMappings::get( newPso->pass.stencilParams.stencilBack.stencilPassOp );
            depthStencilStateCi.back.depthFailOp =
                VulkanMappings::get( newPso->pass.stencilParams.stencilBack.stencilDepthFailOp );
            depthStencilStateCi.back.compareOp =
                VulkanMappings::get( newPso->pass.stencilParams.stencilBack.compareOp );
            depthStencilStateCi.back.compareMask = newPso->pass.stencilParams.readMask;
            depthStencilStateCi.back.writeMask = newPso->pass.stencilParams.writeMask;
            depthStencilStateCi.back.reference = 0;  // Dynamic state
        }
        depthStencilStateCi.minDepthBounds = 0.0f;
        depthStencilStateCi.maxDepthBounds = 1.0f;

        VkPipelineColorBlendStateCreateInfo blendStateCi;
        makeVkStruct( blendStateCi, VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO );
        blendStateCi.logicOpEnable = false;
        uint8 mrtCount = 0;
        VkRenderPass renderPass = getVkRenderPass( newPso->pass, mrtCount );
        blendStateCi.attachmentCount = mrtCount;
        VkPipelineColorBlendAttachmentState blendStates[OGRE_MAX_MULTIPLE_RENDER_TARGETS];
        memset( blendStates, 0, sizeof( blendStates ) );

        if( newPso->blendblock->mSeparateBlend )
        {
            if( newPso->blendblock->mSourceBlendFactor == SBF_ONE &&
                newPso->blendblock->mDestBlendFactor == SBF_ZERO &&
                newPso->blendblock->mSourceBlendFactorAlpha == SBF_ONE &&
                newPso->blendblock->mDestBlendFactorAlpha == SBF_ZERO )
            {
                blendStates[0].blendEnable = false;
            }
            else
            {
                blendStates[0].blendEnable = true;
                blendStates[0].srcColorBlendFactor =
                    VulkanMappings::get( newPso->blendblock->mSourceBlendFactor );
                blendStates[0].dstColorBlendFactor =
                    VulkanMappings::get( newPso->blendblock->mDestBlendFactor );
                blendStates[0].colorBlendOp = VulkanMappings::get( newPso->blendblock->mBlendOperation );

                blendStates[0].srcAlphaBlendFactor =
                    VulkanMappings::get( newPso->blendblock->mSourceBlendFactorAlpha );
                blendStates[0].dstAlphaBlendFactor =
                    VulkanMappings::get( newPso->blendblock->mDestBlendFactorAlpha );
                blendStates[0].alphaBlendOp = blendStates[0].colorBlendOp;
            }
        }
        else
        {
            if( newPso->blendblock->mSourceBlendFactor == SBF_ONE &&
                newPso->blendblock->mDestBlendFactor == SBF_ZERO )
            {
                blendStates[0].blendEnable = false;
            }
            else
            {
                blendStates[0].blendEnable = true;
                blendStates[0].srcColorBlendFactor =
                    VulkanMappings::get( newPso->blendblock->mSourceBlendFactor );
                blendStates[0].dstColorBlendFactor =
                    VulkanMappings::get( newPso->blendblock->mDestBlendFactor );
                blendStates[0].colorBlendOp = VulkanMappings::get( newPso->blendblock->mBlendOperation );

                blendStates[0].srcAlphaBlendFactor = blendStates[0].srcColorBlendFactor;
                blendStates[0].dstAlphaBlendFactor = blendStates[0].dstColorBlendFactor;
                blendStates[0].alphaBlendOp = blendStates[0].colorBlendOp;
            }
        }
        blendStates[0].colorWriteMask = newPso->blendblock->mBlendChannelMask;

        for( int i = 1; i < mrtCount; ++i )
            blendStates[i] = blendStates[0];

        blendStateCi.pAttachments = blendStates;

        // Having viewport hardcoded into PSO is crazy.
        // It could skyrocket the number of required PSOs and heavily neutralize caches.
        const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                 VK_DYNAMIC_STATE_STENCIL_REFERENCE };

        VkPipelineDynamicStateCreateInfo dynamicStateCi;
        makeVkStruct( dynamicStateCi, VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO );
        dynamicStateCi.dynamicStateCount = sizeof( dynamicStates ) / sizeof( dynamicStates[0] );
        dynamicStateCi.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo pipeline;
        makeVkStruct( pipeline, VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO );

        pipeline.layout = layout;
        pipeline.stageCount = static_cast<uint32>( numShaderStages );
        pipeline.pStages = shaderStages;
        pipeline.pVertexInputState = &vertexFormatCi;
        pipeline.pInputAssemblyState = &inputAssemblyCi;
        pipeline.pTessellationState = &tessStateCi;
        pipeline.pViewportState = &viewportStateCi;
        pipeline.pRasterizationState = &rasterState;
        pipeline.pMultisampleState = &mssCi;
        pipeline.pDepthStencilState = &depthStencilStateCi;
        pipeline.pColorBlendState = &blendStateCi;
        pipeline.pDynamicState = &dynamicStateCi;
        pipeline.renderPass = renderPass;

        VkPipeline vulkanPso = 0;
        VkResult result = vkCreateGraphicsPipelines( mActiveDevice->mDevice, VK_NULL_HANDLE, 1u,
                                                     &pipeline, 0, &vulkanPso );
        checkVkResult( result, "vkCreateGraphicsPipelines" );

        VulkanHlmsPso *pso =
            new VulkanHlmsPso( vulkanPso, vertexShader, pixelShader, descriptorLayoutBindingSets, sets, layout );
        // pso->pso = vulkanPso;
        // pso->vertexShader = vertexShader;
        // pso->pixelShader = pixelShader;
        // pso->descriptorLayoutBindingSets = descriptorLayoutBindingSets;
        // pso->descriptorSets = sets;

        newPso->rsData = pso;
    }
    //-------------------------------------------------------------------------
    void VulkanRenderSystem::_hlmsPipelineStateObjectDestroyed( HlmsPso *pso )
    {
        assert( pso->rsData );

        VulkanHlmsPso *vulkanPso = reinterpret_cast<VulkanHlmsPso *>( pso->rsData );

        //        removeDepthStencilState( pso );

        vkDestroyPipeline( mActiveDevice->mDevice, reinterpret_cast<VkPipeline>( vulkanPso->pso ), 0 );

        delete vulkanPso;
        pso->rsData = 0;
    }

    void VulkanRenderSystem::_hlmsMacroblockCreated( HlmsMacroblock *newBlock )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " _hlmsMacroblockCreated " ) );
        }
    }

    void VulkanRenderSystem::_hlmsMacroblockDestroyed( HlmsMacroblock *block )
    {
    }

    void VulkanRenderSystem::_hlmsBlendblockCreated( HlmsBlendblock *newBlock )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " _hlmsBlendblockCreated " ) );
        }
    }

    void VulkanRenderSystem::_hlmsBlendblockDestroyed( HlmsBlendblock *block )
    {
    }

    void VulkanRenderSystem::_hlmsSamplerblockCreated( HlmsSamplerblock *newBlock )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " _hlmsSamplerblockCreated " ) );
        }
    }

    void VulkanRenderSystem::_hlmsSamplerblockDestroyed( HlmsSamplerblock *block )
    {
    }

    void VulkanRenderSystem::_descriptorSetTextureCreated( DescriptorSetTexture *newSet )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " _descriptorSetTextureCreated " ) );
        }
    }

    void VulkanRenderSystem::_descriptorSetTextureDestroyed( DescriptorSetTexture *set )
    {
    }

    void VulkanRenderSystem::_descriptorSetTexture2Created( DescriptorSetTexture2 *newSet )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " _descriptorSetTexture2Created " ) );
        }
    }

    void VulkanRenderSystem::_descriptorSetTexture2Destroyed( DescriptorSetTexture2 *set )
    {
    }

    void VulkanRenderSystem::_descriptorSetSamplerCreated( DescriptorSetSampler *newSet )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " _descriptorSetSamplerCreated " ) );
        }
    }

    void VulkanRenderSystem::_descriptorSetSamplerDestroyed( DescriptorSetSampler *set )
    {
    }

    void VulkanRenderSystem::_descriptorSetUavCreated( DescriptorSetUav *newSet )
    {
        Log *defaultLog = LogManager::getSingleton().getDefaultLog();
        if( defaultLog )
        {
            defaultLog->logMessage( String( " _descriptorSetUavCreated " ) );
        }
    }

    void VulkanRenderSystem::_descriptorSetUavDestroyed( DescriptorSetUav *set )
    {
    }
}  // namespace Ogre