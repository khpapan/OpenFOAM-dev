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

#include "eConstThermo.H"
#include "IOstreams.H"

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

template<class EquationOfState>
Foam::eConstThermo<EquationOfState>::eConstThermo
(
    const word& name,
    const dictionary& dict
)
:
    EquationOfState(name, dict),
    Cv_(dict.subDict("thermodynamics").lookup<scalar>("Cv")),
    hf_
    (
        dict
       .subDict("thermodynamics")
       .lookupBackwardsCompatible<scalar>({"hf", "Hf"})
    ),
    Tref_(dict.subDict("thermodynamics").lookupOrDefault<scalar>("Tref", Tstd)),
    esRef_
    (
        dict
       .subDict("thermodynamics")
       .lookupOrDefaultBackwardsCompatible<scalar>({"esRef", "Esref"}, 0)
    )
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

template<class EquationOfState>
void Foam::eConstThermo<EquationOfState>::write(Ostream& os) const
{
    EquationOfState::write(os);

    dictionary dict("thermodynamics");
    dict.add("Cv", Cv_);
    dict.add("hf", hf_);
    if (Tref_ != Tstd)
    {
        dict.add("Tref", Tref_);
    }
    if (esRef_ != 0)
    {
        dict.add("esRef", esRef_);
    }
    os  << indent << dict.dictName() << dict;
}


// * * * * * * * * * * * * * * * Ostream Operator  * * * * * * * * * * * * * //

template<class EquationOfState>
Foam::Ostream& Foam::operator<<
(
    Ostream& os,
    const eConstThermo<EquationOfState>& ct
)
{
    ct.write(os);
    return os;
}


// ************************************************************************* //
