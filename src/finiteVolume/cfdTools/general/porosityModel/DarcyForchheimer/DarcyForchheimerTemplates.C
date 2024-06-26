/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2012-2023 OpenFOAM Foundation
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

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

template<class RhoFieldType>
void Foam::porosityModels::DarcyForchheimer::apply
(
    scalarField& Udiag,
    vectorField& Usource,
    const scalarField& V,
    const RhoFieldType& rho,
    const scalarField& mu,
    const vectorField& U
) const
{
    const labelList& cells = mesh_.cellZones()[zoneName_];

    forAll(cells, i)
    {
        const label celli = cells[i];
        const label j = this->fieldIndex(i);
        const tensor Cd =
            mu[celli]*D_[j] + (rho[celli]*mag(U[celli]))*F_[j];

        const scalar isoCd = tr(Cd);

        Udiag[celli] += V[celli]*isoCd;
        Usource[celli] -= V[celli]*((Cd - I*isoCd) & U[celli]);
    }
}


template<class RhoFieldType>
void Foam::porosityModels::DarcyForchheimer::apply
(
    tensorField& AU,
    const RhoFieldType& rho,
    const scalarField& mu,
    const vectorField& U
) const
{
    const labelList& cells = mesh_.cellZones()[zoneName_];

    forAll(cells, i)
    {
        const label celli = cells[i];
        const label j = this->fieldIndex(i);
        const tensor D = D_[j];
        const tensor F = F_[j];

        AU[celli] += mu[celli]*D + (rho[celli]*mag(U[celli]))*F;
    }
}


// ************************************************************************* //
