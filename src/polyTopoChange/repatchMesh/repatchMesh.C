/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2011-2023 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "repatchMesh.H"
#include "Time.H"
#include "polyMesh.H"
#include "repatcher.H"
#include "faceList.H"
#include "indexedOctree.H"
#include "treeDataPrimitivePatch.H"
#include "triSurface.H"
#include "SortableList.H"
#include "OFstream.H"
#include "uindirectPrimitivePatch.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
defineTypeNameAndDebug(repatchMesh, 0);

// Normal along which to divide faces into categories (used in getNearest)
const vector repatchMesh::splitNormal_(3, 2, 1);

// Distance to face tolerance for getNearest
const scalar repatchMesh::distanceTol_ = 1e-2;
}


// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

// Returns number of feature edges connected to pointi
Foam::label Foam::repatchMesh::nFeatureEdges(label pointi) const
{
    label nFeats = 0;

    const labelList& pEdges = mesh().pointEdges()[pointi];

    forAll(pEdges, pEdgeI)
    {
        label edgeI = pEdges[pEdgeI];

        if (edgeToFeature_[edgeI] != -1)
        {
            nFeats++;
        }
    }
    return nFeats;
}


// Returns next feature edge connected to pointi
Foam::label Foam::repatchMesh::nextFeatureEdge
(
    const label edgeI,
    const label vertI
) const
{
    const labelList& pEdges = mesh().pointEdges()[vertI];

    forAll(pEdges, pEdgeI)
    {
        label nbrEdgeI = pEdges[pEdgeI];

        if (nbrEdgeI != edgeI)
        {
            label featI = edgeToFeature_[nbrEdgeI];

            if (featI != -1)
            {
                return nbrEdgeI;
            }
        }
    }

    return -1;
}


// Finds connected feature edges, starting from startPointi and returns
// feature labels (not edge labels). Marks feature edges handled in
// featVisited.
Foam::labelList Foam::repatchMesh::collectSegment
(
    const boolList& isFeaturePoint,
    const label startEdgeI,
    boolList& featVisited
) const
{
    // Find starting feature point on edge.

    label edgeI = startEdgeI;

    const edge& e = mesh().edges()[edgeI];

    label vertI = e.start();

    while (!isFeaturePoint[vertI])
    {
        // Step to next feature edge

        edgeI = nextFeatureEdge(edgeI, vertI);

        if ((edgeI == -1) || (edgeI == startEdgeI))
        {
            break;
        }

        // Step to next vertex on edge

        const edge& e = mesh().edges()[edgeI];

        vertI = e.otherVertex(vertI);
    }

    //
    // Now we have:
    //    edgeI : first edge on this segment
    //    vertI : one of the endpoints of this segment
    //
    // Start walking other way and storing edges as we go along.
    //

    // Untrimmed storage for current segment
    labelList featLabels(featureEdges_.size());

    label featLabelI = 0;

    label initEdgeI = edgeI;

    do
    {
        // Mark edge as visited
        label featI = edgeToFeature_[edgeI];

        if (featI == -1)
        {
            FatalErrorInFunction
                << "Problem" << abort(FatalError);
        }
        featLabels[featLabelI++] = featI;

        featVisited[featI] = true;

        // Step to next vertex on edge

        const edge& e = mesh().edges()[edgeI];

        vertI = e.otherVertex(vertI);

        // Step to next feature edge

        edgeI = nextFeatureEdge(edgeI, vertI);

        if ((edgeI == -1) || (edgeI == initEdgeI))
        {
            break;
        }
    }
    while (!isFeaturePoint[vertI]);


    // Trim to size
    featLabels.setSize(featLabelI);

    return featLabels;
}


// Gets labels of changed faces and propagates them to the edges. Returns
// labels of edges changed.
Foam::labelList Foam::repatchMesh::faceToEdge
(
    const boolList& regionEdge,
    const label region,
    const labelList& changedFaces,
    labelList& edgeRegion
) const
{
    labelList changedEdges(mesh().nEdges(), -1);
    label changedI = 0;

    forAll(changedFaces, i)
    {
        label facei = changedFaces[i];

        const labelList& fEdges = mesh().faceEdges()[facei];

        forAll(fEdges, fEdgeI)
        {
            label edgeI = fEdges[fEdgeI];

            if (!regionEdge[edgeI] && (edgeRegion[edgeI] == -1))
            {
                edgeRegion[edgeI] = region;

                changedEdges[changedI++] = edgeI;
            }
        }
    }

    changedEdges.setSize(changedI);

    return changedEdges;
}


