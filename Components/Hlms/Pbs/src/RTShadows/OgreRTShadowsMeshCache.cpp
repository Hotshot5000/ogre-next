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

#include "RTShadows/OgreRTShadowsMeshCache.h"

#include "OgreStableHeaders.h"

#include "OgreRoot.h"
#include "OgreRenderSystem.h"

#include "OgreMesh2.h"
#include "OgreSceneManager.h"
#include "OgreSubMesh2.h"
#include "OgreItem.h"

namespace Ogre
{
    namespace
    {
        uint32 getMeshRtLodCount( const Mesh *mesh )
        {
            const uint32 meshLodCount = std::max<uint32>( mesh->getNumLodLevels(), 1u );
            uint32 lodCount = meshLodCount;
            const unsigned numSubmeshes = mesh->getNumSubMeshes();
            for( unsigned subMeshIdx = 0u; subMeshIdx < numSubmeshes; ++subMeshIdx )
            {
                const SubMesh *subMesh = mesh->getSubMesh( subMeshIdx );
                lodCount = std::min<uint32>( lodCount,
                                             std::max<size_t>( subMesh->mVao[VpNormal].size(), 1u ) );
            }
            return std::max<uint32>( lodCount, 1u );
        }

        bool selectedInstancesDiffer(
            const RTShadowsMeshCache::SelectedSubMeshInstanceArray &a,
            const RTShadowsMeshCache::SelectedSubMeshInstanceArray &b )
        {
            if( a.size() != b.size() )
                return true;

            for( size_t i = 0u; i < a.size(); ++i )
            {
                if( a[i].item != b[i].item || a[i].mesh != b[i].mesh || a[i].subMesh != b[i].subMesh ||
                    a[i].subMeshIdx != b[i].subMeshIdx || a[i].lodLevel != b[i].lodLevel ||
                    a[i].blasIndex != b[i].blasIndex )
                {
                    return true;
                }
            }

            return false;
        }
    }

