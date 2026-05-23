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

#ifndef OgrePathTracer_h
#define OgrePathTracer_h

#include "OgreHlmsPbsPrerequisites.h"
#include "PathTracing/OgrePathTracerScene.h"

#include "OgreHeaderPrefix.h"

namespace Ogre
{
    class RTShadowsMeshCache;

    class _OgreHlmsPbsExport PathTracer
    {
    private:
        TextureGpu          *mRenderWindow;
        RenderSystem        *mRenderSystem;
        VaoManager          *mVaoManager;
        HlmsManager         *mHlmsManager;
        Camera              *mCamera;
        CompositorWorkspace *mWorkspace;
        PathTracerScene      mScene;
        RTShadowsMeshCache  *mMeshCache;

        HlmsComputeJob      *mTraceJob;
        TextureGpu          *mAccumulationTexture;
        TextureGpu          *mRadianceTexture;
        ConstBufferPacked   *mFrameConstBuffer;
        ConstBufferPacked   *mLightsConstBuffer;
        ReadOnlyBufferPacked *mMaterialBuffer;
        ReadOnlyBufferPacked *mGeometryBuffer;
        ReadOnlyBufferPacked *mTriangleBuffer;
        FastArray<TextureGpu *> mDiffuseTextures;
        FastArray<TextureGpu *> mRoughnessTextures;
        FastArray<TextureGpu *> mNormalTextures;
        FastArray<TextureGpu *> mEmissiveTextures;
        FastArray<TextureGpu *> mReflectionTextures;
        ResourceTransitionArray mResourceTransitions;

        Matrix4              mLastViewMatrix;
        Matrix4              mLastProjectionMatrix;
        uint32               mSampleCount;
        bool                 mHasLastCameraState;
        bool                 mEnabled;
        bool                 mInitialized;

        void initResources();
        void destroyResources();
        void updateAccelerationStructure();
        void uploadFrameConstants( uint32 numLights );
        uint32 uploadLights( SceneManager *sceneManager );
        void uploadMaterialBuffer();
        void uploadGeometryBuffer();
        void bindJobResources();
        void updateTraceJobThreadGroups();

    public:
        PathTracer( TextureGpu *renderWindow, RenderSystem *renderSystem,
                    HlmsManager *hlmsManager, Camera *camera,
                    CompositorWorkspace *workspace );
        ~PathTracer();

        void setEnabled( bool enabled );
        bool getEnabled() const { return mEnabled; }

        PathTracerScene &getScene() { return mScene; }
        const PathTracerScene &getScene() const { return mScene; }

        void resetAccumulation();
        uint32 getSampleCount() const { return mSampleCount; }

        void update( SceneManager *sceneManager );
        void render();
    };
}

#include "OgreHeaderSuffix.h"

#endif /* OgrePathTracer_h */
