/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-present Torus Knot Software Ltd

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

#include "OgreVulkanDevice.h"

#include "OgreVulkanRenderSystem.h"
#include "OgreVulkanWindow.h"
#include "Vao/OgreVulkanVaoManager.h"

#include "OgreException.h"
#include "OgreStringConverter.h"

#include "OgreVulkanUtils.h"

#include <vulkan/vulkan.h>

#define TODO_findRealPresentQueue

namespace Ogre
{

    struct ExtensionHeader  // Helper struct to link extensions together
    {
        VkStructureType sType;
        void *pNext;
    };

    VulkanDevice::VulkanDevice( VkInstance instance, uint32 deviceIdx,
                                VulkanRenderSystem *renderSystem ) :
        mInstance( instance ),
        mPhysicalDevice( 0 ),
        mDevice( 0 ),
        mPresentQueue( 0 ),
        mVaoManager( 0 ),
        mRenderSystem( renderSystem ),
        mSupportedStages( 0xFFFFFFFF )
    {
        memset( &mDeviceMemoryProperties, 0, sizeof( mDeviceMemoryProperties ) );
        createPhysicalDevice( deviceIdx );
    }
    //-------------------------------------------------------------------------
    VulkanDevice::~VulkanDevice()
    {
        if( mDevice )
        {
            vkDeviceWaitIdle( mDevice );

            mGraphicsQueue.destroy();
            destroyQueues( mComputeQueues );
            destroyQueues( mTransferQueues );

            // Must be done externally (this' destructor has yet to free more Vulkan stuff)
            // vkDestroyDevice( mDevice, 0 );
            mDevice = 0;
            mPhysicalDevice = 0;
        }
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::destroyQueues( FastArray<VulkanQueue> &queueArray )
    {
        FastArray<VulkanQueue>::iterator itor = queueArray.begin();
        FastArray<VulkanQueue>::iterator endt = queueArray.end();

        while( itor != endt )
        {
            itor->destroy();
            ++itor;
        }
        queueArray.clear();
    }
    //-------------------------------------------------------------------------
    VkDebugReportCallbackCreateInfoEXT VulkanDevice::addDebugCallback(
        PFN_vkDebugReportCallbackEXT debugCallback, RenderSystem *renderSystem )
    {
        // This is info for a temp callback to use during CreateInstance.
        // After the instance is created, we use the instance-based
        // function to register the final callback.
        VkDebugReportCallbackCreateInfoEXT dbgCreateInfoTemp;
        makeVkStruct( dbgCreateInfoTemp, VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT );
        dbgCreateInfoTemp.pfnCallback = debugCallback;
        dbgCreateInfoTemp.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
        dbgCreateInfoTemp.pUserData = renderSystem;
        return dbgCreateInfoTemp;
    }
    //-------------------------------------------------------------------------
    VkInstance VulkanDevice::createInstance( const String &appName, FastArray<const char *> &extensions,
                                             FastArray<const char *> &layers,
                                             PFN_vkDebugReportCallbackEXT debugCallback,
                                             RenderSystem *renderSystem )
    {
        VkInstanceCreateInfo createInfo;
        VkApplicationInfo appInfo;
        memset( &createInfo, 0, sizeof( createInfo ) );
        memset( &appInfo, 0, sizeof( appInfo ) );

        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        if( !appName.empty() )
            appInfo.pApplicationName = appName.c_str();
        appInfo.pEngineName = "Ogre3D Vulkan Engine";
        appInfo.engineVersion = OGRE_VERSION;
        appInfo.apiVersion = VK_MAKE_VERSION( 1, 1, 0 );

        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        createInfo.enabledLayerCount = static_cast<uint32>( layers.size() );
        createInfo.ppEnabledLayerNames = layers.begin();

        extensions.push_back( VK_KHR_SURFACE_EXTENSION_NAME );

        createInfo.enabledExtensionCount = static_cast<uint32>( extensions.size() );
        createInfo.ppEnabledExtensionNames = extensions.begin();

#if OGRE_DEBUG_MODE >= OGRE_DEBUG_HIGH
        VkDebugReportCallbackCreateInfoEXT debugCb = addDebugCallback( debugCallback, renderSystem );
        createInfo.pNext = &debugCb;
#endif

        VkInstance instance;
        VkResult result = vkCreateInstance( &createInfo, 0, &instance );
        checkVkResult( result, "vkCreateInstance" );

        return instance;
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::createPhysicalDevice( uint32 deviceIdx )
    {
        VkResult result = VK_SUCCESS;

        uint32 numDevices = 0u;
        result = vkEnumeratePhysicalDevices( mInstance, &numDevices, NULL );
        checkVkResult( result, "vkEnumeratePhysicalDevices" );

        if( numDevices == 0u )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, "No Vulkan devices found.",
                         "VulkanDevice::createPhysicalDevice" );
        }

        const String numDevicesStr = StringConverter::toString( numDevices );
        String deviceIdsStr = StringConverter::toString( deviceIdx );

        LogManager::getSingleton().logMessage( "[Vulkan] Found " + numDevicesStr + " devices" );

        if( deviceIdx >= numDevices )
        {
            LogManager::getSingleton().logMessage( "[Vulkan] Requested device index " + deviceIdsStr +
                                                   " but there's only " +
                                                   StringConverter::toString( numDevices ) + "devices" );
            deviceIdx = 0u;
            deviceIdsStr = "0";
        }

        LogManager::getSingleton().logMessage( "[Vulkan] Selecting device " + deviceIdsStr );

        FastArray<VkPhysicalDevice> pd;
        pd.resize( numDevices );
        result = vkEnumeratePhysicalDevices( mInstance, &numDevices, pd.begin() );
        checkVkResult( result, "vkEnumeratePhysicalDevices" );
        mPhysicalDevice = pd[deviceIdx];

        vkGetPhysicalDeviceMemoryProperties( mPhysicalDevice, &mDeviceMemoryProperties );
        vkGetPhysicalDeviceFeatures( mPhysicalDevice, &mDeviceFeatures );        

        mSupportedStages = 0xFFFFFFFF;
        if( !mDeviceFeatures.geometryShader )
            mSupportedStages ^= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
        if( !mDeviceFeatures.tessellationShader )
        {
            mSupportedStages ^= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
                                VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
        }
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::findGraphicsQueue( FastArray<uint32> &inOutUsedQueueCount )
    {
        const size_t numQueues = mQueueProps.size();
        for( size_t i = 0u; i < numQueues; ++i )
        {
            if( mQueueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT &&
                inOutUsedQueueCount[i] < mQueueProps[i].queueCount )
            {
                mGraphicsQueue.setQueueData( this, VulkanQueue::Graphics, static_cast<uint32>( i ),
                                             inOutUsedQueueCount[i] );
                ++inOutUsedQueueCount[i];
                return;
            }
        }

        OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                     "GPU does not expose Graphics queue. Cannot be used for rendering",
                     "VulkanQueue::findGraphicsQueue" );
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::findComputeQueue( FastArray<uint32> &inOutUsedQueueCount, uint32 maxNumQueues )
    {
        const size_t numQueues = mQueueProps.size();
        for( size_t i = 0u; i < numQueues && mComputeQueues.size() < maxNumQueues; ++i )
        {
            if( mQueueProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT &&
                inOutUsedQueueCount[i] < mQueueProps[i].queueCount )
            {
                mComputeQueues.push_back( VulkanQueue() );
                mComputeQueues.back().setQueueData( this, VulkanQueue::Compute, static_cast<uint32>( i ),
                                                    inOutUsedQueueCount[i] );
                ++inOutUsedQueueCount[i];
            }
        }
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::findTransferQueue( FastArray<uint32> &inOutUsedQueueCount, uint32 maxNumQueues )
    {
        const size_t numQueues = mQueueProps.size();
        for( size_t i = 0u; i < numQueues && mTransferQueues.size() < maxNumQueues; ++i )
        {
            if( mQueueProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT &&
                !( mQueueProps[i].queueFlags & ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT ) ) &&
                inOutUsedQueueCount[i] < mQueueProps[i].queueCount )
            {
                mTransferQueues.push_back( VulkanQueue() );
                mTransferQueues.back().setQueueData( this, VulkanQueue::Transfer,
                                                     static_cast<uint32>( i ), inOutUsedQueueCount[i] );
                ++inOutUsedQueueCount[i];
            }
        }
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::fillQueueCreationInfo( uint32 maxComputeQueues, uint32 maxTransferQueues,
                                              FastArray<VkDeviceQueueCreateInfo> &outQueueCiArray )
    {
        const size_t numQueueFamilies = mQueueProps.size();

        FastArray<uint32> usedQueueCount;
        usedQueueCount.resize( numQueueFamilies, 0u );

        findGraphicsQueue( usedQueueCount );
        findComputeQueue( usedQueueCount, maxComputeQueues );
        findTransferQueue( usedQueueCount, maxTransferQueues );

        VkDeviceQueueCreateInfo queueCi;
        makeVkStruct( queueCi, VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO );

        for( size_t i = 0u; i < numQueueFamilies; ++i )
        {
            queueCi.queueFamilyIndex = static_cast<uint32>( i );
            queueCi.queueCount = usedQueueCount[i];
            if( queueCi.queueCount > 0u )
                outQueueCiArray.push_back( queueCi );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::createDevice( FastArray<const char *> &extensions, uint32 maxComputeQueues,
                                     uint32 maxTransferQueues )
    {
        uint32 numQueues;
        vkGetPhysicalDeviceQueueFamilyProperties( mPhysicalDevice, &numQueues, NULL );
        if( numQueues == 0u )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, "Vulkan device is reporting 0 queues!",
                         "VulkanDevice::createDevice" );
        }
        mQueueProps.resize( numQueues );
        vkGetPhysicalDeviceQueueFamilyProperties( mPhysicalDevice, &numQueues, &mQueueProps[0] );

        // Setup queue creation
        FastArray<VkDeviceQueueCreateInfo> queueCreateInfo;
        fillQueueCreationInfo( maxComputeQueues, maxTransferQueues, queueCreateInfo );

        FastArray<FastArray<float> > queuePriorities;
        queuePriorities.resize( queueCreateInfo.size() );

        for( size_t i = 0u; i < queueCreateInfo.size(); ++i )
        {
            queuePriorities[i].resize( queueCreateInfo[i].queueCount, 1.0f );
            queueCreateInfo[i].pQueuePriorities = queuePriorities[i].begin();
        }

        makeVkStruct( mDeviceFeatures2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 );
        makeVkStruct( mDeviceProperties2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 );        

        VkPhysicalDeviceMultiviewFeatures multiview;
        makeVkStruct( multiview, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES );        
        VkPhysicalDevice16BitStorageFeatures t16BitStorage;
        makeVkStruct( t16BitStorage, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES );
        VkPhysicalDeviceSamplerYcbcrConversionFeatures samplerYcbcrConversion;
        makeVkStruct( samplerYcbcrConversion,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES );
        VkPhysicalDeviceProtectedMemoryFeatures protectedMemory;
        makeVkStruct( protectedMemory, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES );
        VkPhysicalDeviceShaderDrawParameterFeatures drawParameters;
        makeVkStruct( drawParameters, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETER_FEATURES );
        VkPhysicalDeviceVariablePointerFeatures variablePointers;
        makeVkStruct( variablePointers, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTER_FEATURES );
        
        multiview.pNext = &t16BitStorage;
        t16BitStorage.pNext = &samplerYcbcrConversion;
        samplerYcbcrConversion.pNext = &protectedMemory;
        protectedMemory.pNext = &drawParameters;
        drawParameters.pNext = &variablePointers;
        variablePointers.pNext = 0;

        VkPhysicalDeviceMaintenance3Properties maintenance3;
        makeVkStruct( maintenance3, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES );
        VkPhysicalDeviceIDProperties deviceID{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
        makeVkStruct( deviceID, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES );
        VkPhysicalDeviceMultiviewProperties multiviewProp;
        makeVkStruct( multiviewProp, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES );
        VkPhysicalDeviceProtectedMemoryProperties protectedMemoryProp;
        makeVkStruct( protectedMemoryProp, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES );
        VkPhysicalDevicePointClippingProperties pointClipping;
        makeVkStruct( pointClipping, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES );
        VkPhysicalDeviceSubgroupProperties subgroup;
        makeVkStruct( subgroup, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES );
        makeVkStruct( mRTPipelineProps,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR );

        maintenance3.pNext = &deviceID;
        deviceID.pNext = &multiviewProp;
        multiviewProp.pNext = &protectedMemoryProp;
        protectedMemoryProp.pNext = &pointClipping;
        pointClipping.pNext = &subgroup;
        subgroup.pNext = &mRTPipelineProps;
        mRTPipelineProps.pNext = 0;


        mDeviceFeatures2.pNext = &multiview;
        mDeviceProperties2.pNext = &maintenance3;

        extensions.push_back( VK_KHR_SWAPCHAIN_EXTENSION_NAME );
        extensions.push_back( VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME );
        extensions.push_back( VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME );
        extensions.push_back( VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME );
        extensions.push_back( VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME );
        VkPhysicalDeviceAccelerationStructureFeaturesKHR physicalDeviceAS;
        makeVkStruct( physicalDeviceAS,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR );
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature;
        makeVkStruct( rtPipelineFeature,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR );

        extensions.push_back( VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME );
        extensions.push_back( VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME );

        extensions.push_back( VK_KHR_MAINTENANCE3_EXTENSION_NAME );
        extensions.push_back( VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME );
        extensions.push_back( VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME );
        extensions.push_back( VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME );

        extensions.push_back( VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME );
        extensions.push_back( VK_KHR_SPIRV_1_4_EXTENSION_NAME );
        extensions.push_back( VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME );

        vkGetPhysicalDeviceFeatures2( mPhysicalDevice, &mDeviceFeatures2 );
        vkGetPhysicalDeviceProperties2( mPhysicalDevice, &mDeviceProperties2 );

        VkDeviceCreateInfo createInfo;
        makeVkStruct( createInfo, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO );

        createInfo.enabledExtensionCount = static_cast<uint32>( extensions.size() );
        createInfo.ppEnabledExtensionNames = extensions.begin();

        createInfo.queueCreateInfoCount = static_cast<uint32>( queueCreateInfo.size() );
        createInfo.pQueueCreateInfos = &queueCreateInfo[0];

        createInfo.pEnabledFeatures = 0;
        createInfo.pNext = &mDeviceFeatures2;

        variablePointers.pNext = &physicalDeviceAS;
        physicalDeviceAS.pNext = &rtPipelineFeature;
        rtPipelineFeature.pNext = 0;

        VkResult result = vkCreateDevice( mPhysicalDevice, &createInfo, NULL, &mDevice );
        checkVkResult( result, "vkCreateDevice" );

        initUtils( mDevice );
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::initQueues( void )
    {
        VkQueue queue = 0;
        vkGetDeviceQueue( mDevice, mGraphicsQueue.mFamilyIdx, mGraphicsQueue.mQueueIdx, &queue );
        mGraphicsQueue.init( mDevice, queue, mRenderSystem );

        FastArray<VulkanQueue>::iterator itor = mComputeQueues.begin();
        FastArray<VulkanQueue>::iterator endt = mComputeQueues.end();

        while( itor != endt )
        {
            vkGetDeviceQueue( mDevice, itor->mFamilyIdx, itor->mQueueIdx, &queue );
            itor->init( mDevice, queue, mRenderSystem );
            ++itor;
        }

        itor = mTransferQueues.begin();
        endt = mTransferQueues.end();

        while( itor != endt )
        {
            vkGetDeviceQueue( mDevice, itor->mFamilyIdx, itor->mQueueIdx, &queue );
            itor->init( mDevice, queue, mRenderSystem );
            ++itor;
        }

        TODO_findRealPresentQueue;
        mPresentQueue = mGraphicsQueue.mQueue;
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::commitAndNextCommandBuffer( SubmissionType::SubmissionType submissionType )
    {
        mGraphicsQueue.endAllEncoders();
        mGraphicsQueue.commitAndNextCommandBuffer( submissionType );
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::stall( void )
    {
        // We must flush the cmd buffer and our bindings because we take the
        // moment to delete all delayed buffers and API handles after a stall.
        //
        // We can't have potentially dangling API handles in a cmd buffer.
        // We must submit our current pending work so far and wait until that's done.
        commitAndNextCommandBuffer( SubmissionType::FlushOnly );
        mRenderSystem->resetAllBindings();

        vkDeviceWaitIdle( mDevice );

        mRenderSystem->_notifyDeviceStalled();
    }
    //-------------------------------------------------------------------------
    VkDeviceAddress VulkanDevice::getDeviceAddress( VkBuffer buf )
    {
        VkBufferDeviceAddressInfo bufInfo;
        makeVkStruct( bufInfo, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO );
        return vkGetBufferDeviceAddress( mDevice, &bufInfo );
    }
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    VulkanDevice::SelectedQueue::SelectedQueue() :
        usage( VulkanQueue::NumQueueFamilies ),
        familyIdx( std::numeric_limits<uint32>::max() ),
        queueIdx( 0 )
    {
    }
}  // namespace Ogre
