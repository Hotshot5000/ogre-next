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

#include "OgreStableHeaders.h"

#include "PathTracing/OgrePathTracer.h"
#include "RTShadows/OgreRTShadowsMeshCache.h"

#include "Compositor/OgreCompositorNode.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "OgreHlmsCompute.h"
#include "OgreHlmsComputeJob.h"
#include "OgreHlmsManager.h"
#include "OgreHlmsPbsDatablock.h"
#include "OgreItem.h"
#include "OgreLight.h"
#include "OgreLogManager.h"
#include "OgreRenderSystem.h"
#include "OgreRenderSystemCapabilities.h"
#include "OgreSceneManager.h"
#include "OgreSubItem.h"
#include "Vao/OgreConstBufferPacked.h"
#include "Vao/OgreReadOnlyBufferPacked.h"
#include "Vao/OgreVaoManager.h"

namespace Ogre
{
    namespace
    {
        struct PathTracerFrameGpu
        {
            float invProjectionMat[16];
            float invViewMat[16];
            float invViewProjMat[16];
            float cameraCorner[16];
            float cameraPos[4];
            float cameraRight[4];
            float cameraUp[4];
            float cameraFront[4];
            float skyZenith[4];
            float skyHorizon[4];
            float projectionParams[2];
            float width;
            float height;
            uint32 sampleIndex;
            uint32 maxBounces;
            uint32 numLights;
            uint32 flags;
        };

        struct PathTracerLightGpu
        {
            float position[4];
            float diffuse[4];
            float specular[4];
            float attenuation[4];
            float spotDirection[4];
            float spotParams[4];
        };

        struct PathTracerMaterialGpu
        {
            float baseColour_roughness[4];
            float fresnel_transparency[4];
            float emissive_flags[4];
            float padding[4];
        };

        struct PathTracerGeometryGpu
        {
            float material_subMesh[4];
        };

        void copyMatrix( float *dst, const Matrix4 &src )
        {
            memcpy( dst, &src, sizeof( float ) * 16u );
        }

        void addLight( PathTracerLightGpu *dst, Light *light )
        {
            const ColourValue diffuseColour = light->getDiffuseColour() * light->getPowerScale();
            const ColourValue specularColour = light->getSpecularColour() * light->getPowerScale();
            for( size_t i = 0u; i < 3u; ++i )
            {
                dst->diffuse[i] = static_cast<float>( diffuseColour[i] );
                dst->specular[i] = static_cast<float>( specularColour[i] );
            }

            const Vector4 light4dVec = light->getAs4DVector();
            for( size_t i = 0u; i < 4u; ++i )
                dst->position[i] = static_cast<float>( light4dVec[i] );

            dst->attenuation[0] = static_cast<float>( light->getAttenuationRange() );
            dst->attenuation[1] = static_cast<float>( light->getAttenuationLinear() );
            dst->attenuation[2] = static_cast<float>( light->getAttenuationQuadric() );

            const Vector3 spotDirection = light->getDerivedDirectionUpdated();
            dst->spotDirection[0] = static_cast<float>( spotDirection.x );
            dst->spotDirection[1] = static_cast<float>( spotDirection.y );
            dst->spotDirection[2] = static_cast<float>( spotDirection.z );
            dst->spotDirection[3] = static_cast<float>( light->getLightProfileIdx() );

            const Real cosInner = Math::Cos( light->getSpotlightInnerAngle() * 0.5f );
            const Real cosOuter = Math::Cos( light->getSpotlightOuterAngle() * 0.5f );
            const Real invCosDiff = 1.0f / std::max<Real>( cosInner - cosOuter, 1e-4f );
            dst->spotParams[0] = static_cast<float>( invCosDiff );
            dst->spotParams[1] = static_cast<float>( cosOuter );
            dst->spotParams[2] = static_cast<float>( light->getSpotlightFalloff() );
            dst->spotParams[3] = static_cast<float>( light->getType() );
        }
    }

