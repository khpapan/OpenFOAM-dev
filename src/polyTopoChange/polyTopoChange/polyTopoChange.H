/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2011-2024 OpenFOAM Foundation
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

Class
    Foam::polyTopoChange

Description
    Direct mesh changes based on v1.3 polyTopoChange syntax.

    Instead of recording changes and executing them all in one go (as did
    v1.3 polyTopoChange) this class actually holds the current
    points/faces/cells and does the change immediately.
    It can be asked to compress out all unused points/faces/cells and
    renumber everything to be consistent.

    Note:
    - polyTopoChange can be copied.
    - adding a face using non-existing cells causes all intermediate cells
    to be added. So always first add cells/points and then faces.
    (or set strict checking)
    - strict checking:
        - any added/modified face can only use already existing vertices
        - any added face can only use already existing cells
        - no item can be removed more than once.
    - removed cell: cell set to 0 faces.
    - removed face: face set to 0 vertices.
    - removed point: coordinate set to vector::max (vGreat,vGreat,vGreat).
    Note that this might give problems if this value is used already.
    To see if point is equal to above value we don't use == (which might give
    problems with roundoff error) but instead compare the individual component
    with >.
    - coupled patches: the reorderCoupledFaces routine (borrowed from
    the couplePatches utility) reorders coupled patch faces and
    uses the cyclicPolyPatch,processorPolyPatch functionality.

SourceFiles
    polyTopoChange.C
    polyTopoChangeI.H
    polyTopoChangeTemplates.C

\*---------------------------------------------------------------------------*/

#ifndef polyTopoChange_H
#define polyTopoChange_H

#include "DynamicList.H"
#include "labelList.H"
#include "pointField.H"
#include "Map.H"
#include "HashSet.H"
#include "polyTopoChangeMap.H"
#include "PackedBoolList.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// Forward declaration of classes
class face;
class primitiveMesh;
class polyMesh;
class fvMesh;
class Time;
class fileName;
class polyBoundaryMesh;
class polyPatch;
class dictionary;
class topoAction;
class objectMap;
class IOobject;
template<class T> class CompactListList;

/*---------------------------------------------------------------------------*\
                       Class polyTopoChange Declaration
\*---------------------------------------------------------------------------*/

class polyTopoChange
{
    // Private Data

        //- Whether to allow referencing illegal points/cells/faces
        //  when adding/removing data
        bool strict_;


        // Patches

            //- Number of patches
            label nPatches_;


        // Points

            //- Current point set
            DynamicList<point> points_;

            //- Original point label (or masterpoint for added points)
            DynamicList<label> pointMap_;

            //- For all original and added points contains new point label
            //  (used to map return value of addPoint to new mesh point)
            DynamicList<label> reversePointMap_;

            //- Retired points
            labelHashSet retiredPoints_;

            //- Explicitly provided old location for e.g. added points without
            //  masterPoint
            Map<point> oldPoints_;


        // Faces

            //- Current faceList
            DynamicList<face> faces_;

            //- Patch for every external face (-1 for internal faces)
            DynamicList<label> region_;

            //- Owner for all faces
            DynamicList<label> faceOwner_;

            //- Neighbour for internal faces (-1 for external faces)
            DynamicList<label> faceNeighbour_;

            //- Original face label. Or master face for added-from-faces;
            //  -1 for faces added-from-edge or added-from-point)
            DynamicList<label> faceMap_;

            //- For all original and added faces contains new face label
            //  (used to map return value of addFace to new mesh face)
            DynamicList<label> reverseFaceMap_;

            //- In mapping whether to reverse the flux
            PackedBoolList flipFaceFlux_;

            //- Active faces
            label nActiveFaces_;


        // Cells

            //- Original cell label or master cell for added-from-cell;
            //  -1 for cells added from face or edge
            DynamicList<label> cellMap_;

            //- For all original and added cells contains new cell label
            //  (used to map return value of addCell to new mesh cell)
            DynamicList<label> reverseCellMap_;


    // Private Member Functions

        //- Reorder contents of container according to map
        template<class T>
        static void reorder(const labelList& map, DynamicList<T>&);

