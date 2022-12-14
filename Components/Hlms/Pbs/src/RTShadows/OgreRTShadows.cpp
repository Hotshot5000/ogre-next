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

#include "RTShadows/OgreRTShadows.h"
#include "RTShadows/OgreRTShadowsMeshCache.h"
#include "Math/Array/OgreBooleanMask.h"
#include "OgreItem.h"
#include "OgreSceneManager.h"
#include "Compositor/OgreCompositorManager2.h"
#include "OgreHlmsCompute.h"
#include "OgreHlmsComputeJob.h"
#include "OgreWindow.h"
#include "Vao/OgreVaoManager.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "Compositor/OgreCompositorNode.h"
#include "Vao/OgreConstBufferPacked.h"


namespace Ogre
{
    struct RTInput
    {
        float invProjectionMat[16];
        float invViewMat[16];
        float cameraPos[4];
        float projectionParams[2];
        float width;
        float height;
        
//        float cameraDir[16];
        
        
        
    };
    
    struct RTLight
    {
        float position[4];    //.w contains the objLightMask
        float diffuse[4];        //.w contains numNonCasterDirectionalLights
        float specular[3];

        float attenuation[3];
        //Spotlights:
        //  spotDirection.xyz is direction
        //  spotParams.xyz contains falloff params
        float spotDirection[4];
        float spotParams[4];
    };
    
    RTShadows::RTShadows( TextureGpu *renderWindow, RenderSystem *renderSystem, HlmsManager *hlmsManager, Camera *camera,
                         CompositorWorkspace *workspace) :
        mMeshCache( 0 ),
        mSceneManager( 0 ),
        mCompositorManager( 0 ),
        mRenderSystem( renderSystem ),
        mVaoManager( renderSystem->getVaoManager() ),
        mHlmsManager( hlmsManager ),
        mCamera( camera ),
        mWorkspace( workspace ),
        mRenderWindow( renderWindow ),
        mShadowIntersectionJob( 0 ),
        mLightsConstBuffer( 0 ),
        mFirstBuild( true )
    {
        HlmsCompute *hlmsCompute = mHlmsManager->getComputeHlms();

        mShadowIntersectionJob = hlmsCompute->findComputeJobNoThrow( "RT/IntersectionTestJob" );

#if OGRE_NO_JSON
        OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                     "To use RTShadows, Ogre must be build with JSON support "
                     "and you must include the resources bundled at "
                     "Samples/Media/RT",
                     "RTShadows::RTShadows" );
#endif
        if( !mShadowIntersectionJob )
        {
            OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                         "To use RTShadows, you must include the resources bundled at "
                         "Samples/Media/RT\n"
                         "Could not find RT/IntersectionTestJob",
                         "RTShadows::RTShadows" );
        }
        
        //uint32 textureSize = mRenderWindow->getWidth() * mRenderWindow->getHeight();
        
//        char tmpBuffer[128];
//        LwString texName( LwString::FromEmptyPointer( tmpBuffer, sizeof( tmpBuffer ) ) );
//        texName.a( "VctLighting_", names[i], "/Id", getId() );
//        TextureGpu *texture = textureManager->createTexture(
//            texName.c_str(), GpuPageOutStrategy::Discard, texFlags, TextureTypes::Type3D );
//        if( i == 0u )
//        {
//            texture->setResolution( width, height, depth );
//            texture->setNumMipmaps( numMipsMain );
//        }
//        else
//        {
//            texture->setResolution( widthAniso, heightAniso, depthAniso );
//            texture->setNumMipmaps( numMipsAniso );
//        }
//        texture->setPixelFormat( PFG_RGBA8_UNORM_SRGB );
//        texture->scheduleTransitionTo( GpuResidency::Resident );
        
