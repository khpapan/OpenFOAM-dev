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

#include "processorCyclicPolyPatch.H"
#include "addToRunTimeSelectionTable.H"
#include "SubField.H"
#include "cyclicPolyPatch.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
    defineTypeNameAndDebug(processorCyclicPolyPatch, 0);
    addToRunTimeSelectionTable(polyPatch, processorCyclicPolyPatch, dictionary);
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::processorCyclicPolyPatch::processorCyclicPolyPatch
(
    const word& name,
    const label size,
    const label start,
    const label index,
    const polyBoundaryMesh& bm,
    const int myProcNo,
    const int neighbProcNo,
    const word& referPatchName,
    const word& patchType
)
:
    processorPolyPatch
    (
        name,
        size,
        start,
        index,
        bm,
        myProcNo,
        neighbProcNo,
        patchType
    ),
    referPatchName_(referPatchName),
    tag_(-1),
    referPatchIndex_(-1)
{}


Foam::processorCyclicPolyPatch::processorCyclicPolyPatch
(
    const label size,
    const label start,
    const label index,
    const polyBoundaryMesh& bm,
    const int myProcNo,
    const int neighbProcNo,
    const word& referPatchName,
    const word& patchType
)
:
    processorPolyPatch
    (
        newName(referPatchName, myProcNo, neighbProcNo),
        size,
        start,
        index,
        bm,
        myProcNo,
        neighbProcNo,
        patchType
    ),
    referPatchName_(referPatchName),
    tag_(-1),
    referPatchIndex_(-1)
{}


Foam::processorCyclicPolyPatch::processorCyclicPolyPatch
(
    const word& name,
    const dictionary& dict,
    const label index,
    const polyBoundaryMesh& bm,
    const word& patchType
)
:
    processorPolyPatch(name, dict, index, bm, patchType),
    referPatchName_(dict.lookup("referPatch")),
    tag_(dict.lookupOrDefault<int>("tag", -1)),
    referPatchIndex_(-1)
{}


Foam::processorCyclicPolyPatch::processorCyclicPolyPatch
(
    const processorCyclicPolyPatch& pp,
    const polyBoundaryMesh& bm
)
:
    processorPolyPatch(pp, bm),
    referPatchName_(pp.referPatchName()),
    tag_(pp.tag()),
    referPatchIndex_(-1)
{}


Foam::processorCyclicPolyPatch::processorCyclicPolyPatch
(
    const processorCyclicPolyPatch& pp,
    const polyBoundaryMesh& bm,
    const label index,
    const label newSize,
    const label newStart
)
:
    processorPolyPatch(pp, bm, index, newSize, newStart),
    referPatchName_(pp.referPatchName_),
    tag_(pp.tag()),
    referPatchIndex_(-1)
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::processorCyclicPolyPatch::~processorCyclicPolyPatch()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

Foam::word Foam::processorCyclicPolyPatch::newName
(
    const word& cyclicPolyPatchName,
    const label myProcNo,
    const label neighbProcNo
)
{
    return
        processorPolyPatch::newName(myProcNo, neighbProcNo)
      + "through"
      + cyclicPolyPatchName;
}


Foam::labelList Foam::processorCyclicPolyPatch::patchIDs
(
    const word& cyclicPolyPatchName,
    const polyBoundaryMesh& bm
)
{
    return bm.findIndices
    (
        wordRe(string("procBoundary.*to.*through" + cyclicPolyPatchName))
    );
}


int Foam::processorCyclicPolyPatch::tag() const
{
    if (tag_ == -1)
    {
        // Get unique tag to use for all comms. Make sure that both sides
        // use the same tag.
        const cyclicPolyPatch& cycPatch =
            refCast<const cyclicPolyPatch>(referPatch());

        if (processorPolyPatch::owner())
        {
            tag_ = Hash<word>()(cycPatch.name()) % 32768u;
        }
        else
        {
            tag_ = Hash<word>()(cycPatch.nbrPatch().name()) % 32768u;
        }

        if (tag_ == Pstream::msgType() || tag_ == -1)
        {
            FatalErrorInFunction
                << "Tag calculated from cyclic patch name " << tag_
                << " is the same as the current message type "
                << Pstream::msgType() << " or -1" << nl
                << "Please set a non-conflicting, unique, tag by hand"
                << " using the 'tag' entry"
                << exit(FatalError);
        }

        if (debug)
        {
            Pout<< "processorCyclicPolyPatch " << name() << " uses tag " << tag_
                << endl;
        }
    }
    return tag_;
}


void Foam::processorCyclicPolyPatch::initCalcGeometry(PstreamBuffers& pBufs)
{
    // Send over processorPolyPatch data
    processorPolyPatch::initCalcGeometry(pBufs);
}


void Foam::processorCyclicPolyPatch::calcGeometry(PstreamBuffers& pBufs)
{
    // Receive and initialise processorPolyPatch data
    processorPolyPatch::calcGeometry(pBufs);
}


void Foam::processorCyclicPolyPatch::initMovePoints
(
    PstreamBuffers& pBufs,
    const pointField& p
)
{
    // Recalculate geometry
    initCalcGeometry(pBufs);
}


void Foam::processorCyclicPolyPatch::movePoints
(
    PstreamBuffers& pBufs,
    const pointField&
)
{
    calcGeometry(pBufs);
}


void Foam::processorCyclicPolyPatch::initTopoChange(PstreamBuffers& pBufs)
{
    processorPolyPatch::initTopoChange(pBufs);
}


void Foam::processorCyclicPolyPatch::topoChange(PstreamBuffers& pBufs)
{
     referPatchIndex_ = -1;
     processorPolyPatch::topoChange(pBufs);
}


void Foam::processorCyclicPolyPatch::initOrder
(
    PstreamBuffers& pBufs,
    const primitivePatch& pp
) const
{
    processorPolyPatch::initOrder(pBufs, pp);
}


bool Foam::processorCyclicPolyPatch::order
(
    PstreamBuffers& pBufs,
    const primitivePatch& pp,
    labelList& faceMap,
    labelList& rotation
) const
{
    return processorPolyPatch::order(pBufs, pp, faceMap, rotation);
}


void Foam::processorCyclicPolyPatch::write(Ostream& os) const
{
    processorPolyPatch::write(os);
    writeEntry(os, "referPatch", referPatchName_);
    if (tag_ != -1)
    {
        writeEntry(os, "tag", tag_);
    }
}


// ************************************************************************* //
