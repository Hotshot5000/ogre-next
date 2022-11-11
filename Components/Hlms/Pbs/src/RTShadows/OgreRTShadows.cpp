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


namespace Ogre
{
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
    }
    //-------------------------------------------------------------------------
    RTShadows::~RTShadows()
    {
        if( mMeshCache )
            delete mMeshCache;
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
        Ogre::Matrix4 viewProj = mCamera->getProjectionMatrix() * mCamera->Frustum::getViewMatrix();
        
//        DescriptorSetTexture2::TextureSlot texSlot(
//            DescriptorSetTexture2::TextureSlot::makeEmpty() );
//        texSlot.texture = mDepthTexture;
//        mShadowIntersectionJob->setTexture( 0, texSlot );
        
        DescriptorSetUav::TextureSlot bufferSlot( DescriptorSetUav::TextureSlot::makeEmpty() );
        bufferSlot.texture = mShadowTexture;
        bufferSlot.access = ResourceAccess::Write;
        mShadowIntersectionJob->_setUavTexture( 0, bufferSlot );
        
        

        ShaderParams &shaderParams = mShadowIntersectionJob->getShaderParams( "default" );
        shaderParams.mParams.clear();
        shaderParams.mParams.push_back( ShaderParams::Param() );
        ShaderParams::Param *p = &shaderParams.mParams.back();
        p->name = "projectionParams";
        p->setManualValue( viewProj );
        shaderParams.setDirty();

        mShadowIntersectionJob->analyzeBarriers( mResourceTransitions );
        mRenderSystem->executeResourceTransition( mResourceTransitions );
        hlmsCompute->dispatch( mShadowIntersectionJob, 0, 0 );
    }
}
