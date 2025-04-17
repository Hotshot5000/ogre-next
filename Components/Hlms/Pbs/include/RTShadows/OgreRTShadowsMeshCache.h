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

#ifndef OgreRTShadowsMeshCache_h
#define OgreRTShadowsMeshCache_h

#include "OgreHlmsPbsPrerequisites.h"
#include "OgreIdString.h"
#include <ogrestd/map.h>

#include "OgreHeaderPrefix.h"

namespace Ogre
{
    class _OgreHlmsPbsExport RTShadowsMeshCache
    {
    public:
        struct ShadowsCachedMesh
        {
            uint64      hash[2];
            String      meshName;
            Mesh        *mesh;
            uint32      meshIndex;
        };
    private:
        typedef map<IdString, ShadowsCachedMesh>::type MeshCacheMap;
        typedef map<MeshPtr, ShadowsCachedMesh>::type MeshPtrMap;
        typedef FastArray<MeshPtr> MeshPtrArray;
        typedef FastArray<Item *> ItemArray;
        
        MeshCacheMap mMeshCaches;
        MeshPtrArray mMeshes;
        ItemArray mItems;
        bool mRebuildAS;
        
    public:
        RTShadowsMeshCache();
        ~RTShadowsMeshCache();
        
        /**
        @brief addMeshToCache
            Checks if the mesh is already cached. If it's not, it gets voxelized.
        @param mesh
            Mesh to voxelize
        @param sceneManager
            We need it to temporarily create an Item
        @param refItem
            Reference Item in case we need to copy its materials. Can be nullptr
        @returns
            Entry to VoxelizedMesh in cache
        */
        const ShadowsCachedMesh &addMeshToCache( const MeshPtr &mesh, Item *refItem );
        
        void updateAS();
    };
}

#include "OgreHeaderSuffix.h"

#endif /* OgreRTShadowsMeshCache_h */
