/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2013-2024 OpenFOAM Foundation
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

#include "setTimeStepFunctionObject.H"
#include "addToRunTimeSelectionTable.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
namespace functionObjects
{
    defineTypeNameAndDebug(setTimeStepFunctionObject, 0);

    addToRunTimeSelectionTable
    (
        functionObject,
        setTimeStepFunctionObject,
        dictionary
    );
}
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::functionObjects::setTimeStepFunctionObject::setTimeStepFunctionObject
(
    const word& name,
    const Time& runTime,
    const dictionary& dict
)
:
    functionObject(name, runTime)
{
    read(dict);
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::functionObjects::setTimeStepFunctionObject::~setTimeStepFunctionObject()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

bool Foam::functionObjects::setTimeStepFunctionObject::read
(
    const dictionary& dict
)
{
    timeStepPtr_ =
        Function1<scalar>::New
        (
            "deltaT",
            time_.userUnits(),
            time_.userUnits(),
            dict
        );

    return true;
}


bool Foam::functionObjects::setTimeStepFunctionObject::execute()
{
    const bool adjustTimeStep =
        time_.controlDict().lookupOrDefault("adjustTimeStep", false);

    if (!adjustTimeStep)
    {
        const_cast<Time&>(time_).setDeltaT(timeStepPtr_().value(time_.value()));
    }

    return true;
}


bool Foam::functionObjects::setTimeStepFunctionObject::write()
{
    return true;
}


// ************************************************************************* //