        template<class T>
        static void reorder(const labelList& map, List<DynamicList<T>>&);

        template<class T>
        static void renumberKey(const labelList& map, Map<T>&);

        //- Renumber elements of container according to map
        static void renumber(const labelList&, labelHashSet&);

        //- Special handling of reverse maps which have <-1 in them
        static void renumberReverseMap(const labelList&, DynamicList<label>&);

        //- Renumber & compact elements of list according to map
        static void renumberCompact(const labelList&, labelList&);

        //- Get all set elements as a labelHashSet
        static labelHashSet getSetIndices(const PackedBoolList&);

        //- Count number of added and removed quantities from maps
        static void countMap
        (
            const labelList& map,
            const labelList& reverseMap,
            label& nSplit,
            label& nInserted,
            label& nMerge,
            label& nRemove
        );

        //- Print some stats about mesh
        static void writeMeshStats(const polyMesh& mesh, Ostream&);

        //- Calculate object maps. Requires reverseMap to have destination
        //  to be marked with < -1
        static void getMergeSets
        (
            const labelList& reverseCellMap,
            const labelList& cellMap,
            List<objectMap>& cellsFromCells
        );

        //- Are all face vertices valid
        bool hasValidPoints(const face&) const;

        //- Return face points
        pointField facePoints(const face& f) const;

        //- Check inputs to modifyFace or addFace
        void checkFace
        (
            const face&,
            const label facei,
            const label own,
            const label nei,
            const label patchi
        ) const;

        //- Construct cells (in packed storage)
        void makeCells
        (
            const label nActiveFaces,
            labelList& cellFaces,
            labelList& cellFaceOffsets
        ) const;

        //- Construct cellCells (in packed storage)
        void makeCellCells
        (
            const label nActiveFaces,
            CompactListList<label>& cellCells
        ) const;

        //- Cell ordering (bandCompression). Returns number of remaining cells
        label getCellOrder
        (
            const CompactListList<label>&,
            labelList&
        ) const;

        //- Do upper-triangular ordering and patch ordering
        void getFaceOrder
        (
            const label nActiveFaces,
            const labelList& cellFaces,
            const labelList& cellFaceOffsets,

            labelList& oldToNew,
            labelList& patchSizes,
            labelList& patchStarts
        ) const;

        //- Compact and reorder faces according to map
        void reorderCompactFaces
        (
            const label newSize,
            const labelList& oldToNew
        );

        //- Remove all unused/removed points/faces/cells and update
        //  face ordering (always), cell ordering (bandcompression,
        //  orderCells=true),
        //  point ordering (sorted into internal and boundary points,
        //  orderPoints=true)
        //
        //  Compact all and orders points and faces:
        //  - points into internal followed by external points
        //  - internalfaces upper-triangular
        //  - externalfaces after internal ones.
        void compact
        (
            const bool orderCells,
            const bool orderPoints,
            label& nInternalPoints,
            labelList& patchSizes,
            labelList& patchStarts
        );

        //- Select either internal or external faces out of faceLabels
        //
        // Find faces to interpolate to create value for new face. Internal
        // faces should only be created from internal faces, external faces only
        // from external faces (and ideally the same patch) Is bit problematic
        // if there are no faces to select, i.e. in polyDualMesh an internal
        // face can be created from a boundary edge with no internal faces
        // connected to it.
        static labelList selectFaces
        (
            const primitiveMesh&,
            const labelList& faceLabels,
            const bool internalFacesOnly
        );

        //- Calculate mapping for patchpoints only
        void calcPatchPointMap
        (
            const List<Map<label>>&,
            const polyBoundaryMesh&,
            labelListList&
        ) const;

        //- Do all coupled patch face reordering
        void reorderCoupledFaces
        (
            const bool syncParallel,
            const polyBoundaryMesh&,
            const labelList& patchStarts,
            const labelList& patchSizes,
            const pointField& points
        );