// Reverse of faceToEdge: gets edges and returns faces
Foam::labelList Foam::repatchMesh::edgeToFace
(
    const label region,
    const labelList& changedEdges,
    labelList& faceRegion
) const
{
    labelList changedFaces(mesh().size(), -1);
    label changedI = 0;

    forAll(changedEdges, i)
    {
        label edgeI = changedEdges[i];

        const labelList& eFaces = mesh().edgeFaces()[edgeI];

        forAll(eFaces, eFacei)
        {
            label facei = eFaces[eFacei];

            if (faceRegion[facei] == -1)
            {
                faceRegion[facei] = region;

                changedFaces[changedI++] = facei;
            }
        }
    }

    changedFaces.setSize(changedI);

    return changedFaces;
}


// Finds area, starting at facei, delimited by borderEdge
void Foam::repatchMesh::markZone
(
    const boolList& borderEdge,
    label facei,
    label currentZone,
    labelList& faceZone
) const
{
    faceZone[facei] = currentZone;

    // List of faces whose faceZone has been set.
    labelList changedFaces(1, facei);
    // List of edges whose faceZone has been set.
    labelList changedEdges;

    // Zones on all edges.
    labelList edgeZone(mesh().nEdges(), -1);

    while (true)
    {
        changedEdges = faceToEdge
        (
            borderEdge,
            currentZone,
            changedFaces,
            edgeZone
        );

        if (debug)
        {
            Pout<< "From changedFaces:" << changedFaces.size()
                << " to changedEdges:" << changedEdges.size()
                << endl;
        }

        if (changedEdges.empty())
        {
            break;
        }

        changedFaces = edgeToFace(currentZone, changedEdges, faceZone);

        if (debug)
        {
            Pout<< "From changedEdges:" << changedEdges.size()
                << " to changedFaces:" << changedFaces.size()
                << endl;
        }

        if (changedFaces.empty())
        {
            break;
        }
    }
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

// Null constructor
Foam::repatchMesh::repatchMesh()
:
    meshPtr_(nullptr),
    patches_(),
    meshFace_(),
    featurePoints_(),
    featureEdges_(),
    featureToEdge_(),
    edgeToFeature_(),
    featureSegments_()
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::repatchMesh::~repatchMesh()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void Foam::repatchMesh::read(const polyMesh& mesh)
{
    patches_.clear();

    patches_.setSize(mesh.boundaryMesh().size());

    // Number of boundary faces
    label nBFaces = mesh.nFaces() - mesh.nInternalFaces();

    faceList bFaces(nBFaces);

    meshFace_.setSize(nBFaces);

    label bFacei = 0;

    // Collect all boundary faces.
    forAll(mesh.boundaryMesh(), patchi)
    {
        const polyPatch& pp = mesh.boundaryMesh()[patchi];

        patches_.set
        (
            patchi,
            new repatchPatch
            (
                pp.name(),
                patchi,
                pp.size(),
                bFacei,
                pp.type()
            )
        );

        // Collect all faces in global numbering.
        forAll(pp, patchFacei)
        {
            meshFace_[bFacei] = pp.start() + patchFacei;

            bFaces[bFacei] = pp[patchFacei];

            bFacei++;
        }
    }


    if (debug)
    {
        Pout<< "read : patches now:" << endl;

        forAll(patches_, patchi)
        {
            const repatchPatch& bp = patches_[patchi];

            Pout<< "    name  : " << bp.name() << endl
                << "    size  : " << bp.size() << endl
                << "    start : " << bp.start() << endl
                << "    type  : " << bp.physicalType() << endl
                << endl;
        }
    }

    //
    // Construct single patch for all of boundary
    //

    // Temporary primitivePatch to calculate compact points & faces.
    PrimitivePatch<faceList, const pointField&> globalPatch
    (
        bFaces,
        mesh.points()
    );

    // Store in local(compact) addressing
    meshPtr_ = new rMesh(globalPatch.localFaces(), globalPatch.localPoints());


    if (debug & 2)
    {
        const rMesh& msh = *meshPtr_;

        Pout<< "** Start of Faces **" << endl;

        forAll(msh, facei)
        {
            const face& f = msh[facei];

            point ctr(Zero);

            forAll(f, fp)
            {
                ctr += msh.points()[f[fp]];
            }
            ctr /= f.size();

            Pout<< "    " << facei
                << " ctr:" << ctr
                << " verts:" << f
                << endl;
        }

        Pout<< "** End of Faces **" << endl;

        Pout<< "** Start of Points **" << endl;

        forAll(msh.points(), pointi)
        {
            Pout<< "    " << pointi
                << " coord:" << msh.points()[pointi]
                << endl;
        }

        Pout<< "** End of Points **" << endl;
    }

    // Clear edge storage
    featurePoints_.setSize(0);
    featureEdges_.setSize(0);

    featureToEdge_.setSize(0);
    edgeToFeature_.setSize(meshPtr_->nEdges());
    edgeToFeature_ = -1;

    featureSegments_.setSize(0);
}


void Foam::repatchMesh::readTriSurface(const fileName& fName)
{
    triSurface surf(fName);

    if (surf.empty())
    {
        return;
    }

    // Sort according to region
    SortableList<label> regions(surf.size());

    forAll(surf, triI)
    {
        regions[triI] = surf[triI].region();
    }
    regions.sort();

    // Determine region mapping.
    Map<label> regionToBoundaryPatch;

    label oldRegion = -1111;
    label boundPatch = 0;

    forAll(regions, i)
    {
        if (regions[i] != oldRegion)
        {
            regionToBoundaryPatch.insert(regions[i], boundPatch);

            oldRegion = regions[i];
            boundPatch++;
        }
    }

    const geometricSurfacePatchList& surfPatches = surf.patches();

    patches_.clear();

    if (surfPatches.size() == regionToBoundaryPatch.size())
    {
        // There are as many surface patches as region numbers in triangles
        // so use the surface patches

        patches_.setSize(surfPatches.size());

        // Take over patches, setting size to 0 for now.
        forAll(surfPatches, patchi)
        {
            const geometricSurfacePatch& surfPatch = surfPatches[patchi];

            patches_.set
            (
                patchi,
                new repatchPatch
                (
                    surfPatch.name(),
                    patchi,
                    0,
                    0,
                    surfPatch.geometricType()
                )
            );
        }
    }
    else
    {
        // There are not enough surface patches. Make up my own.

        patches_.setSize(regionToBoundaryPatch.size());

        forAll(patches_, patchi)
        {
            patches_.set
            (
                patchi,
                new repatchPatch
                (
                    "patch" + name(patchi),
                    patchi,
                    0,
                    0,
                    "empty"
                )
            );
        }
    }

    //
    // Copy according into bFaces according to regions
    //

    const labelList& indices = regions.indices();

    faceList bFaces(surf.size());

    meshFace_.setSize(surf.size());

    label bFacei = 0;

    // Current region number
    label surfRegion = regions[0];
    label foamRegion = regionToBoundaryPatch[surfRegion];

    Pout<< "Surface region " << surfRegion << " becomes boundary patch "
        << foamRegion << " with name " << patches_[foamRegion].name() << endl;


    // Index in bFaces of start of current patch
    label startFacei = 0;

    forAll(indices, indexI)
    {
        label triI = indices[indexI];

        const labelledTri& tri = surf.localFaces()[triI];

        if (tri.region() != surfRegion)
        {
            // Change of region. We now know the size of the previous one.
            repatchPatch& bp = patches_[foamRegion];

            bp.size() = bFacei - startFacei;
            bp.start() = startFacei;

            surfRegion = tri.region();
            foamRegion = regionToBoundaryPatch[surfRegion];

            Pout<< "Surface region " << surfRegion << " becomes boundary patch "
                << foamRegion << " with name " << patches_[foamRegion].name()
                << endl;

            startFacei = bFacei;
        }

        meshFace_[bFacei] = triI;

        bFaces[bFacei++] = face(tri);
    }

    // Final region
    repatchPatch& bp = patches_[foamRegion];

    bp.size() = bFacei - startFacei;
    bp.start() = startFacei;

    //
    // Construct single primitivePatch for all of boundary
    //

    // Store compact.
    meshPtr_ = new rMesh(bFaces, surf.localPoints());

    // Clear edge storage
    featurePoints_.setSize(0);
    featureEdges_.setSize(0);

    featureToEdge_.setSize(0);
    edgeToFeature_.setSize(meshPtr_->nEdges());
    edgeToFeature_ = -1;

    featureSegments_.setSize(0);
}


// Get index in this (repatchMesh) of face nearest to each boundary face in
// pMesh.
// Originally all triangles/faces of repatchMesh would be bunged into
// one big octree. Problem was that faces on top of each other, differing
// only in sign of normal, could not be found separately. It would always
// find only one. We could detect that it was probably finding the wrong one
// (based on normal) but could not 'tell' the octree to retrieve the other
// one (since they occupy exactly the same space)
// So now faces get put into different octrees depending on normal.
// !It still will not be possible to differentiate between two faces on top
// of each other having the same normal
Foam::labelList Foam::repatchMesh::getNearest
(
    const primitiveMesh& pMesh,
    const vector& searchSpan
) const
{

    // Divide faces into two bins acc. to normal
    // - left of splitNormal
    // - right ,,
    DynamicList<label> leftFaces(mesh().size()/2);
    DynamicList<label> rightFaces(mesh().size()/2);

    forAll(mesh(), bFacei)
    {
        scalar sign = mesh().faceNormals()[bFacei] & splitNormal_;

        if (sign > -1e-5)
        {
            rightFaces.append(bFacei);
        }
        if (sign < 1e-5)
        {
            leftFaces.append(bFacei);
        }
    }

    leftFaces.shrink();
    rightFaces.shrink();

    if (debug)
    {
        Pout<< "getNearest :"
            << " rightBin:" << rightFaces.size()
            << " leftBin:" << leftFaces.size()
            << endl;
    }

    uindirectPrimitivePatch leftPatch
    (
        UIndirectList<face>(mesh(), leftFaces),
        mesh().points()
    );
    uindirectPrimitivePatch rightPatch
    (
        UIndirectList<face>(mesh(), rightFaces),
        mesh().points()
    );


    // Overall bb
    treeBoundBox overallBb(mesh().localPoints());

    // Extend domain slightly (also makes it 3D if was 2D)
    // Note asymmetry to avoid having faces align with octree cubes.
    scalar tol = 1e-6 * overallBb.avgDim();

    point& bbMin = overallBb.min();
    bbMin.x() -= tol;
    bbMin.y() -= tol;
    bbMin.z() -= tol;

    point& bbMax = overallBb.max();
    bbMax.x() += 2*tol;
    bbMax.y() += 2*tol;
    bbMax.z() += 2*tol;

    const scalar planarTol =
        indexedOctree<treeDataPrimitivePatch<uindirectPrimitivePatch>>::
        perturbTol();


    // Create the octrees
    indexedOctree
    <
        treeDataPrimitivePatch<uindirectPrimitivePatch>
    > leftTree
    (
        treeDataPrimitivePatch<uindirectPrimitivePatch>
        (
            false,          // cacheBb
            leftPatch,
            planarTol
        ),
        overallBb,
        10, // maxLevel
        10, // leafSize
        3.0 // duplicity
    );
    indexedOctree
    <
        treeDataPrimitivePatch<uindirectPrimitivePatch>
    > rightTree
    (
        treeDataPrimitivePatch<uindirectPrimitivePatch>
        (
            false,          // cacheBb
            rightPatch,
            planarTol
        ),
        overallBb,
        10, // maxLevel
        10, // leafSize
        3.0 // duplicity
    );

    if (debug)
    {
        Pout<< "getNearest : built trees" << endl;
    }


    const vectorField& ns = mesh().faceNormals();


    //
    // Search nearest triangle centre for every polyMesh boundary face
    //

    labelList nearestBFacei(pMesh.nFaces() - pMesh.nInternalFaces());

    treeBoundBox tightest;

    const scalar searchDimSqr = magSqr(searchSpan);

    forAll(nearestBFacei, patchFacei)
    {
        label meshFacei = pMesh.nInternalFaces() + patchFacei;

        const point& ctr = pMesh.faceCentres()[meshFacei];

        if (debug && (patchFacei % 1000) == 0)
        {
            Pout<< "getNearest : patchFace:" << patchFacei
                << " meshFacei:" << meshFacei << " ctr:" << ctr << endl;
        }


        // Get normal from area vector
        vector n = pMesh.faceAreas()[meshFacei];
        scalar area = mag(n);
        n /= area;

        scalar typDim = -great;
        const face& f = pMesh.faces()[meshFacei];

        forAll(f, fp)
        {
            typDim = max(typDim, mag(pMesh.points()[f[fp]] - ctr));
        }

        // Search right tree
        pointIndexHit rightInfo = rightTree.findNearest(ctr, searchDimSqr);

        // Search left tree. Note: could start from rightDist bounding box
        // instead of starting from top.
        pointIndexHit leftInfo = leftTree.findNearest(ctr, searchDimSqr);

        if (rightInfo.hit())
        {
            if (leftInfo.hit())
            {
                // Found in both trees. Compare normals.
                label rightFacei = rightFaces[rightInfo.index()];
                label leftFacei = leftFaces[leftInfo.index()];

                label rightDist = mag(rightInfo.hitPoint()-ctr);
                label leftDist = mag(leftInfo.hitPoint()-ctr);

                scalar rightSign = n & ns[rightFacei];
                scalar leftSign = n & ns[leftFacei];

                if
                (
                    (rightSign > 0 && leftSign > 0)
                 || (rightSign < 0 && leftSign < 0)
                )
                {
                    // Both same sign. Choose nearest.
                    if (rightDist < leftDist)
                    {
                        nearestBFacei[patchFacei] = rightFacei;
                    }
                    else
                    {
                        nearestBFacei[patchFacei] = leftFacei;
                    }
                }
                else
                {
                    // Differing sign.
                    // - if both near enough choose one with correct sign
                    // - otherwise choose nearest.

                    // Get local dimension as max of distance between ctr and
                    // any face vertex.

                    typDim *= distanceTol_;

                    if (rightDist < typDim && leftDist < typDim)
                    {
                        // Different sign and nearby. Choosing matching normal
                        if (rightSign > 0)
                        {
                            nearestBFacei[patchFacei] = rightFacei;
                        }
                        else
                        {
                            nearestBFacei[patchFacei] = leftFacei;
                        }
                    }
                    else
                    {
                        // Different sign but faraway. Choosing nearest.
                        if (rightDist < leftDist)
                        {
                            nearestBFacei[patchFacei] = rightFacei;
                        }
                        else
                        {
                            nearestBFacei[patchFacei] = leftFacei;
                        }
                    }
                }
            }
            else
            {
                // Found in right but not in left. Choose right regardless if
                // correct sign. Note: do we want this?
                label rightFacei = rightFaces[rightInfo.index()];
                nearestBFacei[patchFacei] = rightFacei;
            }
        }
        else
        {
            // No face found in right tree.

            if (leftInfo.hit())
            {
                // Found in left but not in right. Choose left regardless if
                // correct sign. Note: do we want this?
                nearestBFacei[patchFacei] = leftFaces[leftInfo.index()];
            }
            else
            {
                // No face found in left tree.
                nearestBFacei[patchFacei] = -1;
            }
        }
    }

    return nearestBFacei;
}


void Foam::repatchMesh::setFeatureEdges(const scalar minCos)
{
    edgeToFeature_.setSize(mesh().nEdges());

    edgeToFeature_ = -1;

    // 1. Mark feature edges

    // Storage for edge labels that are features. Trim later.
    featureToEdge_.setSize(mesh().nEdges());

    label featureI = 0;

    if (minCos >= 0.9999)
    {
        // Select everything
        forAll(mesh().edges(), edgeI)
        {
            edgeToFeature_[edgeI] = featureI;
            featureToEdge_[featureI++] = edgeI;
        }
    }
    else
    {
        forAll(mesh().edges(), edgeI)
        {
            const labelList& eFaces = mesh().edgeFaces()[edgeI];

            if (eFaces.size() == 2)
            {
                label face0I = eFaces[0];

                label face1I = eFaces[1];

                ////- Uncomment below code if you want to include patch
                ////  boundaries in feature edges.
                // if (whichPatch(face0I) != whichPatch(face1I))
                //{
                //    edgeToFeature_[edgeI] = featureI;
                //    featureToEdge_[featureI++] = edgeI;
                //}
                // else
                {
                    const vector& n0 = mesh().faceNormals()[face0I];

                    const vector& n1 = mesh().faceNormals()[face1I];

                    float cosAng = n0 & n1;

                    if (cosAng < minCos)
                    {
                        edgeToFeature_[edgeI] = featureI;
                        featureToEdge_[featureI++] = edgeI;
                    }
                }
            }
            else
            {
                // Should not occur: 0 or more than two faces
                edgeToFeature_[edgeI] = featureI;
                featureToEdge_[featureI++] = edgeI;
            }
        }
    }

    // Trim featureToEdge_ to actual number of edges.
    featureToEdge_.setSize(featureI);

    //
    // Compact edges i.e. relabel vertices.
    //

    featureEdges_.setSize(featureI);
    featurePoints_.setSize(mesh().nPoints());

    labelList featToMeshPoint(mesh().nPoints(), -1);

    label featPtI = 0;

    forAll(featureToEdge_, fEdgeI)
    {
        label edgeI = featureToEdge_[fEdgeI];

        const edge& e = mesh().edges()[edgeI];

        label start = featToMeshPoint[e.start()];

        if (start == -1)
        {
            featToMeshPoint[e.start()] = featPtI;

            featurePoints_[featPtI] = mesh().points()[e.start()];

            start = featPtI;

            featPtI++;
        }

        label end = featToMeshPoint[e.end()];

        if (end == -1)
        {
            featToMeshPoint[e.end()] = featPtI;

            featurePoints_[featPtI] = mesh().points()[e.end()];

            end = featPtI;

            featPtI++;
        }

        // Store with renumbered vertices.
        featureEdges_[fEdgeI] = edge(start, end);
    }

    // Compact points
    featurePoints_.setSize(featPtI);


    //
    // 2. Mark endpoints of feature segments. These are points with
    // != 2 feature edges connected.
    // Note: can add geometric constraint here as well that if 2 feature
    // edges the angle between them should be less than xxx.
    //

    boolList isFeaturePoint(mesh().nPoints(), false);

    forAll(featureToEdge_, featI)
    {
        label edgeI = featureToEdge_[featI];

        const edge& e = mesh().edges()[edgeI];

        if (nFeatureEdges(e.start()) != 2)
        {
            isFeaturePoint[e.start()] = true;
        }

        if (nFeatureEdges(e.end()) != 2)
        {
            isFeaturePoint[e.end()] = true;
        }
    }


    //
    // 3: Split feature edges into segments:
    // find point with not 2 feature edges -> start of feature segment
    //

    DynamicList<labelList> segments;


    boolList featVisited(featureToEdge_.size(), false);

    do
    {
        label startFeatI = -1;

        forAll(featVisited, featI)
        {
            if (!featVisited[featI])
            {
                startFeatI = featI;

                break;
            }
        }

        if (startFeatI == -1)
        {
            // No feature lines left.
            break;
        }

        segments.append
        (
            collectSegment
            (
                isFeaturePoint,
                featureToEdge_[startFeatI],
                featVisited
            )
        );
    }
    while (true);


    //
    // Store in *this
    //
    featureSegments_.setSize(segments.size());

    forAll(featureSegments_, segmentI)
    {
        featureSegments_[segmentI] = segments[segmentI];
    }
}


Foam::label Foam::repatchMesh::whichPatch(const label facei) const
{
    forAll(patches_, patchi)
    {
        const repatchPatch& bp = patches_[patchi];

        if ((facei >= bp.start()) && (facei < (bp.start() + bp.size())))
        {
            return patchi;
        }
    }

    FatalErrorInFunction
        << "Cannot find face " << facei << " in list of repatchPatches "
        << patches_
        << abort(FatalError);

    return -1;
}


Foam::label Foam::repatchMesh::findIndex(const word& patchName) const
{
    forAll(patches_, patchi)
    {
        if (patches_[patchi].name() == patchName)
        {
            return patchi;
        }
    }

    return -1;
}


void Foam::repatchMesh::addPatch(const word& patchName)
{
    patches_.setSize(patches_.size() + 1);

    // Add empty patch at end of patch list.

    label patchi = patches_.size()-1;

    repatchPatch* bpPtr = new repatchPatch
    (
        patchName,
        patchi,
        0,
        mesh().size(),
        "empty"
    );

    patches_.set(patchi, bpPtr);

    if (debug)
    {
        Pout<< "addPatch : patches now:" << endl;

        forAll(patches_, patchi)
        {
            const repatchPatch& bp = patches_[patchi];

            Pout<< "    name  : " << bp.name() << endl
                << "    size  : " << bp.size() << endl
                << "    start : " << bp.start() << endl
                << "    type  : " << bp.physicalType() << endl
                << endl;
        }
    }
}


void Foam::repatchMesh::deletePatch(const word& patchName)
{
    const label delPatchi = findIndex(patchName);

    if (delPatchi == -1)
    {
        FatalErrorInFunction
            << "Can't find patch named " << patchName
            << abort(FatalError);
    }

    if (patches_[delPatchi].size())
    {
        FatalErrorInFunction
            << "Trying to delete non-empty patch " << patchName
            << endl << "Current size:" << patches_[delPatchi].size()
            << abort(FatalError);
    }

    PtrList<repatchPatch> newPatches(patches_.size() - 1);

    for (label patchi = 0; patchi < delPatchi; patchi++)
    {
        newPatches.set(patchi, patches_[patchi].clone());
    }

    // Move patches down, starting from delPatchi.

    for (label patchi = delPatchi + 1; patchi < patches_.size(); patchi++)
    {
        newPatches.set(patchi - 1, patches_[patchi].clone());
    }

    patches_.clear();

    patches_ = newPatches;

    if (debug)
    {
        Pout<< "deletePatch : patches now:" << endl;

        forAll(patches_, patchi)
        {
            const repatchPatch& bp = patches_[patchi];

            Pout<< "    name  : " << bp.name() << endl
                << "    size  : " << bp.size() << endl
                << "    start : " << bp.start() << endl
                << "    type  : " << bp.physicalType() << endl
                << endl;
        }
    }
}


void Foam::repatchMesh::changePatchType
(
    const word& patchName,
    const word& patchType
)
{
    const label changeI = findIndex(patchName);

    if (changeI == -1)
    {
        FatalErrorInFunction
            << "Can't find patch named " << patchName
            << abort(FatalError);
    }


    // Cause we can't reassign to individual PtrList elems ;-(
    // work on copy


    PtrList<repatchPatch> newPatches(patches_.size());

    forAll(patches_, patchi)
    {
        if (patchi == changeI)
        {
            // Create copy but for type
            const repatchPatch& oldBp = patches_[patchi];

            repatchPatch* bpPtr = new repatchPatch
            (
                oldBp.name(),
                oldBp.index(),
                oldBp.size(),
                oldBp.start(),
                patchType
            );

            newPatches.set(patchi, bpPtr);
        }
        else
        {
            // Create copy
            newPatches.set(patchi, patches_[patchi].clone());
        }
    }

    patches_ = newPatches;
}


void Foam::repatchMesh::markFaces
(
    const labelList& protectedEdges,
    const label seedFacei,
    boolList& visited
) const
{
    boolList protectedEdge(mesh().nEdges(), false);

    forAll(protectedEdges, i)
    {
        protectedEdge[protectedEdges[i]] = true;
    }


    // Initialise zone for all faces to -1
    labelList currentZone(mesh().size(), -1);

    // Mark with 0 all faces reachable from seedFacei
    markZone(protectedEdge, seedFacei, 0, currentZone);

    // Set in visited all reached ones.
    visited.setSize(mesh().size());

    forAll(currentZone, facei)
    {
        if (currentZone[facei] == 0)
        {
            visited[facei] = true;
        }
        else
        {
            visited[facei] = false;
        }
    }
}


// ************************************************************************* //