//        mShadowTex = mVaoManager->createUavBuffer( textureSize, 4,
//                                                  BP_TYPE_UAV, 0, false );
        
        Ogre::CompositorNode *shadowsNode = workspace->findNode( "RayTracedShadowsRenderingNode" );
        mShadowTexture = shadowsNode->getDefinedTexture( "shadowTexture" );
        mDepthTexture = shadowsNode->getDefinedTexture( "gBufferDepthBuffer" );
        
        mLightsConstBuffer = mVaoManager->createConstBuffer( sizeof( RTLight ) * 16u,
                                                            BT_DYNAMIC_PERSISTENT, 0, false );
        mInputDataConstBuffer = mVaoManager->createConstBuffer( sizeof( RTInput ),
                                                            BT_DYNAMIC_PERSISTENT, 0, false );
    }
    //-------------------------------------------------------------------------
    RTShadows::~RTShadows()
    {
        if( mMeshCache )
            delete mMeshCache;
        
        if( mLightsConstBuffer )
        {
            if( mLightsConstBuffer->getMappingState() != MS_UNMAPPED )
                mLightsConstBuffer->unmap( UO_UNMAP_ALL );
            mVaoManager->destroyConstBuffer( mLightsConstBuffer );
            mLightsConstBuffer = 0;
        }
        if( mInputDataConstBuffer )
        {
            if( mInputDataConstBuffer->getMappingState() != MS_UNMAPPED )
                mInputDataConstBuffer->unmap( UO_UNMAP_ALL );
            mVaoManager->destroyConstBuffer( mInputDataConstBuffer );
            mInputDataConstBuffer = 0;
        }
    }
    //-------------------------------------------------------------------------
    void RTShadows::addAllItems( SceneManager *sceneManager,
                                            const uint32 sceneVisibilityFlags, const bool bStatic )
    {
        // TODO: We should be able to keep a list of Items ourselves, and in
        // VctCascadedVoxelizer::update when a voxelizer is moved, we detect
        // which items go into that voxelizer. What's faster? who knows

        Ogre::ObjectMemoryManager &objMemoryManager =
            sceneManager->_getEntityMemoryManager( bStatic ? SCENE_STATIC : SCENE_DYNAMIC );

        const ArrayInt sceneFlags = Mathlib::SetAll( sceneVisibilityFlags );

        const size_t numRenderQueues = objMemoryManager.getNumRenderQueues();

        for( size_t i = 0u; i < numRenderQueues; ++i )
        {
            Ogre::ObjectData objData;
            const size_t totalObjs = objMemoryManager.getFirstObjectData( objData, i );

            for( size_t j = 0; j < totalObjs; j += ARRAY_PACKED_REALS )
            {
                ArrayInt *RESTRICT_ALIAS visibilityFlags =
                    reinterpret_cast<ArrayInt * RESTRICT_ALIAS>( objData.mVisibilityFlags );

                ArrayMaskI isVisible = Mathlib::And(
                    Mathlib::TestFlags4( *visibilityFlags,
                                         Mathlib::SetAll( VisibilityFlags::LAYER_VISIBILITY ) ),
                    Mathlib::And( sceneFlags, *visibilityFlags ) );

                const uint32 scalarMask = BooleanMask4::getScalarMask( isVisible );

                for( size_t k = 0; k < ARRAY_PACKED_REALS; ++k )
                {
                    if( IS_BIT_SET( k, scalarMask ) )
                    {
                        // UGH! We're relying on dynamic_cast. TODO: Refactor it.
                        Item *item = dynamic_cast<Item *>( objData.mOwner[k] );
                        if( item )
                        {
                            addItem( item );
                        }
                    }
                }

                objData.advancePack();
            }
        }

        mFirstBuild = true;
    }
    //-------------------------------------------------------------------------
    void RTShadows::removeAllItems()
    {
        mItems.clear();
    }
    //-------------------------------------------------------------------------
    void RTShadows::addItem(Item *item)
    {
        mItems.push_back(item);
    }
    //-------------------------------------------------------------------------
    void RTShadows::removeItem(Item *item)
    {
        ItemArray::iterator itor = std::find( mItems.begin(), mItems.end(), item );
        if( itor == mItems.end() )
            OGRE_EXCEPT( Exception::ERR_ITEM_NOT_FOUND, "", "VctImageVoxelizer::removeItem" );

        
        mItems.erase( itor );
    }
    //-------------------------------------------------------------------------
    void RTShadows::init()
    {
        mMeshCache = new RTShadowsMeshCache();
    }
    //-------------------------------------------------------------------------
    void RTShadows::updateAS()
    {
        
    }
    //-------------------------------------------------------------------------
    void RTShadows::setAutoUpdate( CompositorManager2 *compositorManager,
                                              SceneManager *sceneManager )
    {
        if( compositorManager && !mCompositorManager )
        {
            mSceneManager = sceneManager;
            mCompositorManager = compositorManager;
            compositorManager->addListener( this );
        }
        else if( !compositorManager && mCompositorManager )
        {
            mCompositorManager->removeListener( this );
            mSceneManager = 0;
            mCompositorManager = 0;
        }
    }
    //-------------------------------------------------------------------------
    void RTShadows::allWorkspacesBeforeBeginUpdate()
    {
        update( mSceneManager );
    }
    //-------------------------------------------------------------------------
    void RTShadows::update( SceneManager *sceneManager )
    {
        HlmsCompute *hlmsCompute = mHlmsManager->getComputeHlms();
        Ogre::Matrix4 invViewProj = (mCamera->getProjectionMatrix() * mCamera->Frustum::getViewMatrix()).inverse();
        Ogre::Matrix4 invProjMat = mCamera->getProjectionMatrix().inverse();
        Ogre::Matrix4 invViewMat = mCamera->Frustum::getViewMatrix().inverse();
        
//        DescriptorSetTexture2::TextureSlot texSlot(
//            DescriptorSetTexture2::TextureSlot::makeEmpty() );
//        texSlot.texture = mDepthTexture;
//        mShadowIntersectionJob->setTexture( 0, texSlot );
        
        DescriptorSetUav::TextureSlot bufferSlot( DescriptorSetUav::TextureSlot::makeEmpty() );
        bufferSlot.texture = mShadowTexture;
        bufferSlot.access = ResourceAccess::Write;
        mShadowIntersectionJob->_setUavTexture( 0, bufferSlot );
        
        

//        ShaderParams &shaderParams = mShadowIntersectionJob->getShaderParams( "default" );
//        shaderParams.mParams.clear();
//        shaderParams.mParams.push_back( ShaderParams::Param() );
//        ShaderParams::Param *p = &shaderParams.mParams.back();
//        p->name = "projectionParams";
//        p->setManualValue( viewProj );
//        shaderParams.setDirty();
//
        Ogre::Vector2 projectionAB = mCamera->getProjectionParamsAB();
        // The division will keep "linearDepth" in the shader in the [0; 1] range.
        projectionAB.y /= mCamera->getFarClipDistance();
//        shaderParams.mParams.push_back( ShaderParams::Param() );
//        p = &shaderParams.mParams.back();
//        p->name = "projectionParams";
//        p->setManualValue( Ogre::Vector4( projectionAB.x, projectionAB.y, 0, 0 ) );
//        shaderParams.setDirty();
        
        mShadowIntersectionJob->setConstBuffer( 0, mLightsConstBuffer );
        
        RTLight *RESTRICT_ALIAS rtLight = reinterpret_cast<RTLight *>(
            mLightsConstBuffer->map( 0, mLightsConstBuffer->getNumElements() ) );
        uint32 numCollectedLights = 0;
        const uint32 maxNumLights =
            static_cast<uint32>( mLightsConstBuffer->getNumElements() / sizeof( RTLight ) );
        
        // TODO Remove this when struct gets filled.
        memset(rtLight, 0, mLightsConstBuffer->getNumElements());

//        const uint32 lightMask = _lightMask & VisibilityFlags::RESERVED_VISIBILITY_FLAGS;

        ObjectMemoryManager &memoryManager = sceneManager->_getLightMemoryManager();
        const size_t numRenderQueues = memoryManager.getNumRenderQueues();
        
        for( size_t i = 0; i < numRenderQueues; ++i )
        {
            ObjectData objData;
            const size_t totalObjs = memoryManager.getFirstObjectData( objData, i );

            for( size_t j = 0; j < totalObjs && numCollectedLights < maxNumLights;
                 j += ARRAY_PACKED_REALS )
            {
                for( size_t k = 0; k < ARRAY_PACKED_REALS && numCollectedLights < maxNumLights; ++k )
                {
                    uint32 *RESTRICT_ALIAS visibilityFlags = objData.mVisibilityFlags;

                    if( visibilityFlags[k] & VisibilityFlags::LAYER_VISIBILITY /*&&
                        visibilityFlags[k] & lightMask*/ )
                    {
                        Light *light = static_cast<Light *>( objData.mOwner[k] );
                        if( light->getType() == Light::LT_DIRECTIONAL ||
                            light->getType() == Light::LT_POINT ||
                            light->getType() == Light::LT_SPOTLIGHT ||
                            light->getType() == Light::LT_AREA_APPROX ||
                            light->getType() == Light::LT_AREA_LTC )
                        {
                            addLight( rtLight, light );
//                            autoMultiplierValue = std::max( autoMultiplierValue, maxVal );
                            ++rtLight;
                            ++numCollectedLights;
                        }
                    }
                }

                objData.advancePack();
            }
        }

        mLightsConstBuffer->unmap( UO_KEEP_PERSISTENT );
        
        mShadowIntersectionJob->setConstBuffer( 1, mInputDataConstBuffer );
        
        RTInput *RESTRICT_ALIAS rtInput = reinterpret_cast<RTInput *>(
            mInputDataConstBuffer->map( 0, mInputDataConstBuffer->getNumElements() ) );
        
        const Ogre::Vector3 cameraPos = mCamera->getDerivedPosition();
//        const Ogre::Vector3 cameraDir = mCamera->getRealDirection();
        const Vector3 *corners = mCamera->getWorldSpaceCorners();
        
        Vector3 cameraDirs[4];
        cameraDirs[0] = corners[5] - cameraPos;
        cameraDirs[1] = corners[6] - cameraPos;
        cameraDirs[2] = corners[4] - cameraPos;
        cameraDirs[3] = corners[7] - cameraPos;
        
        rtInput->projectionParams[0] = projectionAB.x;
        rtInput->projectionParams[1] = projectionAB.y;
        
        //Camera Pos
        rtInput->cameraPos[0] = cameraPos.x;
        rtInput->cameraPos[1] = cameraPos.y;
        rtInput->cameraPos[2] = cameraPos.z;
        
        //Camera Dir
//        rtInput->cameraDir[0] = cameraDirs[0].x;
//        rtInput->cameraDir[1] = cameraDirs[0].y;
//        rtInput->cameraDir[2] = cameraDirs[0].z;
//
//        rtInput->cameraDir[3] = cameraDirs[1].x;
//        rtInput->cameraDir[4] = cameraDirs[1].y;
//        rtInput->cameraDir[5] = cameraDirs[1].z;
//
//        rtInput->cameraDir[6] = cameraDirs[2].x;
//        rtInput->cameraDir[7] = cameraDirs[2].y;
//        rtInput->cameraDir[8] = cameraDirs[2].z;
//
//        rtInput->cameraDir[9] = cameraDirs[3].x;
//        rtInput->cameraDir[10] = cameraDirs[3].y;
//        rtInput->cameraDir[11] = cameraDirs[3].z;
        
        rtInput->width = mRenderWindow->getWidth();
        rtInput->height = mRenderWindow->getHeight();
        
        memcpy( rtInput->invProjectionMat, &invProjMat, 4 * 4 * sizeof( float ) );
        memcpy( rtInput->invViewMat, &invViewMat, 4 * 4 * sizeof( float ) );
        
        mInputDataConstBuffer->unmap( UO_KEEP_PERSISTENT );

        mShadowIntersectionJob->analyzeBarriers( mResourceTransitions );
        mRenderSystem->executeResourceTransition( mResourceTransitions );
        hlmsCompute->dispatch( mShadowIntersectionJob, 0, 0 );
    }
    //-------------------------------------------------------------------------
    void RTShadows::addLight( RTLight *RESTRICT_ALIAS vctLight, Light *light )
    {
        const ColourValue diffuseColour = light->getDiffuseColour() * light->getPowerScale();
        for( size_t i = 0; i < 3u; ++i )
            vctLight->diffuse[i] = static_cast<float>( diffuseColour[i] );

//        const Vector4 *lightDistThreshold =
//            light->getCustomParameterNoThrow( msDistanceThresholdCustomParam );
//        vctLight->diffuse[3] = lightDistThreshold
//                                   ? ( lightDistThreshold->x * lightDistThreshold->x )
//                                   : ( mDefaultLightDistThreshold * mDefaultLightDistThreshold );
        
        Light::LightTypes lightType = light->getType();
        if( lightType == Light::LT_AREA_APPROX )
            lightType = Light::LT_AREA_LTC;

        Vector4 light4dVec = light->getAs4DVector();

        for( size_t i = 0; i < 4u; ++i )
            vctLight->position[i] = static_cast<float>( light4dVec[i] );
    }
}
