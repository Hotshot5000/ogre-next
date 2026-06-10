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

        bool gpuCullModeUsesDistance( RTShadowsMeshCache::GpuCullMode mode )
        {
            return mode == RTShadowsMeshCache::GpuCullDistance ||
                   mode == RTShadowsMeshCache::GpuCullFrustumAndDistance;
        }

        bool gpuCullModeUsesFrustum( RTShadowsMeshCache::GpuCullMode mode )
        {
            return mode == RTShadowsMeshCache::GpuCullFrustum ||
                   mode == RTShadowsMeshCache::GpuCullFrustumAndDistance;
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

        // Middle RT LOD tier. These triangles are used directly by the Metal ray tracing AS,
        // so the VAO must carry the same shading attributes that PathTracingTrace.metal reads
        // through primitive_id: position for intersection, plus UV/normal for material lookup.
        struct SimplifiedMeshletVertex
        {
            Vector3 position;
            Vector2 uv;
            Vector3 normal;
        };

        VertexArrayObject *createTriangleSampleVao( const std::vector<SimplifiedMeshletVertex> &vertices,
                                                     VaoManager *vaoManager )
        {
            if( vertices.empty() )
                return 0;

            std::vector<uint16> indices;
            indices.reserve( vertices.size() );
            for( size_t i = 0u; i < vertices.size(); ++i )
                indices.push_back( static_cast<uint16>( i ) );

            VertexElement2Vec vertexElements;
            vertexElements.push_back( VertexElement2( VET_FLOAT3, VES_POSITION ) );
            vertexElements.push_back( VertexElement2( VET_FLOAT2, VES_TEXTURE_COORDINATES ) );
            vertexElements.push_back( VertexElement2( VET_FLOAT3, VES_NORMAL ) );
            VertexBufferPacked *vertexBuffer = vaoManager->createVertexBuffer(
                vertexElements, vertices.size(), BT_IMMUTABLE,
                reinterpret_cast<void *>( const_cast<SimplifiedMeshletVertex *>( vertices.data() ) ), false );
            IndexBufferPacked *indexBuffer = vaoManager->createIndexBuffer(
                IndexBufferPacked::IT_16BIT, indices.size(), BT_IMMUTABLE,
                reinterpret_cast<void *>( indices.data() ), false );
            VertexBufferPackedVec vertexBuffers( 1u, vertexBuffer );
            return vaoManager->createVertexArrayObject( vertexBuffers, indexBuffer, OT_TRIANGLE_LIST );
        }

        const uint32 RtMeshletTargetTriangles = 192u;
        const uint32 RtMeshletMaxPerSubMesh = 32u;
        const uint32 RtSimplifiedMeshletMaxTriangles = 32u;

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

        void createSimplifiedMeshletVaos( SubMesh *subMesh, const Aabb &fallbackBounds,
                                          VaoManager *vaoManager,
                                          FastArray<VertexArrayObject *> &meshletVaos,
                                          FastArray<Aabb> &meshletBounds )
        {
            meshletVaos.clear();
            meshletBounds.clear();
            if( subMesh->mVao[VpNormal].empty() )
            {
                meshletBounds.push_back( fallbackBounds );
                meshletVaos.push_back( createBoxVao( fallbackBounds, vaoManager ) );
                return;
            }

            VertexArrayObject *vao = subMesh->mVao[VpNormal].front();
            const uint32 indexCount = vao->getPrimitiveCount();
            const uint32 triangleCount = indexCount / 3u;
            if( triangleCount == 0u )
            {
                meshletBounds.push_back( fallbackBounds );
                meshletVaos.push_back( createBoxVao( fallbackBounds, vaoManager ) );
                return;
            }

            // Download source attributes once and emit a sparse triangle stream per meshlet.
            // This is deliberately not a visual renderer LOD: it is AS geometry, and the trace
            // shader shades it using the primitive's own UV/normal records.
            VertexElementSemanticFullArray semanticsToDownload;
            semanticsToDownload.push_back( VES_POSITION );
            semanticsToDownload.push_back( VES_NORMAL );
            semanticsToDownload.push_back( VES_TEXTURE_COORDINATES );

            VertexBufferDownloadHelper downloadHelper;
            downloadHelper.queueDownload( vao, semanticsToDownload, 0u, 0u );
            const VertexBufferDownloadHelper::DownloadData *downloadData =
                downloadHelper.getDownloadData().data();
            if( !downloadData || !downloadData[0].origElements )
            {
                meshletBounds.push_back( fallbackBounds );
                meshletVaos.push_back( createBoxVao( fallbackBounds, vaoManager ) );
                return;
            }

            VertexElement2 dummy( VET_FLOAT1, VES_TEXTURE_COORDINATES );
            VertexElement2 origElements[3] = {
                downloadData[0].origElements ? *downloadData[0].origElements : dummy,
                downloadData[1].origElements ? *downloadData[1].origElements : dummy,
                downloadData[2].origElements ? *downloadData[2].origElements : dummy,
            };

            uint8 const *srcData[3];
            downloadHelper.map( srcData );
            if( !srcData[0] )
            {
                downloadHelper.unmap();
                meshletBounds.push_back( fallbackBounds );
                meshletVaos.push_back( createBoxVao( fallbackBounds, vaoManager ) );
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
                const uint32 meshletTriangleCount = std::max<uint32>( lastTriangle - firstTriangle, 1u );
                const uint32 triangleStep = std::max<uint32>( 1u,
                    ( meshletTriangleCount + RtSimplifiedMeshletMaxTriangles - 1u ) /
                    RtSimplifiedMeshletMaxTriangles );

                std::vector<SimplifiedMeshletVertex> vertices;
                vertices.reserve( RtSimplifiedMeshletMaxTriangles * 3u );
                Vector3 minCorner( Math::POS_INFINITY, Math::POS_INFINITY, Math::POS_INFINITY );
                Vector3 maxCorner( Math::NEG_INFINITY, Math::NEG_INFINITY, Math::NEG_INFINITY );

                for( uint32 triangleIdx = firstTriangle; triangleIdx < lastTriangle;
                     triangleIdx += triangleStep )
                {
                    SimplifiedMeshletVertex triangleVertices[3];
                    uint32 validCorners = 0u;
                    for( uint32 corner = 0u; corner < 3u; ++corner )
                    {
                        const uint32 indexIdx = triangleIdx * 3u + corner;
                        const uint32 vertexIdx = indexBuffer ?
                            readIndexAt( indexData, indexBuffer, indexIdx ) : indexIdx;
                        if( vertexIdx >= vertexCount )
                            continue;

                        const uint8 *positionData = srcData[0] + downloadData[0].srcOffset +
                                                    vertexIdx * downloadData[0].srcBytesPerVertex;
                        const Vector4 pos4 = VertexBufferDownloadHelper::getVector4(
                            positionData, origElements[0] );

                        SimplifiedMeshletVertex vertex;
                        vertex.position = Vector3( pos4.x, pos4.y, pos4.z );
                        vertex.uv = Vector2::ZERO;
                        vertex.normal = Vector3::UNIT_Y;

                        if( srcData[1] )
                        {
                            const uint8 *normalData = srcData[1] + downloadData[1].srcOffset +
                                                      vertexIdx * downloadData[1].srcBytesPerVertex;
                            vertex.normal = downloadHelper.getNormal( normalData, origElements[1] );
                            if( vertex.normal.squaredLength() <= Real( 1e-8 ) )
                                vertex.normal = Vector3::UNIT_Y;
                        }
                        if( srcData[2] )
                        {
                            const uint8 *uvData = srcData[2] + downloadData[2].srcOffset +
                                                  vertexIdx * downloadData[2].srcBytesPerVertex;
                            vertex.uv = VertexBufferDownloadHelper::getVector4( uvData, origElements[2] ).xy();
                        }

                        triangleVertices[validCorners++] = vertex;
                        minCorner.makeFloor( vertex.position );
                        maxCorner.makeCeil( vertex.position );
                    }

                    if( validCorners == 3u )
                    {
                        const Vector3 edge1 = triangleVertices[1].position - triangleVertices[0].position;
                        const Vector3 edge2 = triangleVertices[2].position - triangleVertices[0].position;
                        Vector3 faceNormal = edge1.crossProduct( edge2 );
                        if( faceNormal.squaredLength() > Real( 1e-8 ) )
                        {
                            faceNormal.normalise();
                            for( uint32 corner = 0u; corner < 3u; ++corner )
                            {
                                if( triangleVertices[corner].normal.squaredLength() <= Real( 1e-8 ) )
                                    triangleVertices[corner].normal = faceNormal;
                                else if( triangleVertices[corner].normal.dotProduct( faceNormal ) < Real( 0 ) )
                                    triangleVertices[corner].normal = -triangleVertices[corner].normal;
                                triangleVertices[corner].normal.normalise();
                            }
                        }

                        vertices.push_back( triangleVertices[0] );
                        vertices.push_back( triangleVertices[1] );
                        vertices.push_back( triangleVertices[2] );
                    }
                }

                if( vertices.empty() )
                    continue;

                Aabb bounds;
                bounds.setExtents( minCorner, maxCorner );
                if( bounds.mHalfSize.squaredLength() <= Real( 1e-8 ) )
                    bounds.mHalfSize = Vector3( 0.5f, 0.5f, 0.5f );
                meshletBounds.push_back( bounds );
                meshletVaos.push_back( createTriangleSampleVao( vertices, vaoManager ) );
            }

            if( indexTicket )
                indexTicket->unmap();
            downloadHelper.unmap();

            if( meshletVaos.empty() )
            {
                meshletBounds.push_back( fallbackBounds );
                meshletVaos.push_back( createBoxVao( fallbackBounds, vaoManager ) );
            }
        }

        MeshPtr createSimplifiedMeshletMesh( Mesh *sourceMesh, VaoManager *vaoManager,
                                             FastArray<uint32> &simplifiedSubMeshToSourceSubMesh,
                                             FastArray<Aabb> &simplifiedSubMeshBounds )
        {
            simplifiedSubMeshToSourceSubMesh.clear();
            simplifiedSubMeshBounds.clear();
            MeshPtr simplifiedMesh = MeshManager::getSingleton().createManual(
                "AutoGen_PathTracerRtSimplifiedMeshlets_" + sourceMesh->getName() + "_" +
                    StringConverter::toString( IdString( sourceMesh->getName() ).mHash ),
                ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME );

            const Aabb &meshBounds = sourceMesh->getAabb();
            FastArray<VertexArrayObject *> meshletVaos;
            FastArray<Aabb> meshletBounds;
            for( unsigned subMeshIdx = 0u; subMeshIdx < sourceMesh->getNumSubMeshes(); ++subMeshIdx )
            {
                SubMesh *sourceSubMesh = sourceMesh->getSubMesh( subMeshIdx );
                createSimplifiedMeshletVaos( sourceSubMesh, meshBounds, vaoManager, meshletVaos,
                                             meshletBounds );
                const size_t numMeshlets = std::min( meshletVaos.size(), meshletBounds.size() );
                for( size_t meshletIdx = 0u; meshletIdx < numMeshlets; ++meshletIdx )
                {
                    SubMesh *simplifiedSubMesh = simplifiedMesh->createSubMesh();
                    for( int i = 0; i < NumVertexPass; ++i )
                        simplifiedSubMesh->mVao[i].push_back( meshletVaos[meshletIdx] );
                    simplifiedSubMeshToSourceSubMesh.push_back( subMeshIdx );
                    simplifiedSubMeshBounds.push_back( meshletBounds[meshletIdx] );
                }
            }

            simplifiedMesh->_setBounds( meshBounds, false );
            simplifiedMesh->_setBoundingSphereRadius( meshBounds.getRadius() );
            return simplifiedMesh;
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
                                 bool wasUsingSimplified,
                                 bool wasUsingProxy )
        {
            const uint32 lastLod = static_cast<uint32>( cachedMesh.lodRanges.size() - 1u );
            const bool hasProxy = cachedMesh.proxyMesh && !cachedMesh.proxySubMeshToSourceSubMesh.empty();
            const bool hasSimplified = cachedMesh.simplifiedMesh &&
                !cachedMesh.simplifiedSubMeshToSourceSubMesh.empty() &&
                cachedMesh.lodRanges.size() > ( hasProxy ? 2u : 1u );
            const uint32 proxyLod = hasProxy ? lastLod : lastLod + 1u;
            const uint32 simplifiedLod = hasSimplified ? ( hasProxy ? lastLod - 1u : lastLod ) :
                                                          lastLod + 1u;
            const uint32 maxFullLod = hasSimplified ? simplifiedLod - 1u :
                ( hasProxy && proxyLod > 0u ? proxyLod - 1u : lastLod );
            uint32 lodLevel = std::min<uint32>( item->getCurrentMeshLod(), maxFullLod );
            if( lodCamera && ( hasSimplified || hasProxy ) )
            {
                const Mesh *mesh = item->getMesh().get();
                const Matrix4 transform = item->getParentSceneNode()->_getFullTransformUpdated();
                const Vector3 worldCenter = transform * mesh->getAabb().mCenter;
                const Vector3 axisX( transform[0][0], transform[1][0], transform[2][0] );
                const Vector3 axisY( transform[0][1], transform[1][1], transform[2][1] );
                const Vector3 axisZ( transform[0][2], transform[1][2], transform[2][2] );
                const Real maxScale = std::max<Real>( axisX.length(),
                    std::max<Real>( axisY.length(), axisZ.length() ) );
                const Real worldRadius = std::max<Real>( mesh->getBoundingSphereRadius() * maxScale,
                                                         Real( 1e-4 ) );
                const Real cameraDistance = worldCenter.distance( lodCamera->getDerivedPosition() );
                const Real projectedDistance = std::max<Real>( cameraDistance, Real( 1e-4 ) );
                const Real pixelDisplayRatio = lodCamera->getPixelDisplayRatio();
                if( pixelDisplayRatio > Real( 1e-6 ) )
                {
                    const Real projectedDiameterPixels =
                        ( worldRadius * Real( 2 ) ) / ( projectedDistance * pixelDisplayRatio );
                    // Projected diameter thresholds are intentionally conservative. The sampled
                    // triangle tier is visibly approximate at close range, while the box proxy is
                    // only acceptable when the whole item is tiny. Exit thresholds add hysteresis.
                    const Real proxyEnterPixels = Real( 8 );
                    const Real proxyExitPixels = Real( 12 );
                    const Real simplifiedEnterPixels = Real( 32 );
                    const Real simplifiedExitPixels = Real( 48 );

                    if( hasProxy )
                    {
                        if( wasUsingProxy && projectedDiameterPixels < proxyExitPixels )
                            return proxyLod;
                        if( !wasUsingProxy && projectedDiameterPixels < proxyEnterPixels )
                            return proxyLod;
                    }

                    if( hasSimplified )
                    {
                        if( wasUsingSimplified && projectedDiameterPixels < simplifiedExitPixels )
                            return simplifiedLod;
                        if( !wasUsingSimplified && projectedDiameterPixels < simplifiedEnterPixels )
                            return simplifiedLod;
                    }
                }
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
        mGpuCullReflectionConeExpansion( 2.5f ),
        mGpuCullMode( GpuCullOff ),
        mLastActiveMeshletCount( 0u ),
        mLastTotalMeshletCount( 0u ),
        mLastFullTierMeshletCount( 0u ),
        mLastSimplifiedTierMeshletCount( 0u ),
        mLastProxyTierMeshletCount( 0u ),
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
    void RTShadowsMeshCache::setGpuCullReflectionConeExpansion( Real expansion )
    {
        expansion = expansion > Real( 1 ) ? expansion : Real( 1 );
        if( Math::Abs( mGpuCullReflectionConeExpansion - expansion ) <= Real( 1e-4 ) )
            return;

        mGpuCullReflectionConeExpansion = expansion;
        mRebuildTlas = true;
    }
    //-------------------------------------------------------------------------
    void RTShadowsMeshCache::setGpuCullMode( GpuCullMode mode )
    {
        if( mode > GpuCullFrustumAndDistance )
            mode = GpuCullOff;
        if( mGpuCullMode == mode )
            return;

        mGpuCullMode = mode;
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
                    // BLAS ranges are appended in selection order: source mesh LODs first,
                    // sampled-triangle meshlets next, and box meshlets last. The LOD picker
                    // uses this ordering to identify the simplified/proxy tiers.
                    VaoManager *vaoManager = Root::getSingleton().getRenderSystem()->getVaoManager();
                    if( !meshCacheIt->second.simplifiedMesh )
                        meshCacheIt->second.simplifiedMesh = createSimplifiedMeshletMesh(
                            mesh, vaoManager,
                            meshCacheIt->second.simplifiedSubMeshToSourceSubMesh,
                            meshCacheIt->second.simplifiedSubMeshBounds );

                    Mesh *simplifiedMesh = meshCacheIt->second.simplifiedMesh.get();
                    MeshLodRange simplifiedRange;
                    simplifiedRange.blasStart = blasIdx;
                    simplifiedRange.numBlas = simplifiedMesh->getNumSubMeshes();
                    for( unsigned subMeshIdx = 0u; subMeshIdx < simplifiedMesh->getNumSubMeshes(); ++subMeshIdx )
                    {
                        SubMesh *simplifiedSubMesh = simplifiedMesh->getSubMesh( subMeshIdx );
                        meshVaos.push_back( simplifiedSubMesh->mVao[VpNormal].front() );
                        ++blasIdx;
                    }
                    meshCacheIt->second.lodRanges.push_back( simplifiedRange );

                    if( !meshCacheIt->second.proxyMesh )
                        meshCacheIt->second.proxyMesh = createMeshletProxyMesh(
                            mesh, vaoManager,
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
        ItemSet selectedSimplifiedItems;
        ItemSet selectedProxyItems;
        std::vector<uint32> instanceMeshIndex;
        std::vector<Matrix4> instanceTransform;
        std::vector<Vector4> instanceBounds;
        mLastFullTierMeshletCount = 0u;
        mLastSimplifiedTierMeshletCount = 0u;
        mLastProxyTierMeshletCount = 0u;
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
                const bool wasUsingSimplified =
                    mLastSelectedSimplifiedItems.find( item ) != mLastSelectedSimplifiedItems.end();
                const bool wasUsingProxy = mLastSelectedProxyItems.find( item ) != mLastSelectedProxyItems.end();
                const uint32 lodLevel = chooseRtLodLevel( item, cachedMesh, mLodCamera,
                                                          wasUsingSimplified, wasUsingProxy );
                const MeshLodRange &lodRange = cachedMesh.lodRanges[lodLevel];
                const bool usingProxy = cachedMesh.proxyMesh && lodLevel == cachedMesh.lodRanges.size() - 1u;
                const bool usingSimplified = !usingProxy && cachedMesh.simplifiedMesh &&
                    cachedMesh.lodRanges.size() >= 2u && lodLevel == cachedMesh.lodRanges.size() - 2u;
                if( usingSimplified )
                    selectedSimplifiedItems.insert( item );
                if( usingProxy )
                    selectedProxyItems.insert( item );
                const Matrix4 transform = item->getParentSceneNode()->_getFullTransformUpdated();
                uint32 numSubMeshes = std::min<uint32>( item->getNumSubItems(), lodRange.numBlas );
                if( usingSimplified )
                {
                    numSubMeshes = std::min<uint32>(
                        std::min<uint32>( cachedMesh.simplifiedMesh->getNumSubMeshes(),
                                          cachedMesh.simplifiedSubMeshToSourceSubMesh.size() ),
                        cachedMesh.simplifiedSubMeshBounds.size() );
                }
                else if( usingProxy )
                {
                    numSubMeshes = std::min<uint32>(
                        std::min<uint32>( cachedMesh.proxyMesh->getNumSubMeshes(),
                                          cachedMesh.proxySubMeshToSourceSubMesh.size() ),
                        cachedMesh.proxySubMeshBounds.size() );
                }

                if( usingProxy )
                    mLastProxyTierMeshletCount += numSubMeshes;
                else if( usingSimplified )
                    mLastSimplifiedTierMeshletCount += numSubMeshes;
                else
                    mLastFullTierMeshletCount += numSubMeshes;
                for( uint32 subMeshIdx = 0u; subMeshIdx < numSubMeshes; ++subMeshIdx )
                {
                    // AS geometry may come from generated meshlets, but material lookup must stay
                    // on the original Item subitem. Keep the generated submesh index separate from
                    // the source submesh index used by PathTracer::uploadGeometryBuffer.
                    const uint32 sourceSubMeshIdx = usingProxy ?
                        cachedMesh.proxySubMeshToSourceSubMesh[subMeshIdx] :
                        ( usingSimplified ? cachedMesh.simplifiedSubMeshToSourceSubMesh[subMeshIdx] :
                                            subMeshIdx );
                    if( sourceSubMeshIdx >= item->getNumSubItems() )
                        continue;

                    SelectedSubMeshInstance selectedInstance;
                    selectedInstance.item = item;
                    selectedInstance.mesh = usingProxy ? cachedMesh.proxyMesh.get() :
                        ( usingSimplified ? cachedMesh.simplifiedMesh.get() : mesh );
                    selectedInstance.subMesh = usingProxy ?
                        cachedMesh.proxyMesh->getSubMesh( static_cast<unsigned>( subMeshIdx ) ) :
                        ( usingSimplified ?
                              cachedMesh.simplifiedMesh->getSubMesh( static_cast<unsigned>( subMeshIdx ) ) :
                              mesh->getSubMesh( static_cast<unsigned>( subMeshIdx ) ) );
                    selectedInstance.subMeshIdx = sourceSubMeshIdx;
                    selectedInstance.localBounds = usingProxy ? cachedMesh.proxySubMeshBounds[subMeshIdx] :
                        ( usingSimplified ? cachedMesh.simplifiedSubMeshBounds[subMeshIdx] : mesh->getAabb() );
                    selectedInstance.lodLevel = ( usingProxy || usingSimplified ) ? 0u : lodLevel;
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

        mLastSelectedSimplifiedItems.swap( selectedSimplifiedItems );
        mLastSelectedProxyItems.swap( selectedProxyItems );

        if( wasRebuildingBlas )
            ++mGeometryRevision;
        
        RenderSystem *renderSystem = Root::getSingleton().getRenderSystem();
        if( instanceMeshIndex.empty() )
        {
            mLastActiveMeshletCount = 0u;
            mLastTotalMeshletCount = 0u;
            renderSystem->clearAccelerationStructure();
            mRebuildTlas = true;
            return;
        }

        GpuCullMode effectiveCullMode = mGpuCullMode;
        if( !mLodCamera && gpuCullModeUsesFrustum( effectiveCullMode ) )
            effectiveCullMode = gpuCullModeUsesDistance( effectiveCullMode ) ? GpuCullDistance : GpuCullOff;

        RenderSystem::AccelerationStructureCullParams cullParams;
        const Vector3 cameraPos = mLodCamera ? mLodCamera->getDerivedPosition() : Vector3::ZERO;
        cullParams.cameraPositionAndMaxDistance = Vector4( cameraPos.x, cameraPos.y, cameraPos.z,
                                                           mGpuCullDistance );

        Vector3 cameraForward = Vector3::ZERO;
        Vector3 cameraRight = Vector3::ZERO;
        Vector3 cameraUp = Vector3::ZERO;
        Real tanHalfFovX = Real( 0 );
        Real tanHalfFovY = Real( 0 );
        Real nearDistance = Real( 0 );
        Real farDistance = Real( 0 );

        if( mLodCamera && effectiveCullMode != GpuCullOff )
        {
            cameraForward = mLodCamera->getDerivedDirection().normalisedCopy();
            cameraRight = mLodCamera->getDerivedRight().normalisedCopy();
            cameraUp = mLodCamera->getDerivedUp().normalisedCopy();
            tanHalfFovY = Math::Tan( mLodCamera->getFOVy() * Real( 0.5 ) );
            tanHalfFovX = tanHalfFovY * mLodCamera->getAspectRatio();
            nearDistance = mLodCamera->getNearClipDistance();
            farDistance = mLodCamera->getFarClipDistance();

            cullParams.cameraForwardAndNear = Vector4( cameraForward.x, cameraForward.y,
                                                       cameraForward.z, nearDistance );
            cullParams.cameraRightAndTanHalfFovX = Vector4( cameraRight.x, cameraRight.y,
                                                            cameraRight.z, tanHalfFovX );
            cullParams.cameraUpAndTanHalfFovY = Vector4( cameraUp.x, cameraUp.y,
                                                         cameraUp.z, tanHalfFovY );
        }
        cullParams.cullOptions = Vector4( static_cast<Real>( effectiveCullMode ),
                                          mGpuCullReflectionConeExpansion, farDistance, 0.0f );

        mLastTotalMeshletCount = static_cast<uint32>( instanceBounds.size() );
        mLastActiveMeshletCount = 0u;
        for( size_t i = 0u; i < instanceBounds.size(); ++i )
        {
            const Vector4 &bounds = instanceBounds[i];
            const Vector3 toBounds( bounds.x - cameraPos.x, bounds.y - cameraPos.y,
                                    bounds.z - cameraPos.z );
            const Real radius = bounds.w;

            bool distanceVisible = true;
            if( gpuCullModeUsesDistance( effectiveCullMode ) && mGpuCullDistance > 0.0f )
            {
                const Real cullDistance = mGpuCullDistance + radius;
                distanceVisible = toBounds.squaredLength() <= cullDistance * cullDistance;
            }

            bool frustumVisible = true;
            if( gpuCullModeUsesFrustum( effectiveCullMode ) && mLodCamera )
            {
                const Real depth = toBounds.dotProduct( cameraForward );
                const Real projectedDepth = std::max<Real>( depth, Real( 0 ) );
                const Real expandedTanHalfFovX = tanHalfFovX * mGpuCullReflectionConeExpansion;
                const Real expandedTanHalfFovY = tanHalfFovY * mGpuCullReflectionConeExpansion;
                const Real horizontalDistance = Math::Abs( toBounds.dotProduct( cameraRight ) );
                const Real verticalDistance = Math::Abs( toBounds.dotProduct( cameraUp ) );

                const bool depthVisible = depth + radius >= nearDistance &&
                    ( farDistance <= 0.0f || depth - radius <= farDistance );
                const bool horizontalVisible =
                    horizontalDistance <= projectedDepth * expandedTanHalfFovX + radius;
                const bool verticalVisible =
                    verticalDistance <= projectedDepth * expandedTanHalfFovY + radius;
                frustumVisible = depthVisible && horizontalVisible && verticalVisible;
            }

            if( distanceVisible && frustumVisible )
                ++mLastActiveMeshletCount;
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