        void compactAndReorder
        (
            const polyMesh&,
            const bool syncParallel,
            const bool orderCells,
            const bool orderPoints,
            label& nInternalPoints,
            pointField& newPoints,
            labelList& patchSizes,
            labelList& patchStarts,
            List<objectMap>& pointsFromPoints,
            List<objectMap>& facesFromFaces,
            List<objectMap>& cellsFromCells,
            List<Map<label>>& oldPatchMeshPointMaps,
            labelList& oldPatchNMeshPoints,
            labelList& oldPatchSizes,
            labelList& oldPatchStarts
        );


public:

    //- Runtime type information
    ClassName("polyTopoChange");


    // Constructors

        //- Construct without mesh. Either specify nPatches or use
        //  setNumPatches before trying to make a mesh (makeMesh, changeMesh)
        polyTopoChange(const label nPatches, const bool strict = true);

        //- Construct from mesh. Adds all points/face/cells from mesh
        polyTopoChange(const polyMesh& mesh, const bool strict = true);


    // Member Functions

        // Access

            //- Points. Shrunk after constructing mesh (or calling of compact())
            const DynamicList<point>& points() const
            {
                return points_;
            }

            const DynamicList<face>& faces() const
            {
                return faces_;
            }

            const DynamicList<label>& region() const
            {
                return region_;
            }

            const DynamicList<label>& faceOwner() const
            {
                return faceOwner_;
            }

            const DynamicList<label>& faceNeighbour() const
            {
                return faceNeighbour_;
            }

            //- Is point removed?
            inline bool pointRemoved(const label pointi) const;

            //- Is face removed?
            inline bool faceRemoved(const label facei) const;

            //- Is cell removed?
            inline bool cellRemoved(const label celli) const;


        // Edit

            //- Clear all storage
            void clear();

            //- Explicitly pre-size the dynamic storage for expected mesh
            //  size for if construct-without-mesh
            void setCapacity
            (
                const label nPoints,
                const label nFaces,
                const label nCells
            );

            //- Add point and return new point index
            //  Notes:
            //  - masterPointID can be < 0 (appended points)
            //  - inCell = false: add retired point (to end of point list)
            label addPoint
            (
                const point&,
                const label masterPointID,
                const bool inCell
            );

            //- Modify coordinate
            //  Notes:
            //  - inCell = false: add retired point (to end of point list)
            void modifyPoint
            (
                const label,
                const point&,
                const bool inCell
            );

            //- Remove point / merge points
            void removePoint(const label, const label);

            //- Add face to cells and return new face index
            //  own, nei <0 >=0 : add inactive face (to end of face list)
            label addFace
            (
                const face& f,
                const label own,
                const label nei,
                const label masterFaceID,
                const bool flipFaceFlux,
                const label patchID
            );

            //- Modify vertices or cell of face
            void modifyFace
            (
                const face& f,
                const label facei,
                const label own,
                const label nei,
                const bool flipFaceFlux,
                const label patchID
            );

            //- Remove face / merge faces
            void removeFace(const label, const label);

            //- Add cell and return new cell index
            label addCell(const label masterCellID);

            //- Remove cell / merge cells
            void removeCell(const label, const label);

            //- Explicitly set the number of patches if construct-without-mesh
            //  used
            inline void setNumPatches(const label nPatches);


        // Other

            //- Inplace changes mesh without change of patches.
            //  Adapts patch start/end and by default does parallel matching.
            //  Clears all data. Returns map.
            //  orderCells :  whether to order the cells (see bandCompression.H)
            //  orderPoints : whether to order the points into internal first
            //  followed by boundary points. This is not fully consistent
            //  with upper-triangular ordering of points and edges so
            //  is only done when explicitly asked for.
            autoPtr<polyTopoChangeMap> changeMesh
            (
                polyMesh& mesh,
                const bool syncParallel = true,
                const bool orderCells = false,
                const bool orderPoints = false
            );

            //- Create new mesh with old mesh patches
            autoPtr<polyTopoChangeMap> makeMesh
            (
                autoPtr<fvMesh>& newMesh,
                const IOobject& io,
                const polyMesh& mesh,
                const bool syncParallel = true,
                const bool orderCells = false,
                const bool orderPoints = false
            );
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#include "polyTopoChangeI.H"

#ifdef NoRepository
    #include "polyTopoChangeTemplates.C"
#endif

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
