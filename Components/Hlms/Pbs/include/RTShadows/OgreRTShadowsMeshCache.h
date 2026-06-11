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
#include "OgreMesh2.h"
#include <ogrestd/map.h>
#include <set>

#include "OgreHeaderPrefix.h"

namespace Ogre
{
    class _OgreHlmsPbsExport RTShadowsMeshCache
    {
    public:
        struct MeshLodRange
        {
            uint32 blasStart;
            uint32 numBlas;
        };

        struct ShadowsCachedMesh
        {
            uint64                  hash[2];
            String                  meshName;
            Mesh                    *mesh;
            MeshPtr                 simplifiedMesh;
            FastArray<uint32>       simplifiedSubMeshToSourceSubMesh;
            FastArray<Aabb>         simplifiedSubMeshBounds;
            MeshPtr                 proxyMesh;
            FastArray<uint32>       proxySubMeshToSourceSubMesh;
            FastArray<Aabb>         proxySubMeshBounds;
            FastArray<MeshLodRange> lodRanges;
        };

        enum RtMeshletTier
        {
            RtMeshletTierFull = 0u,
            RtMeshletTierSimplified = 1u,
            RtMeshletTierProxy = 2u
        };

        struct CandidateSubMeshInstance
        {
            Item         *item;
            Mesh         *mesh;
            SubMesh      *subMesh;
            Aabb          localBounds;
            uint32        subMeshIdx;
            uint32        lodLevel;
            uint32        blasIndex;
            RtMeshletTier tier;
            uint32        availableTiersMask;
        };
        typedef FastArray<CandidateSubMeshInstance> CandidateSubMeshInstanceArray;

        enum GpuCullMode
        {
            GpuCullOff = 0u,
            GpuCullDistance = 1u,
            GpuCullFrustum = 2u,
            GpuCullFrustumAndDistance = 3u
        };

    private:
        typedef map<IdString, ShadowsCachedMesh>::type MeshCacheMap;
        typedef map<MeshPtr, ShadowsCachedMesh>::type MeshPtrMap;
        typedef FastArray<MeshPtr> MeshPtrArray;
        typedef FastArray<Item *> ItemArray;
        typedef std::set<Item *> ItemSet;
        
        MeshCacheMap mMeshCaches;
        MeshPtrArray mMeshes;
        ItemArray mItems;
        CandidateSubMeshInstanceArray mCandidateSubMeshInstances;
        CandidateSubMeshInstanceArray mSelectedSubMeshInstances;
        const Camera *mLodCamera;
        Real mGpuCullDistance;
        Real mGpuCullReflectionConeExpansion;
        GpuCullMode mGpuCullMode;
        uint32 mLastActiveMeshletCount;
        uint32 mLastTotalMeshletCount;
        uint32 mLastFullTierMeshletCount;
        uint32 mLastSimplifiedTierMeshletCount;
        uint32 mLastProxyTierMeshletCount;
        uint32 mLastFullTierObjectCount;
        uint32 mLastSimplifiedTierObjectCount;
        uint32 mLastProxyTierObjectCount;
        uint32 mGeometryRevision;
        bool mRebuildBlas;
        bool mRebuildTlas;
        bool mEnabled;
        
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
        void removeMeshFromCache( Item *item );
        void removeAllItems();
        void markInstancesDirty();

        void setEnabled( bool enabled ) { mEnabled = enabled; }
        bool getEnabled() const { return mEnabled; }
        void setLodCamera( const Camera *camera ) { mLodCamera = camera; }
        void setGpuCullDistance( Real distance );
        Real getGpuCullDistance() const { return mGpuCullDistance; }
        void setGpuCullReflectionConeExpansion( Real expansion );
        Real getGpuCullReflectionConeExpansion() const { return mGpuCullReflectionConeExpansion; }
        void setGpuCullMode( GpuCullMode mode );
        GpuCullMode getGpuCullMode() const { return mGpuCullMode; }
        uint32 getLastActiveMeshletCount() const { return mLastActiveMeshletCount; }
        uint32 getLastTotalMeshletCount() const { return mLastTotalMeshletCount; }
        uint32 getLastFullTierMeshletCount() const { return mLastFullTierMeshletCount; }
        uint32 getLastSimplifiedTierMeshletCount() const { return mLastSimplifiedTierMeshletCount; }
        uint32 getLastProxyTierMeshletCount() const { return mLastProxyTierMeshletCount; }
        uint32 getLastFullTierObjectCount() const { return mLastFullTierObjectCount; }
        uint32 getLastSimplifiedTierObjectCount() const { return mLastSimplifiedTierObjectCount; }
        uint32 getLastProxyTierObjectCount() const { return mLastProxyTierObjectCount; }
        const CandidateSubMeshInstanceArray &getCandidateSubMeshInstances() const
        {
            return mCandidateSubMeshInstances;
        }
        const CandidateSubMeshInstanceArray &getSelectedSubMeshInstances() const
        {
            return mSelectedSubMeshInstances;
        }
        uint32 getGeometryRevision() const { return mGeometryRevision; }
        
        void updateAS();
    };
}

#include "OgreHeaderSuffix.h"

#endif /* OgreRTShadowsMeshCache_h */
