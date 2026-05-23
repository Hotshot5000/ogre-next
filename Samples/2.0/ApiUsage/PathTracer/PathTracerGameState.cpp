#include "PathTracerGameState.h"
#include "CameraController.h"
#include "GraphicsSystem.h"

#include "OgreItem.h"
#include "OgreSceneManager.h"

#include "OgreMesh2.h"
#include "OgreMeshManager.h"
#include "OgreMeshManager2.h"

#include "OgreCamera.h"

#include "OgreHlmsPbsDatablock.h"

#include "OgreHlmsManager.h"
#include "OgreHlmsPbs.h"
#include "OgreRoot.h"

#include "../LocalCubemaps/LocalCubemapScene.h"

#include "OgreTextureFilters.h"
#include "OgreTextureGpuManager.h"

#include "OgreForward3D.h"
#include "PathTracing/OgrePathTracer.h"

#include "OgreWindow.h"

using namespace Demo;

namespace Demo
{
    PathTracerGameState::PathTracerGameState( const Ogre::String &helpDescription ) :
        TutorialGameState( helpDescription ),
        mPathTracer( 0 ),
        mAnimateObjects( true )
    {
        mDisplayHelpMode = 2;
        mNumDisplayHelpModes = 3;

        memset( mMaterials, 0, sizeof( mMaterials ) );
    }
    //-----------------------------------------------------------------------------------
    void PathTracerGameState::createScene01()
    {
        Ogre::SceneManager *sceneManager = mGraphicsSystem->getSceneManager();
        sceneManager->setForwardClustered( true, 16, 8, 24, 96, 0, 0, 5, 500 );

        Ogre::RenderSystem *renderSystem = mGraphicsSystem->getRoot()->getRenderSystem();
        Ogre::HlmsManager *hlmsManager = mGraphicsSystem->getRoot()->getHlmsManager();
        Ogre::Camera *camera = mGraphicsSystem->getCamera();

        mPathTracer = new Ogre::PathTracer( mGraphicsSystem->getRenderWindow()->getTexture(),
                                            renderSystem, hlmsManager, camera,
                                            mGraphicsSystem->getCompositorWorkspace() );
        mPathTracer->setEnabled( true );

        assert( dynamic_cast<Ogre::HlmsPbs *>( hlmsManager->getHlms( Ogre::HLMS_PBS ) ) );

        const float armsLength = 2.5f;

        Ogre::v1::MeshPtr planeMeshV1 = Ogre::v1::MeshManager::getSingleton().createPlane(
            "PathTracer Plane v1", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
            Ogre::Plane( Ogre::Vector3::UNIT_Y, 1.0f ), 50.0f, 50.0f, 1, 1, true, 1, 4.0f, 4.0f,
            Ogre::Vector3::UNIT_Z, Ogre::v1::HardwareBuffer::HBU_STATIC,
            Ogre::v1::HardwareBuffer::HBU_STATIC );

        Ogre::MeshPtr planeMesh = Ogre::MeshManager::getSingleton().createByImportingV1(
            "PathTracer Plane", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
            planeMeshV1.get(), true, true, true );

        {
            Ogre::Item *item = sceneManager->createItem( planeMesh, Ogre::SCENE_DYNAMIC );
            item->setDatablock( "Marble" );
            Ogre::SceneNode *sceneNode = sceneManager->getRootSceneNode( Ogre::SCENE_DYNAMIC )
                                             ->createChildSceneNode( Ogre::SCENE_DYNAMIC );
            sceneNode->setPosition( 0, -1, 0 );
            sceneNode->attachObject( item );

            assert( dynamic_cast<Ogre::HlmsPbsDatablock *>( item->getSubItem( 0 )->getDatablock() ) );
            Ogre::HlmsPbsDatablock *datablock =
                static_cast<Ogre::HlmsPbsDatablock *>( item->getSubItem( 0 )->getDatablock() );

            Ogre::HlmsSamplerblock samplerblock( *datablock->getSamplerblock( Ogre::PBSM_ROUGHNESS ) );
            samplerblock.mU = Ogre::TAM_WRAP;
            samplerblock.mV = Ogre::TAM_WRAP;
            samplerblock.mW = Ogre::TAM_WRAP;
            datablock->setSamplerblock( Ogre::PBSM_ROUGHNESS, samplerblock );

            mPathTracer->getScene().addItem( item );
        }

        for( int i = 0; i < 4; ++i )
        {
            for( int j = 0; j < 4; ++j )
            {
                Ogre::String meshName;

                if( i == j )
                    meshName = "Sphere1000.mesh";
                else
                    meshName = "Cube_d.mesh";

                Ogre::Item *item = sceneManager->createItem(
                    meshName, Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME,
                    Ogre::SCENE_DYNAMIC );
                if( i % 2 == 0 )
                    item->setDatablock( "Rocks" );
                else
                    item->setDatablock( "Marble" );

                item->setVisibilityFlags( 0x000000001 );

                const size_t idx = static_cast<size_t>( i * 4 + j );

                mSceneNode[idx] = sceneManager->getRootSceneNode( Ogre::SCENE_DYNAMIC )
                                      ->createChildSceneNode( Ogre::SCENE_DYNAMIC );

                mSceneNode[idx]->setPosition( ( Ogre::Real( i ) - 1.5f ) * armsLength, 2.0f,
                                              ( Ogre::Real( j ) - 1.5f ) * armsLength );
                mSceneNode[idx]->setScale( 0.65f, 0.65f, 0.65f );

                mSceneNode[idx]->roll( Ogre::Radian( (Ogre::Real)idx ) );

                mSceneNode[idx]->attachObject( item );
                mPathTracer->getScene().addItem( item );
            }
        }

        {
            mNumSpheres = 0;
            Ogre::HlmsPbs *hlmsPbs =
                static_cast<Ogre::HlmsPbs *>( hlmsManager->getHlms( Ogre::HLMS_PBS ) );

            const int numX = 8;
            const int numZ = 8;

            const float armsLengthSphere = 1.0f;
            const float startX = ( numX - 1 ) / 2.0f;
            const float startZ = ( numZ - 1 ) / 2.0f;

            Ogre::Root *root = mGraphicsSystem->getRoot();
            Ogre::TextureGpuManager *textureMgr = root->getRenderSystem()->getTextureGpuManager();

            for( int x = 0; x < numX; ++x )
            {
                for( int z = 0; z < numZ; ++z )
                {
                    Ogre::String datablockName =
                        "PathTracerTest" + Ogre::StringConverter::toString( mNumSpheres++ );
                    Ogre::HlmsPbsDatablock *datablock = static_cast<Ogre::HlmsPbsDatablock *>(
                        hlmsPbs->createDatablock( datablockName, datablockName, Ogre::HlmsMacroblock(),
                                                  Ogre::HlmsBlendblock(), Ogre::HlmsParamVec() ) );

                    Ogre::TextureGpu *texture = textureMgr->createOrRetrieveTexture(
                        "SaintPetersBasilica.dds", Ogre::GpuPageOutStrategy::Discard,
                        Ogre::TextureFlags::PrefersLoadingFromFileAsSRGB, Ogre::TextureTypes::TypeCube,
                        Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME,
                        Ogre::TextureFilter::TypeGenerateDefaultMipmaps );

                    datablock->setTexture( Ogre::PBSM_REFLECTION, texture );
                    datablock->setDiffuse( Ogre::Vector3( 0.0f, 1.0f, 0.0f ) );
                    datablock->setRoughness(
                        std::max( 0.02f, float( x ) / std::max( 1.0f, (float)( numX - 1 ) ) ) );
                    datablock->setFresnel(
                        Ogre::Vector3( float( z ) / std::max( 1.0f, (float)( numZ - 1 ) ) ), false );

                    Ogre::Item *item = sceneManager->createItem(
                        "Sphere1000.mesh", Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME,
                        Ogre::SCENE_DYNAMIC );
                    item->setDatablock( datablock );
                    item->setVisibilityFlags( 0x000000002 );

                    Ogre::SceneNode *sceneNode = sceneManager->getRootSceneNode( Ogre::SCENE_DYNAMIC )
                                                     ->createChildSceneNode( Ogre::SCENE_DYNAMIC );
                    sceneNode->setPosition(
                        Ogre::Vector3( armsLengthSphere * Ogre::Real( x ) - startX, 1.0f,
                                       armsLengthSphere * Ogre::Real( z ) - startZ ) );
                    sceneNode->attachObject( item );

                    mPathTracer->getScene().addItem( item );
                }
            }
        }

        Ogre::SceneNode *rootNode = sceneManager->getRootSceneNode();

        Ogre::Light *light = sceneManager->createLight();
        Ogre::SceneNode *lightNode = rootNode->createChildSceneNode();
        lightNode->attachObject( light );
        light->setPowerScale( 1.0f );
        light->setType( Ogre::Light::LT_DIRECTIONAL );
        light->setDirection( Ogre::Vector3( -1, -1, -1 ).normalisedCopy() );

        mLightNodes[0] = lightNode;

        sceneManager->setAmbientLight( Ogre::ColourValue( 0.3f, 0.5f, 0.7f ) * 0.1f * 0.75f,
                                       Ogre::ColourValue( 0.6f, 0.45f, 0.3f ) * 0.065f * 0.75f,
                                       -light->getDirection() + Ogre::Vector3::UNIT_Y * 0.2f );

        mGraphicsSystem->createAtmosphere( light );

        light = sceneManager->createLight();
        lightNode = rootNode->createChildSceneNode();
        lightNode->attachObject( light );
        light->setDiffuseColour( 0.8f, 0.4f, 0.2f );
        light->setSpecularColour( 0.8f, 0.4f, 0.2f );
        light->setPowerScale( Ogre::Math::PI );
        light->setType( Ogre::Light::LT_SPOTLIGHT );
        lightNode->setPosition( -10.0f, 10.0f, 10.0f );
        light->setDirection( Ogre::Vector3( 1, -1, -1 ).normalisedCopy() );
        light->setAttenuationBasedOnRadius( 10.0f, 0.01f );

        mLightNodes[1] = lightNode;

        light = sceneManager->createLight();
        lightNode = rootNode->createChildSceneNode();
        lightNode->attachObject( light );
        light->setDiffuseColour( 0.2f, 0.4f, 0.8f );
        light->setSpecularColour( 0.2f, 0.4f, 0.8f );
        light->setPowerScale( Ogre::Math::PI );
        light->setType( Ogre::Light::LT_SPOTLIGHT );
        lightNode->setPosition( 10.0f, 10.0f, -10.0f );
        light->setDirection( Ogre::Vector3( -1, -1, 1 ).normalisedCopy() );
        light->setAttenuationBasedOnRadius( 10.0f, 0.01f );

        mLightNodes[2] = lightNode;

        mCameraController = new CameraController( mGraphicsSystem, false );
        mCameraController->mCameraBaseSpeed = 1.0f;
        mCameraController->mCameraSpeedBoost = 10.0f;

        TutorialGameState::createScene01();
    }
    //-----------------------------------------------------------------------------------
    void PathTracerGameState::destroyScene()
    {
        delete mPathTracer;
        mPathTracer = 0;
    }
    //-----------------------------------------------------------------------------------
    void PathTracerGameState::update( float timeSinceLast )
    {
        if( mAnimateObjects )
        {
            for( int i = 0; i < 16; ++i )
                mSceneNode[i]->yaw( Ogre::Radian( timeSinceLast * float( i ) * 0.125f ) );

            if( mPathTracer )
                mPathTracer->getScene().markInstancesDirty();
        }

        TutorialGameState::update( timeSinceLast );

        if( mPathTracer )
        {
            mPathTracer->update( mGraphicsSystem->getSceneManager() );
            mPathTracer->render();
        }
    }
    //-----------------------------------------------------------------------------------
    void PathTracerGameState::generateDebugText( float timeSinceLast, Ogre::String &outText )
    {
        Ogre::uint32 visibilityMask = mGraphicsSystem->getSceneManager()->getVisibilityMask();

        TutorialGameState::generateDebugText( timeSinceLast, outText );
        outText += "\nPath tracer samples: ";
        outText += Ogre::StringConverter::toString( mPathTracer ? mPathTracer->getSampleCount() : 0u );
        outText += "\nPress F2 to toggle animation. ";
        outText += mAnimateObjects ? "[On]" : "[Off]";
        outText += "\nPress F3 to show/hide animated objects. ";
        outText += ( visibilityMask & 0x000000001 ) ? "[On]" : "[Off]";
        outText += "\nPress F4 to show/hide palette of spheres. ";
        outText += ( visibilityMask & 0x000000002 ) ? "[On]" : "[Off]";
        outText += "\nPress F5 to toggle transparency mode. ";
        outText += mTransparencyMode == Ogre::HlmsPbsDatablock::Fade ? "[Fade]" : "[Transparent]";
        outText += "\n+/- to change transparency. [";
        outText += Ogre::StringConverter::toString( mTransparencyValue ) + "]";
    }
    //-----------------------------------------------------------------------------------
    void PathTracerGameState::setTransparencyToMaterials()
    {
        Ogre::HlmsManager *hlmsManager = mGraphicsSystem->getRoot()->getHlmsManager();

        assert( dynamic_cast<Ogre::HlmsPbs *>( hlmsManager->getHlms( Ogre::HLMS_PBS ) ) );

        Ogre::HlmsPbs *hlmsPbs = static_cast<Ogre::HlmsPbs *>( hlmsManager->getHlms( Ogre::HLMS_PBS ) );

        Ogre::HlmsPbsDatablock::TransparencyModes mode =
            static_cast<Ogre::HlmsPbsDatablock::TransparencyModes>( mTransparencyMode );

        if( mTransparencyValue >= 1.0f )
            mode = Ogre::HlmsPbsDatablock::None;

        if( mTransparencyMode < 1.0f && mode == Ogre::HlmsPbsDatablock::None )
            mode = Ogre::HlmsPbsDatablock::Transparent;

        for( size_t i = 0; i < mNumSpheres; ++i )
        {
            Ogre::String datablockName = "PathTracerTest" + Ogre::StringConverter::toString( i );
            Ogre::HlmsPbsDatablock *datablock =
                static_cast<Ogre::HlmsPbsDatablock *>( hlmsPbs->getDatablock( datablockName ) );

            datablock->setTransparency( mTransparencyValue, mode );
        }

        if( mPathTracer )
            mPathTracer->getScene().markMaterialsDirty();
    }
    //-----------------------------------------------------------------------------------
    void PathTracerGameState::keyReleased( const SDL_KeyboardEvent &arg )
    {
        if( ( arg.keysym.mod & ~( KMOD_NUM | KMOD_CAPS ) ) != 0 )
        {
            TutorialGameState::keyReleased( arg );
            return;
        }

        if( arg.keysym.sym == SDLK_F2 )
        {
            mAnimateObjects = !mAnimateObjects;
        }
        else if( arg.keysym.sym == SDLK_F3 )
        {
            Ogre::uint32 visibilityMask = mGraphicsSystem->getSceneManager()->getVisibilityMask();
            bool showMovingObjects = ( visibilityMask & 0x00000001 );
            showMovingObjects = !showMovingObjects;
            visibilityMask &= static_cast<uint32_t>( ~0x00000001 );
            visibilityMask |= (Ogre::uint32)showMovingObjects;
            mGraphicsSystem->getSceneManager()->setVisibilityMask( visibilityMask );
        }
        else if( arg.keysym.sym == SDLK_F4 )
        {
            Ogre::uint32 visibilityMask = mGraphicsSystem->getSceneManager()->getVisibilityMask();
            bool showPalette = ( visibilityMask & 0x00000002 ) != 0;
            showPalette = !showPalette;
            visibilityMask &= static_cast<uint32_t>( ~0x00000002 );
            visibilityMask |= ( Ogre::uint32 )( showPalette ) << 1;
            mGraphicsSystem->getSceneManager()->setVisibilityMask( visibilityMask );
        }
        else if( arg.keysym.sym == SDLK_F5 )
        {
            mTransparencyMode = mTransparencyMode == Ogre::HlmsPbsDatablock::Fade
                                    ? Ogre::HlmsPbsDatablock::Transparent
                                    : Ogre::HlmsPbsDatablock::Fade;
            if( mTransparencyValue != 1.0f )
                setTransparencyToMaterials();
        }
        else if( arg.keysym.scancode == SDL_SCANCODE_KP_PLUS )
        {
            if( mTransparencyValue < 1.0f )
            {
                mTransparencyValue += 0.1f;
                mTransparencyValue = std::min( mTransparencyValue, 1.0f );
                setTransparencyToMaterials();
            }
        }
        else if( arg.keysym.scancode == SDL_SCANCODE_MINUS ||
                 arg.keysym.scancode == SDL_SCANCODE_KP_MINUS )
        {
            if( mTransparencyValue > 0.0f )
            {
                mTransparencyValue -= 0.1f;
                mTransparencyValue = std::max( mTransparencyValue, 0.0f );
                setTransparencyToMaterials();
            }
        }
        else
        {
            TutorialGameState::keyReleased( arg );
        }
    }
}  // namespace Demo
