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

#ifndef OgreRTShadows_h
#define OgreRTShadows_h

#include "OgreHlmsPbsPrerequisites.h"

#include "Compositor/OgreCompositorWorkspaceListener.h"

#include "OgreHeaderPrefix.h"

namespace Ogre
{
    class RTShadowsMeshCache;
    
    class _OgreHlmsPbsExport RTShadows : public CompositorWorkspaceListener
    {
    private:
        typedef FastArray<Item *> ItemArray;
        ItemArray  mItems;
        RTShadowsMeshCache *mMeshCache;
        
        SceneManager       *mSceneManager;
        CompositorManager2 *mCompositorManager;
        RenderSystem       *mRenderSystem;
        VaoManager         *mVaoManager;
        HlmsManager        *mHlmsManager;
        
        HlmsComputeJob *mShadowIntersectionJob;
        
        UavBufferPacked *mShadowTex;
        
        bool mFirstBuild;
    public:
        RTShadows( RenderSystem *renderSystem, HlmsManager *hlmsManager );
        ~RTShadows();
        /** Adds all items in SceneManager that match visibilityFlags
        @param sceneManager
            SceneManager ptr
        @param visibilityFlags
            Visibility Mask to exclude Items
        @param bStatic
            True to add only static Items
            False to add only dynamic Items
        */
        void addAllItems( SceneManager *sceneManager, const uint32 visibilityFlags, const bool bStatic );
        void removeAllItems();
        
        void addItem( Item *item );
        void removeItem( Item *item );
        
        void init();
        
        void updateAS();
        
        void update( SceneManager *sceneManager );
        
        /// CompositorWorkspaceListener override
        void allWorkspacesBeforeBeginUpdate() override;
        
        /// Register against the CompositorManager to call RTShadows::update
        /// automatically when the CompositorManager is about to render (recommended)
        ///
        /// You still must call setCameraPosition every frame though (or whenever the camera
        /// changes)
        ///
        /// Set to nullptr to disable auto update
        void setAutoUpdate( CompositorManager2 *compositorManager, SceneManager *sceneManager );
        
        RTShadowsMeshCache *getMeshCache() { return mMeshCache; }
    };
}

#include "OgreHeaderSuffix.h"


#endif /* OgreRTShadows_h */