    PathTracer::PathTracer( TextureGpu *renderWindow, RenderSystem *renderSystem,
                            HlmsManager *hlmsManager, Camera *camera,
                            CompositorWorkspace *workspace ) :
        mRenderWindow( renderWindow ),
        mRenderSystem( renderSystem ),
        mVaoManager( renderSystem ? renderSystem->getVaoManager() : 0 ),
        mHlmsManager( hlmsManager ),
        mCamera( camera ),
        mWorkspace( workspace ),
        mMeshCache( 0 ),
        mTraceJob( 0 ),
        mAccumulationTexture( 0 ),
        mRadianceTexture( 0 ),
        mFrameConstBuffer( 0 ),
        mLightsConstBuffer( 0 ),
        mMaterialBuffer( 0 ),
        mGeometryBuffer( 0 ),
        mSampleCount( 0u ),
        mEnabled( false ),
        mInitialized( false )
    {
        initResources();
    }
    //-------------------------------------------------------------------------
    PathTracer::~PathTracer()
    {
        destroyResources();
    }
    //-------------------------------------------------------------------------
    void PathTracer::initResources()
    {
        if( !mRenderSystem || !mVaoManager || !mHlmsManager || !mWorkspace )
            return;

        const bool rayTracingSupported =
            mRenderSystem->getCapabilities()->hasCapability( RSC_RAY_TRACING );
        if( !rayTracingSupported )
        {
            LogManager::getSingleton().logMessage(
                "PathTracer disabled: active RenderSystem does not support ray tracing acceleration structures." );
            return;
        }

        HlmsCompute *hlmsCompute = mHlmsManager->getComputeHlms();
        mTraceJob = hlmsCompute->findComputeJobNoThrow( "PathTracing/TraceJob" );
        if( !mTraceJob )
        {
            LogManager::getSingleton().logMessage(
                "PathTracer disabled: could not find PathTracing/TraceJob." );
            return;
        }

        CompositorNode *pathTracerNode = mWorkspace->findNode( "PathTracerRenderingNode" );
        if( !pathTracerNode )
        {
            LogManager::getSingleton().logMessage(
                "PathTracer disabled: PathTracerRenderingNode is missing from the workspace." );
            return;
        }

        mAccumulationTexture = pathTracerNode->getDefinedTexture( "accumulationTexture" );
        mRadianceTexture = pathTracerNode->getDefinedTexture( "radianceTexture" );
        if( !mAccumulationTexture || !mRadianceTexture )
        {
            LogManager::getSingleton().logMessage(
                "PathTracer disabled: compositor UAV textures are missing." );
            return;
        }

        mFrameConstBuffer = mVaoManager->createConstBuffer( sizeof( PathTracerFrameGpu ),
                                                            BT_DYNAMIC_PERSISTENT, 0, false );
        mLightsConstBuffer = mVaoManager->createConstBuffer( sizeof( PathTracerLightGpu ) * 16u,
                                                             BT_DYNAMIC_PERSISTENT, 0, false );

        mMeshCache = new RTShadowsMeshCache();
        mMeshCache->setEnabled( true );
        mInitialized = true;
    }
    //-------------------------------------------------------------------------
    void PathTracer::destroyResources()
    {
        if( mFrameConstBuffer )
        {
            if( mFrameConstBuffer->getMappingState() != MS_UNMAPPED )
                mFrameConstBuffer->unmap( UO_UNMAP_ALL );
            mVaoManager->destroyConstBuffer( mFrameConstBuffer );
            mFrameConstBuffer = 0;
        }

        if( mLightsConstBuffer )
        {
            if( mLightsConstBuffer->getMappingState() != MS_UNMAPPED )
                mLightsConstBuffer->unmap( UO_UNMAP_ALL );
            mVaoManager->destroyConstBuffer( mLightsConstBuffer );
            mLightsConstBuffer = 0;
        }

        if( mMaterialBuffer )
        {
            if( mMaterialBuffer->getMappingState() != MS_UNMAPPED )
                mMaterialBuffer->unmap( UO_UNMAP_ALL );
            mVaoManager->destroyReadOnlyBuffer( mMaterialBuffer );
            mMaterialBuffer = 0;
        }

        if( mGeometryBuffer )
        {
            if( mGeometryBuffer->getMappingState() != MS_UNMAPPED )
                mGeometryBuffer->unmap( UO_UNMAP_ALL );
            mVaoManager->destroyReadOnlyBuffer( mGeometryBuffer );
            mGeometryBuffer = 0;
        }

        delete mMeshCache;
        mMeshCache = 0;
    }
    //-------------------------------------------------------------------------
    void PathTracer::setEnabled( bool enabled )
    {
        enabled = enabled && mInitialized;
        if( mEnabled == enabled )
            return;

        mEnabled = enabled;
        mScene.setEnabled( enabled );
        resetAccumulation();
    }
    //-------------------------------------------------------------------------
    void PathTracer::resetAccumulation()
    {
        mSampleCount = 0u;
    }
    //-------------------------------------------------------------------------
    void PathTracer::updateAccelerationStructure()
    {
        if( !mMeshCache )
            return;

        if( mScene.needsBlasRebuild() || mScene.needsTlasRebuild() )
        {
            mMeshCache->removeAllItems();

            const PathTracerScene::ItemArray &items = mScene.getItems();
            PathTracerScene::ItemArray::const_iterator itor = items.begin();
            PathTracerScene::ItemArray::const_iterator end = items.end();
            while( itor != end )
            {
                Item *item = *itor;
                mMeshCache->addMeshToCache( item->getMesh(), item );
                ++itor;
            }
        }

        mMeshCache->updateAS();
    }
    //-------------------------------------------------------------------------
    void PathTracer::uploadFrameConstants( uint32 numLights )
    {
        PathTracerFrameGpu *frame = reinterpret_cast<PathTracerFrameGpu *>(
            mFrameConstBuffer->map( 0, mFrameConstBuffer->getNumElements() ) );
        memset( frame, 0, sizeof( PathTracerFrameGpu ) );

        const Matrix4 projMat = mCamera->getProjectionMatrix();
        const Matrix4 viewMat = mCamera->getViewMatrix( true );
        const Matrix4 viewProj = mCamera->getProjectionMatrixWithRSDepth() * viewMat;
        const Matrix4 invProjMat = projMat.inverse();
        const Matrix4 invViewMat = viewMat.inverse();
        const Matrix4 invViewProj = viewProj.inverse();

        copyMatrix( frame->invProjectionMat, invProjMat );
        copyMatrix( frame->invViewMat, invViewMat );
        copyMatrix( frame->invViewProjMat, invViewProj );

        const Vector3 cameraPos = mCamera->getDerivedPosition();
        const Vector3 *corners = mCamera->getWorldSpaceCorners();
        const Vector3 cameraDirs[4] = { corners[5] - cameraPos, corners[6] - cameraPos,
                                        corners[4] - cameraPos, corners[7] - cameraPos };

        for( size_t i = 0u; i < 4u; ++i )
        {
            frame->cameraCorner[i * 4u + 0u] = cameraDirs[i].x;
            frame->cameraCorner[i * 4u + 1u] = cameraDirs[i].y;
            frame->cameraCorner[i * 4u + 2u] = cameraDirs[i].z;
            frame->cameraCorner[i * 4u + 3u] = 1.0f;
        }

        frame->cameraPos[0] = cameraPos.x;
        frame->cameraPos[1] = cameraPos.y;
        frame->cameraPos[2] = cameraPos.z;
        frame->cameraPos[3] = 1.0f;

        const Vector3 cameraRight = mCamera->getRight();
        const Vector3 cameraUp = mCamera->getUp();
        const Vector3 cameraFront = mCamera->getDirection();
        frame->cameraRight[0] = cameraRight.x;
        frame->cameraRight[1] = cameraRight.y;
        frame->cameraRight[2] = cameraRight.z;
        frame->cameraUp[0] = cameraUp.x;
        frame->cameraUp[1] = cameraUp.y;
        frame->cameraUp[2] = cameraUp.z;
        frame->cameraFront[0] = cameraFront.x;
        frame->cameraFront[1] = cameraFront.y;
        frame->cameraFront[2] = cameraFront.z;

        frame->skyZenith[0] = 0.16f;
        frame->skyZenith[1] = 0.35f;
        frame->skyZenith[2] = 0.70f;
        frame->skyZenith[3] = 1.0f;
        frame->skyHorizon[0] = 0.70f;
        frame->skyHorizon[1] = 0.78f;
        frame->skyHorizon[2] = 0.88f;
        frame->skyHorizon[3] = 1.0f;

        Vector2 projectionAB = mCamera->getProjectionParamsAB();
        projectionAB.y /= mCamera->getFarClipDistance();
        frame->projectionParams[0] = projectionAB.x;
        frame->projectionParams[1] = projectionAB.y;
        frame->width = static_cast<float>( mRenderWindow->getWidth() );
        frame->height = static_cast<float>( mRenderWindow->getHeight() );
        frame->sampleIndex = mSampleCount;
        frame->maxBounces = 4u;
        frame->numLights = numLights;
        frame->flags = 0u;

        mFrameConstBuffer->unmap( UO_KEEP_PERSISTENT );
    }
    //-------------------------------------------------------------------------
    uint32 PathTracer::uploadLights( SceneManager *sceneManager )
    {
        PathTracerLightGpu *lightData = reinterpret_cast<PathTracerLightGpu *>(
            mLightsConstBuffer->map( 0, mLightsConstBuffer->getNumElements() ) );
        memset( lightData, 0, mLightsConstBuffer->getTotalSizeBytes() );

        uint32 numCollectedLights = 0u;
        const uint32 maxNumLights =
            static_cast<uint32>( mLightsConstBuffer->getNumElements() / sizeof( PathTracerLightGpu ) );

        ObjectMemoryManager &memoryManager = sceneManager->_getLightMemoryManager();
        const size_t numRenderQueues = memoryManager.getNumRenderQueues();
        for( size_t i = 0u; i < numRenderQueues && numCollectedLights < maxNumLights; ++i )
        {
            ObjectData objData;
            const size_t totalObjs = memoryManager.getFirstObjectData( objData, i );
            for( size_t j = 0u; j < totalObjs && numCollectedLights < maxNumLights;
                 j += ARRAY_PACKED_REALS )
            {
                for( size_t k = 0u; k < ARRAY_PACKED_REALS && j + k < totalObjs &&
                                   numCollectedLights < maxNumLights; ++k )
                {
                    if( objData.mVisibilityFlags[k] & VisibilityFlags::LAYER_VISIBILITY )
                    {
                        Light *light = static_cast<Light *>( objData.mOwner[k] );
                        if( light->getType() == Light::LT_DIRECTIONAL ||
                            light->getType() == Light::LT_POINT ||
                            light->getType() == Light::LT_SPOTLIGHT )
                        {
                            addLight( lightData + numCollectedLights, light );
                            ++numCollectedLights;
                        }
                    }
                }
                objData.advancePack();
            }
        }

        mLightsConstBuffer->unmap( UO_KEEP_PERSISTENT );
        return numCollectedLights;
    }
    //-------------------------------------------------------------------------
    void PathTracer::uploadMaterialBuffer()
    {
        const PathTracerMaterialCache::MaterialRecordArray &materials =
            mScene.getMaterialCache().getMaterials();
        const size_t numMaterials = std::max<size_t>( materials.size(), 1u );
        const size_t bytesNeeded = numMaterials * sizeof( PathTracerMaterialGpu );

        if( !mMaterialBuffer || mMaterialBuffer->getTotalSizeBytes() < bytesNeeded )
        {
            if( mMaterialBuffer )
                mVaoManager->destroyReadOnlyBuffer( mMaterialBuffer );
            mMaterialBuffer = mVaoManager->createReadOnlyBuffer( PFG_RGBA32_FLOAT, bytesNeeded,
                                                                 BT_DYNAMIC_PERSISTENT, 0, false );
        }

        PathTracerMaterialGpu *dst = reinterpret_cast<PathTracerMaterialGpu *>(
            mMaterialBuffer->map( 0, mMaterialBuffer->getNumElements() ) );
        memset( dst, 0, bytesNeeded );

        for( size_t i = 0u; i < materials.size(); ++i )
        {
            const HlmsPbsDatablock *datablock = materials[i].datablock;
            const Vector3 diffuse = datablock->getDiffuse();
            const Vector3 fresnel = datablock->getFresnel();
            const Vector3 emissive = datablock->getEmissive();

            dst[i].baseColour_roughness[0] = diffuse.x;
            dst[i].baseColour_roughness[1] = diffuse.y;
            dst[i].baseColour_roughness[2] = diffuse.z;
            dst[i].baseColour_roughness[3] = datablock->getRoughness();
            dst[i].fresnel_transparency[0] = fresnel.x;
            dst[i].fresnel_transparency[1] = fresnel.y;
            dst[i].fresnel_transparency[2] = fresnel.z;
            dst[i].fresnel_transparency[3] = datablock->getTransparency();
            dst[i].emissive_flags[0] = emissive.x;
            dst[i].emissive_flags[1] = emissive.y;
            dst[i].emissive_flags[2] = emissive.z;
            dst[i].emissive_flags[3] = static_cast<float>( datablock->getTransparencyMode() );
        }

        mMaterialBuffer->unmap( UO_KEEP_PERSISTENT );
    }
    //-------------------------------------------------------------------------
    void PathTracer::uploadGeometryBuffer()
    {
        size_t numGeometryRecords = 0u;
        const PathTracerScene::ItemArray &items = mScene.getItems();
        for( size_t i = 0u; i < items.size(); ++i )
            numGeometryRecords += items[i]->getNumSubItems();

        numGeometryRecords = std::max<size_t>( numGeometryRecords, 1u );
        const size_t bytesNeeded = numGeometryRecords * sizeof( PathTracerGeometryGpu );

        if( !mGeometryBuffer || mGeometryBuffer->getTotalSizeBytes() < bytesNeeded )
        {
            if( mGeometryBuffer )
                mVaoManager->destroyReadOnlyBuffer( mGeometryBuffer );
            mGeometryBuffer = mVaoManager->createReadOnlyBuffer( PFG_RGBA32_FLOAT, bytesNeeded,
                                                                 BT_DYNAMIC_PERSISTENT, 0, false );
        }

        PathTracerGeometryGpu *dst = reinterpret_cast<PathTracerGeometryGpu *>(
            mGeometryBuffer->map( 0, mGeometryBuffer->getNumElements() ) );
        memset( dst, 0, bytesNeeded );

        size_t geometryIdx = 0u;
        for( size_t itemIdx = 0u; itemIdx < items.size(); ++itemIdx )
        {
            Item *item = items[itemIdx];
            for( size_t subItemIdx = 0u; subItemIdx < item->getNumSubItems(); ++subItemIdx )
            {
                uint32 materialIdx = 0u;
                HlmsDatablock *datablock = item->getSubItem( subItemIdx )->getDatablock();
                if( datablock && datablock->getCreator()->getType() == HLMS_PBS )
                {
                    materialIdx = mScene.getMaterialCache().addDatablock(
                        static_cast<HlmsPbsDatablock *>( datablock ) );
                }

                dst[geometryIdx].material_subMesh[0] = static_cast<float>( materialIdx );
                dst[geometryIdx].material_subMesh[1] = static_cast<float>( itemIdx );
                dst[geometryIdx].material_subMesh[2] = static_cast<float>( subItemIdx );
                dst[geometryIdx].material_subMesh[3] = 0.0f;
                ++geometryIdx;
            }
        }

        mGeometryBuffer->unmap( UO_KEEP_PERSISTENT );
    }
    //-------------------------------------------------------------------------
    void PathTracer::bindJobResources()
    {
        mTraceJob->setConstBuffer( 0, mFrameConstBuffer );
        mTraceJob->setConstBuffer( 1, mLightsConstBuffer );

        if( mMaterialBuffer )
        {
            DescriptorSetTexture2::BufferSlot materialSlot(
                DescriptorSetTexture2::BufferSlot::makeEmpty() );
            materialSlot.buffer = mMaterialBuffer;
            mTraceJob->setTexBuffer( 0, materialSlot );
        }

        if( mGeometryBuffer )
        {
            DescriptorSetTexture2::BufferSlot geometrySlot(
                DescriptorSetTexture2::BufferSlot::makeEmpty() );
            geometrySlot.buffer = mGeometryBuffer;
            mTraceJob->setTexBuffer( 1, geometrySlot );
        }

        DescriptorSetUav::TextureSlot accumulationSlot( DescriptorSetUav::TextureSlot::makeEmpty() );
        accumulationSlot.texture = mAccumulationTexture;
        accumulationSlot.access = ResourceAccess::ReadWrite;
        mTraceJob->_setUavTexture( 0, accumulationSlot );

        DescriptorSetUav::TextureSlot radianceSlot( DescriptorSetUav::TextureSlot::makeEmpty() );
        radianceSlot.texture = mRadianceTexture;
        radianceSlot.access = ResourceAccess::Write;
        mTraceJob->_setUavTexture( 1, radianceSlot );

        mTraceJob->analyzeBarriers( mResourceTransitions );
        mRenderSystem->executeResourceTransition( mResourceTransitions );
    }
    //-------------------------------------------------------------------------
    void PathTracer::update( SceneManager *sceneManager )
    {
        if( !mEnabled || !mInitialized )
            return;

        const bool resetNeeded = mScene.needsBlasRebuild() || mScene.needsTlasRebuild() ||
                                 mScene.needsMaterialUpload();
        if( resetNeeded )
            resetAccumulation();

        updateAccelerationStructure();
        const uint32 numLights = uploadLights( sceneManager );
        uploadFrameConstants( numLights );
        if( mScene.needsMaterialUpload() )
            uploadMaterialBuffer();
        if( mScene.needsBlasRebuild() || mScene.needsTlasRebuild() || !mGeometryBuffer )
            uploadGeometryBuffer();
        bindJobResources();

        mScene.clearDirtyFlags();
    }
    //-------------------------------------------------------------------------
    void PathTracer::render()
    {
        if( !mEnabled )
            return;

        ++mSampleCount;
    }
}
