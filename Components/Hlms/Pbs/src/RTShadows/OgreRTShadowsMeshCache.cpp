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
#include "OgreCamera.h"
#include "OgreRenderSystem.h"

#include "OgreMesh2.h"
#include "OgreMeshManager2.h"
#include "OgreResourceGroupManager.h"
#include "OgreSceneManager.h"
#include "OgreStringConverter.h"
#include "OgreSubMesh2.h"
#include "OgreItem.h"
#include "Vao/OgreAsyncTicket.h"
#include "Vao/OgreIndexBufferPacked.h"
#include "Vao/OgreVaoManager.h"
#include "Vao/OgreVertexArrayObject.h"
#include "Vao/OgreVertexBufferDownloadHelper.h"
#include "Vao/OgreVertexBufferPacked.h"

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

        uint32 readIndexAt( const uint8 *indexData, IndexBufferPacked *indexBuffer, size_t indexIdx )
        {
            if( indexBuffer->getIndexType() == IndexBufferPacked::IT_16BIT )
                return reinterpret_cast<const uint16 *>( indexData )[indexIdx];
            return reinterpret_cast<const uint32 *>( indexData )[indexIdx];
        }

        Aabb calculateSubMeshBounds( SubMesh *subMesh, const Aabb &fallbackBounds )
        {
            if( subMesh->mVao[VpNormal].empty() )
                return fallbackBounds;

            VertexArrayObject *vao = subMesh->mVao[VpNormal].front();
            VertexElementSemanticFullArray semanticsToDownload;
            semanticsToDownload.push_back( VES_POSITION );

            VertexBufferDownloadHelper downloadHelper;
            downloadHelper.queueDownload( vao, semanticsToDownload, 0u, 0u );
            const VertexBufferDownloadHelper::DownloadData *downloadData =
                downloadHelper.getDownloadData().data();
            if( !downloadData || !downloadData[0].origElements )
                return fallbackBounds;

            uint8 const *srcData[1];
            downloadHelper.map( srcData );
            if( !srcData[0] )
            {
                downloadHelper.unmap();
                return fallbackBounds;
            }

            IndexBufferPacked *indexBuffer = vao->getIndexBuffer();
            AsyncTicketPtr indexTicket;
            const uint8 *indexData = 0;
            if( indexBuffer )
            {
                if( indexBuffer->getShadowCopy() )
                {
                    indexData = reinterpret_cast<const uint8 *>( indexBuffer->getShadowCopy() ) +
                                vao->getPrimitiveStart() * indexBuffer->getBytesPerElement();
                }
                else
                {
                    indexTicket = indexBuffer->readRequest( vao->getPrimitiveStart(),
                                                            vao->getPrimitiveCount() );
                    indexData = reinterpret_cast<const uint8 *>( indexTicket->map() );
                }
            }

            Vector3 minCorner( Math::POS_INFINITY, Math::POS_INFINITY, Math::POS_INFINITY );
            Vector3 maxCorner( Math::NEG_INFINITY, Math::NEG_INFINITY, Math::NEG_INFINITY );
            bool hasVertex = false;
            const uint32 indexCount = vao->getPrimitiveCount();
            const size_t vertexCount = vao->getBaseVertexBuffer()->getNumElements();
            const uint32 iterations = indexBuffer ? indexCount : static_cast<uint32>( vertexCount );
            for( uint32 idx = 0u; idx < iterations; ++idx )
            {
                const uint32 vertexIdx = indexBuffer ? readIndexAt( indexData, indexBuffer, idx ) : idx;
                if( vertexIdx >= vertexCount )
                    continue;

                const uint8 *vertexData = srcData[0] + downloadData[0].srcOffset +
                                          vertexIdx * downloadData[0].srcBytesPerVertex;
                const Vector4 pos4 = VertexBufferDownloadHelper::getVector4(
                    vertexData, *downloadData[0].origElements );
                const Vector3 pos( pos4.x, pos4.y, pos4.z );
                minCorner.makeFloor( pos );
                maxCorner.makeCeil( pos );
                hasVertex = true;
            }

            if( indexTicket )
                indexTicket->unmap();
            downloadHelper.unmap();

            if( !hasVertex )
                return fallbackBounds;

            Aabb bounds;
            bounds.setExtents( minCorner, maxCorner );
            if( bounds.mHalfSize.squaredLength() <= Real( 1e-8 ) )
                bounds.mHalfSize = Vector3( 0.5f, 0.5f, 0.5f );
            return bounds;
        }

        VertexArrayObject *createBoxVao( const Aabb &bounds, VaoManager *vaoManager )
        {
            Vector3 halfSize = bounds.mHalfSize;
            if( halfSize.squaredLength() <= Real( 1e-8 ) )
                halfSize = Vector3( 0.5f, 0.5f, 0.5f );

            const Vector3 center = bounds.mCenter;
            const Vector3 vertices[8] = {
                center + Vector3( -halfSize.x, -halfSize.y,  halfSize.z ),
                center + Vector3(  halfSize.x, -halfSize.y,  halfSize.z ),
                center + Vector3(  halfSize.x,  halfSize.y,  halfSize.z ),
                center + Vector3( -halfSize.x,  halfSize.y,  halfSize.z ),
                center + Vector3( -halfSize.x, -halfSize.y, -halfSize.z ),
                center + Vector3(  halfSize.x, -halfSize.y, -halfSize.z ),
                center + Vector3(  halfSize.x,  halfSize.y, -halfSize.z ),
                center + Vector3( -halfSize.x,  halfSize.y, -halfSize.z )
            };

            const uint16 indices[36] = {
                2, 1, 0, 0, 3, 2,
                4, 5, 6, 6, 7, 4,
                6, 2, 3, 3, 7, 6,
                0, 1, 5, 5, 4, 0,
                3, 0, 4, 4, 7, 3,
                1, 2, 6, 6, 5, 1
            };

            VertexElement2Vec vertexElements;
            vertexElements.push_back( VertexElement2( VET_FLOAT3, VES_POSITION ) );
            VertexBufferPacked *vertexBuffer = vaoManager->createVertexBuffer(
                vertexElements, 8u, BT_IMMUTABLE,
                reinterpret_cast<void *>( const_cast<Vector3 *>( vertices ) ), false );
            IndexBufferPacked *indexBuffer = vaoManager->createIndexBuffer(
                IndexBufferPacked::IT_16BIT, 36u, BT_IMMUTABLE,
                reinterpret_cast<void *>( const_cast<uint16 *>( indices ) ), false );
            VertexBufferPackedVec vertexBuffers( 1u, vertexBuffer );
            return vaoManager->createVertexArrayObject( vertexBuffers, indexBuffer, OT_TRIANGLE_LIST );
        }

        const uint32 RtMeshletTargetTriangles = 192u;
        const uint32 RtMeshletMaxPerSubMesh = 32u;

        void calculateSubMeshMeshletBounds( SubMesh *subMesh, const Aabb &fallbackBounds,
                                            FastArray<Aabb> &meshletBounds )
        {
            meshletBounds.clear();
            if( subMesh->mVao[VpNormal].empty() )
            {
                meshletBounds.push_back( fallbackBounds );
                return;
            }

            VertexArrayObject *vao = subMesh->mVao[VpNormal].front();
            const uint32 indexCount = vao->getPrimitiveCount();
            const uint32 triangleCount = indexCount / 3u;
            if( triangleCount <= RtMeshletTargetTriangles )
            {
                meshletBounds.push_back( calculateSubMeshBounds( subMesh, fallbackBounds ) );
                return;
            }

            VertexElementSemanticFullArray semanticsToDownload;
            semanticsToDownload.push_back( VES_POSITION );

            VertexBufferDownloadHelper downloadHelper;
            downloadHelper.queueDownload( vao, semanticsToDownload, 0u, 0u );
            const VertexBufferDownloadHelper::DownloadData *downloadData =
                downloadHelper.getDownloadData().data();
            if( !downloadData || !downloadData[0].origElements )
            {
                meshletBounds.push_back( fallbackBounds );
                return;
            }

            uint8 const *srcData[1];
            downloadHelper.map( srcData );
            if( !srcData[0] )
            {
                downloadHelper.unmap();
                meshletBounds.push_back( fallbackBounds );
                return;
            }

            IndexBufferPacked *indexBuffer = vao->getIndexBuffer();
            AsyncTicketPtr indexTicket;
            const uint8 *indexData = 0;
            if( indexBuffer )
            {
                if( indexBuffer->getShadowCopy() )
                {
                    indexData = reinterpret_cast<const uint8 *>( indexBuffer->getShadowCopy() ) +
                                vao->getPrimitiveStart() * indexBuffer->getBytesPerElement();
                }
                else
                {
                    indexTicket = indexBuffer->readRequest( vao->getPrimitiveStart(), indexCount );
                    indexData = reinterpret_cast<const uint8 *>( indexTicket->map() );
                }
            }

            const uint32 meshletCount = std::min<uint32>( RtMeshletMaxPerSubMesh,
                std::max<uint32>( 1u, ( triangleCount + RtMeshletTargetTriangles - 1u ) /
                                      RtMeshletTargetTriangles ) );
            const uint32 trianglesPerMeshlet = ( triangleCount + meshletCount - 1u ) / meshletCount;
            const size_t vertexCount = vao->getBaseVertexBuffer()->getNumElements();

            for( uint32 meshletIdx = 0u; meshletIdx < meshletCount; ++meshletIdx )
            {
                const uint32 firstTriangle = meshletIdx * trianglesPerMeshlet;
                const uint32 lastTriangle = std::min<uint32>( triangleCount,
                                                              firstTriangle + trianglesPerMeshlet );
                Vector3 minCorner( Math::POS_INFINITY, Math::POS_INFINITY, Math::POS_INFINITY );
                Vector3 maxCorner( Math::NEG_INFINITY, Math::NEG_INFINITY, Math::NEG_INFINITY );
                bool hasVertex = false;

                for( uint32 triangleIdx = firstTriangle; triangleIdx < lastTriangle; ++triangleIdx )
                {
                    for( uint32 corner = 0u; corner < 3u; ++corner )
                    {
                        const uint32 indexIdx = triangleIdx * 3u + corner;
                        const uint32 vertexIdx = indexBuffer ?
                            readIndexAt( indexData, indexBuffer, indexIdx ) : indexIdx;
                        if( vertexIdx >= vertexCount )
                            continue;

                        const uint8 *vertexData = srcData[0] + downloadData[0].srcOffset +
                                                  vertexIdx * downloadData[0].srcBytesPerVertex;
                        const Vector4 pos4 = VertexBufferDownloadHelper::getVector4(
                            vertexData, *downloadData[0].origElements );
                        const Vector3 pos( pos4.x, pos4.y, pos4.z );
                        minCorner.makeFloor( pos );
                        maxCorner.makeCeil( pos );
                        hasVertex = true;
                    }
                }

                if( hasVertex )
                {
                    Aabb bounds;
                    bounds.setExtents( minCorner, maxCorner );
                    if( bounds.mHalfSize.squaredLength() <= Real( 1e-8 ) )
                        bounds.mHalfSize = Vector3( 0.5f, 0.5f, 0.5f );
                    meshletBounds.push_back( bounds );
                }
            }

            if( indexTicket )
                indexTicket->unmap();
            downloadHelper.unmap();

            if( meshletBounds.empty() )
                meshletBounds.push_back( fallbackBounds );
        }

        MeshPtr createMeshletProxyMesh( Mesh *sourceMesh, VaoManager *vaoManager,
                                        FastArray<uint32> &proxySubMeshToSourceSubMesh,
                                        FastArray<Aabb> &proxySubMeshBounds )
        {
            proxySubMeshToSourceSubMesh.clear();
            proxySubMeshBounds.clear();
            MeshPtr proxyMesh = MeshManager::getSingleton().createManual(
                "AutoGen_PathTracerRtMeshlets_" + sourceMesh->getName() + "_" +
                    StringConverter::toString( IdString( sourceMesh->getName() ).mHash ),
                ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME );

            const Aabb &meshBounds = sourceMesh->getAabb();
            FastArray<Aabb> meshletBounds;
            for( unsigned subMeshIdx = 0u; subMeshIdx < sourceMesh->getNumSubMeshes(); ++subMeshIdx )
            {
                SubMesh *sourceSubMesh = sourceMesh->getSubMesh( subMeshIdx );
                calculateSubMeshMeshletBounds( sourceSubMesh, meshBounds, meshletBounds );
                for( size_t meshletIdx = 0u; meshletIdx < meshletBounds.size(); ++meshletIdx )
                {
                    VertexArrayObject *vao = createBoxVao( meshletBounds[meshletIdx], vaoManager );

                    SubMesh *proxySubMesh = proxyMesh->createSubMesh();
                    for( int i = 0; i < NumVertexPass; ++i )
                        proxySubMesh->mVao[i].push_back( vao );
                    proxySubMeshToSourceSubMesh.push_back( subMeshIdx );
                    proxySubMeshBounds.push_back( meshletBounds[meshletIdx] );
                }
            }

            proxyMesh->_setBounds( meshBounds, false );
            proxyMesh->_setBoundingSphereRadius( meshBounds.getRadius() );
            return proxyMesh;
        }

        uint32 chooseRtLodLevel( const Item *item,
                                 const RTShadowsMeshCache::ShadowsCachedMesh &cachedMesh,
                                 const Camera *lodCamera,
                                 bool wasUsingProxy )
        {
            const uint32 maxLod = static_cast<uint32>( cachedMesh.lodRanges.size() - 1u );
            uint32 lodLevel = std::min<uint32>( item->getCurrentMeshLod(), maxLod );
            if( lodCamera && maxLod > lodLevel )
            {
                const Mesh *mesh = item->getMesh().get();
                const Matrix4 transform = item->getParentSceneNode()->_getFullTransformUpdated();
                const Vector3 worldCenter = transform * mesh->getAabb().mCenter;
                const Real distance = worldCenter.distance( lodCamera->getDerivedPosition() );
                const Real radius = std::max<Real>( mesh->getBoundingSphereRadius(), Real( 1 ) );
                const Real proxyEnterDistance = std::max<Real>( radius * Real( 30 ), Real( 96 ) );
                const Real proxyExitDistance = std::max<Real>( radius * Real( 20 ), Real( 64 ) );
                if( wasUsingProxy )
                    lodLevel = distance > proxyExitDistance ? maxLod : lodLevel;
                else if( distance > proxyEnterDistance )
                    lodLevel = maxLod;
            }
            return lodLevel;
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
                    a[i].localBounds.mCenter != b[i].localBounds.mCenter ||
                    a[i].localBounds.mHalfSize != b[i].localBounds.mHalfSize ||
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
        mLodCamera( 0 ),
        mGpuCullDistance( 0 ),
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
    void RTShadowsMeshCache::setGpuCullDistance( Real distance )
    {
        distance = distance > Real( 0 ) ? distance : Real( 0 );
        if( Math::Abs( mGpuCullDistance - distance ) <= Real( 1e-4 ) )
            return;

        mGpuCullDistance = distance;
        mRebuildTlas = true;
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
                Mesh *mesh = itor->get();
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

                if( meshCacheIt != mMeshCaches.end() )
                {
                    if( !meshCacheIt->second.proxyMesh )
                        meshCacheIt->second.proxyMesh = createMeshletProxyMesh(
                            mesh, Root::getSingleton().getRenderSystem()->getVaoManager(),
                            meshCacheIt->second.proxySubMeshToSourceSubMesh,
                            meshCacheIt->second.proxySubMeshBounds );

                    Mesh *proxyMesh = meshCacheIt->second.proxyMesh.get();
                    MeshLodRange proxyRange;
                    proxyRange.blasStart = blasIdx;
                    proxyRange.numBlas = proxyMesh->getNumSubMeshes();
                    for( unsigned subMeshIdx = 0u; subMeshIdx < proxyMesh->getNumSubMeshes(); ++subMeshIdx )
                    {
                        SubMesh *proxySubMesh = proxyMesh->getSubMesh( subMeshIdx );
                        meshVaos.push_back( proxySubMesh->mVao[VpNormal].front() );
                        ++blasIdx;
                    }
                    meshCacheIt->second.lodRanges.push_back( proxyRange );
                }

                ++itor;
            }
        }
        
        SelectedSubMeshInstanceArray selectedSubMeshInstances;
        ItemSet selectedProxyItems;
        std::vector<uint32> instanceMeshIndex;
        std::vector<Matrix4> instanceTransform;
        std::vector<Vector4> instanceBounds;
        ItemArray::iterator itemItor = mItems.begin();
        ItemArray::iterator itemEnd = mItems.end();
        
        while( itemItor != itemEnd )
        {
            Item *item = *itemItor;
            Mesh *mesh = item->getMesh().get();
            MeshCacheMap::iterator meshCacheIt = mMeshCaches.find( mesh->getName() );
            if( meshCacheIt != mMeshCaches.end() && !meshCacheIt->second.lodRanges.empty() )
            {
                const ShadowsCachedMesh &cachedMesh = meshCacheIt->second;
                const bool wasUsingProxy = mLastSelectedProxyItems.find( item ) != mLastSelectedProxyItems.end();
                const uint32 lodLevel = chooseRtLodLevel( item, cachedMesh, mLodCamera, wasUsingProxy );
                const MeshLodRange &lodRange = cachedMesh.lodRanges[lodLevel];
                const bool usingProxy = cachedMesh.proxyMesh && lodLevel == cachedMesh.lodRanges.size() - 1u;
                if( usingProxy )
                    selectedProxyItems.insert( item );
                const Matrix4 transform = item->getParentSceneNode()->_getFullTransformUpdated();
                const uint32 numSubMeshes = usingProxy ?
                    std::min<uint32>( std::min<uint32>( cachedMesh.proxyMesh->getNumSubMeshes(),
                                                        cachedMesh.proxySubMeshToSourceSubMesh.size() ),
                                      cachedMesh.proxySubMeshBounds.size() ) :
                    std::min<uint32>( item->getNumSubItems(), lodRange.numBlas );
                for( uint32 subMeshIdx = 0u; subMeshIdx < numSubMeshes; ++subMeshIdx )
                {
                    const uint32 sourceSubMeshIdx = usingProxy ?
                        cachedMesh.proxySubMeshToSourceSubMesh[subMeshIdx] : subMeshIdx;
                    if( sourceSubMeshIdx >= item->getNumSubItems() )
                        continue;

                    SelectedSubMeshInstance selectedInstance;
                    selectedInstance.item = item;
                    selectedInstance.mesh = usingProxy ? cachedMesh.proxyMesh.get() : mesh;
                    selectedInstance.subMesh = usingProxy ?
                        cachedMesh.proxyMesh->getSubMesh( static_cast<unsigned>( subMeshIdx ) ) :
                        mesh->getSubMesh( static_cast<unsigned>( subMeshIdx ) );
                    selectedInstance.subMeshIdx = sourceSubMeshIdx;
                    selectedInstance.localBounds = usingProxy ? cachedMesh.proxySubMeshBounds[subMeshIdx] :
                        mesh->getAabb();
                    selectedInstance.lodLevel = usingProxy ? 0u : lodLevel;
                    selectedInstance.blasIndex = lodRange.blasStart + subMeshIdx;
                    selectedSubMeshInstances.push_back( selectedInstance );

                    instanceMeshIndex.push_back( selectedInstance.blasIndex );
                    instanceTransform.push_back( transform );
                    const Aabb &bounds = selectedInstance.localBounds;
                    const Vector3 worldCenter = transform * bounds.mCenter;
                    const Vector3 axisX( transform[0][0], transform[1][0], transform[2][0] );
                    const Vector3 axisY( transform[0][1], transform[1][1], transform[2][1] );
                    const Vector3 axisZ( transform[0][2], transform[1][2], transform[2][2] );
                    const Real maxScale = std::max<Real>( axisX.length(),
                        std::max<Real>( axisY.length(), axisZ.length() ) );
                    const Real worldRadius = bounds.mHalfSize.length() * std::max<Real>( maxScale, Real( 1e-6 ) );
                    instanceBounds.push_back( Vector4( worldCenter.x, worldCenter.y, worldCenter.z,
                                                       worldRadius ) );
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

        mLastSelectedProxyItems.swap( selectedProxyItems );

        if( wasRebuildingBlas )
            ++mGeometryRevision;
        
        RenderSystem *renderSystem = Root::getSingleton().getRenderSystem();
        if( instanceMeshIndex.empty() )
        {
            renderSystem->clearAccelerationStructure();
            mRebuildTlas = true;
            return;
        }

        RenderSystem::AccelerationStructureCullParams cullParams;
        const Vector3 cameraPos = mLodCamera ? mLodCamera->getDerivedPosition() : Vector3::ZERO;
        cullParams.cameraPositionAndMaxDistance = Vector4( cameraPos.x, cameraPos.y, cameraPos.z,
                                                           mGpuCullDistance );
        if( mLodCamera && mGpuCullDistance > 0.0f )
        {
            const Vector3 cameraForward = mLodCamera->getDerivedDirection().normalisedCopy();
            const Vector3 cameraRight = mLodCamera->getDerivedRight().normalisedCopy();
            const Vector3 cameraUp = mLodCamera->getDerivedUp().normalisedCopy();
            const Real coneExpansion = Real( 1.5 );
            const Real tanHalfFovY = Math::Tan( mLodCamera->getFOVy() * Real( 0.5 ) ) * coneExpansion;
            const Real tanHalfFovX = tanHalfFovY * mLodCamera->getAspectRatio();

            cullParams.cameraForwardAndNear = Vector4( cameraForward.x, cameraForward.y,
                                                       cameraForward.z,
                                                       mLodCamera->getNearClipDistance() );
            cullParams.cameraRightAndTanHalfFovX = Vector4( cameraRight.x, cameraRight.y,
                                                            cameraRight.z, tanHalfFovX );
            cullParams.cameraUpAndTanHalfFovY = Vector4( cameraUp.x, cameraUp.y,
                                                         cameraUp.z, tanHalfFovY );
            cullParams.cullOptions = Vector4( 1.0f, coneExpansion,
                                              mLodCamera->getFarClipDistance(), 0.0f );
        }

        if( mRebuildBlas )
        {
            renderSystem->createAccelerationStructure( mMeshes, meshVaos, instanceMeshIndex, instanceTransform,
                                                       &instanceBounds, cullParams );
            mRebuildBlas = false;
            mRebuildTlas = false;
        }
        else if( mRebuildTlas )
        {
            renderSystem->rebuildAccelerationStructure( instanceMeshIndex, instanceTransform,
                                                        &instanceBounds, cullParams );
            mRebuildTlas = false;
        }
        else
        {
            renderSystem->refitAccelerationStructure( instanceMeshIndex, instanceTransform,
                                                      &instanceBounds, cullParams );
        }
    }
}