    RTShadowsMeshCache::RTShadowsMeshCache() :
        mGeometryRevision( 1u ),
        mRebuildBlas( true ),
        mRebuildTlas( true ),
        mEnabled( true )
    {
        
    }
    //-------------------------------------------------------------------------
    RTShadowsMeshCache::~RTShadowsMeshCache()
    {
        
    }
    //-------------------------------------------------------------------------
    const RTShadowsMeshCache::ShadowsCachedMesh &RTShadowsMeshCache::addMeshToCache(
        const MeshPtr &mesh, Item *refItem )
    {
        const String &meshName = mesh->getName();
        MeshCacheMap::iterator itor = mMeshCaches.find( meshName );
        if( itor == mMeshCaches.end() )
        {
            ShadowsCachedMesh shadowCachedMesh;
            shadowCachedMesh.meshName = meshName;
            shadowCachedMesh.mesh = mesh.get();
            itor = mMeshCaches.insert( std::pair<IdString, ShadowsCachedMesh>( meshName, shadowCachedMesh ) ).first;
            mMeshes.push_back( mesh );
            mRebuildBlas = true;
        }
        mItems.push_back( refItem );
        mRebuildTlas = true;
        return itor->second;
    }
    //-------------------------------------------------------------------------
    void RTShadowsMeshCache::removeMeshFromCache( Item *item )
    {
        ItemArray::iterator itor = std::find( mItems.begin(), mItems.end(), item );
        if( itor == mItems.end() )
            OGRE_EXCEPT( Exception::ERR_ITEM_NOT_FOUND, "", "RTShadowsMeshCache::removeMeshFromCache" );

        mItems.erase( itor );
        mRebuildTlas = true;
    }
    //-------------------------------------------------------------------------
    void RTShadowsMeshCache::removeAllItems()
    {
        mItems.clear();
        mRebuildTlas = true;
    }
    //-------------------------------------------------------------------------
    void RTShadowsMeshCache::markInstancesDirty()
    {
        mRebuildTlas = true;
    }
    //-------------------------------------------------------------------------
    void RTShadowsMeshCache::updateAS()
    {
        if( !mEnabled )
            return;

        const bool wasRebuildingBlas = mRebuildBlas;
        std::vector<VertexArrayObject *> meshVaos;
        if( mRebuildBlas )
        {
            uint32 blasIdx = 0u;
            MeshPtrArray::iterator itor = mMeshes.begin();
            MeshPtrArray::iterator end = mMeshes.end();

            while( itor != end )
            {
                const Mesh *mesh = itor->get();
                const unsigned numSubmeshes = mesh->getNumSubMeshes();
                MeshCacheMap::iterator meshCacheIt = mMeshCaches.find( mesh->getName() );
                const uint32 lodCount = getMeshRtLodCount( mesh );
                if( meshCacheIt != mMeshCaches.end() )
                    meshCacheIt->second.lodRanges.clear();

                for( uint32 lodLevel = 0u; lodLevel < lodCount; ++lodLevel )
                {
                    MeshLodRange lodRange;
                    lodRange.blasStart = blasIdx;
                    lodRange.numBlas = numSubmeshes;

                    for( unsigned subMeshIdx = 0u; subMeshIdx < numSubmeshes; ++subMeshIdx )
                    {
                        SubMesh *subMesh = mesh->getSubMesh( subMeshIdx );
                        const size_t vaoLod = std::min<size_t>( lodLevel, subMesh->mVao[VpNormal].size() - 1u );
                        VertexArrayObject *vao = subMesh->mVao[VpNormal][vaoLod];
                        meshVaos.push_back( vao );
                        ++blasIdx;
                    }

                    if( meshCacheIt != mMeshCaches.end() )
                        meshCacheIt->second.lodRanges.push_back( lodRange );
                }

                ++itor;
            }
        }
        
        SelectedSubMeshInstanceArray selectedSubMeshInstances;
        std::vector<uint32> instanceMeshIndex;
        std::vector<Matrix4> instanceTransform;
        ItemArray::iterator itemItor = mItems.begin();
        ItemArray::iterator itemEnd = mItems.end();
        
        while( itemItor != itemEnd )
        {
            Item *item = *itemItor;
            Mesh *mesh = item->getMesh().get();
            MeshCacheMap::iterator meshCacheIt = mMeshCaches.find( mesh->getName() );
            if( meshCacheIt != mMeshCaches.end() && !meshCacheIt->second.lodRanges.empty() )
            {
                const uint32 lodLevel = std::min<uint32>( item->getCurrentMeshLod(),
                                                          meshCacheIt->second.lodRanges.size() - 1u );
                const MeshLodRange &lodRange = meshCacheIt->second.lodRanges[lodLevel];
                const Matrix4 transform = item->getParentSceneNode()->_getFullTransformUpdated();
                const uint32 numSubMeshes = std::min<uint32>( item->getNumSubItems(), lodRange.numBlas );
                for( uint32 subMeshIdx = 0u; subMeshIdx < numSubMeshes; ++subMeshIdx )
                {
                    SelectedSubMeshInstance selectedInstance;
                    selectedInstance.item = item;
                    selectedInstance.mesh = mesh;
                    selectedInstance.subMesh = mesh->getSubMesh( static_cast<unsigned>( subMeshIdx ) );
                    selectedInstance.subMeshIdx = subMeshIdx;
                    selectedInstance.lodLevel = lodLevel;
                    selectedInstance.blasIndex = lodRange.blasStart + subMeshIdx;
                    selectedSubMeshInstances.push_back( selectedInstance );

                    instanceMeshIndex.push_back( selectedInstance.blasIndex );
                    instanceTransform.push_back( transform );
                }
            }
            
            ++itemItor;
        }

        const bool selectionChanged = selectedInstancesDiffer( mSelectedSubMeshInstances, selectedSubMeshInstances );
        if( selectionChanged )
        {
            mSelectedSubMeshInstances.swap( selectedSubMeshInstances );
            mRebuildTlas = true;
            ++mGeometryRevision;
        }
        else
        {
            mSelectedSubMeshInstances.swap( selectedSubMeshInstances );
        }

        if( wasRebuildingBlas )
            ++mGeometryRevision;
        
        RenderSystem *renderSystem = Root::getSingleton().getRenderSystem();
        if( instanceMeshIndex.empty() )
        {
            renderSystem->clearAccelerationStructure();
            mRebuildTlas = true;
            return;
        }

        if( mRebuildBlas )
        {
            renderSystem->createAccelerationStructure( mMeshes, meshVaos, instanceMeshIndex, instanceTransform );
            mRebuildBlas = false;
            mRebuildTlas = false;
        }
        else if( mRebuildTlas )
        {
            renderSystem->rebuildAccelerationStructure( instanceMeshIndex, instanceTransform );
            mRebuildTlas = false;
        }
        else
        {
            renderSystem->refitAccelerationStructure( instanceMeshIndex, instanceTransform );
        }
    }
}
