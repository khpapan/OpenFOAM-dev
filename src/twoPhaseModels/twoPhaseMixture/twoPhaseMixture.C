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

\*---------------------------------------------------------------------------*/

#include "twoPhaseMixture.H"
#include "viscosityModel.H"
#include "surfaceInterpolate.H"
#include "fvcGrad.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
    defineTypeNameAndDebug(twoPhaseMixture, 0);
}

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * //

Foam::typeIOobject<Foam::IOdictionary>
Foam::twoPhaseMixture::readPhasePropertiesDict
(
    const objectRegistry& obr
)
{
    typeIOobject<IOdictionary> phasePropertiesIO
    (
        "phaseProperties",
        obr.time().constant(),
        obr,
        IOobject::MUST_READ_IF_MODIFIED,
        IOobject::NO_WRITE,
        true
    );

    if (phasePropertiesIO.headerOk())
    {
        return phasePropertiesIO;
    }
    else
    {
        typeIOobject<IOdictionary> thermophysicalPropertiesIO
        (
            "thermophysicalProperties",
            obr.time().constant(),
            obr,
            IOobject::MUST_READ_IF_MODIFIED,
            IOobject::NO_WRITE,
            true
        );

        if (thermophysicalPropertiesIO.headerOk())
        {
            IOdictionary phasePropertiesDict(thermophysicalPropertiesIO);
            phasePropertiesDict.rename("phaseProperties");
            return phasePropertiesDict;
        }
        else
        {
            typeIOobject<IOdictionary> transportPropertiesIO
            (
                "transportProperties",
                obr.time().constant(),
                obr,
                IOobject::MUST_READ_IF_MODIFIED,
                IOobject::NO_WRITE,
                true
            );

            if (transportPropertiesIO.headerOk())
            {
                IOdictionary phasePropertiesDict(transportPropertiesIO);
                phasePropertiesDict.rename("phaseProperties");

                const wordList phases(phasePropertiesDict.lookup("phases"));

                forAll(phases, i)
                {
                    IOdictionary phaseDict
                    (
                        IOobject
                        (
                            IOobject::groupName
                            (
                                physicalProperties::typeName,
                                phases[i]
                            ),
                            obr.time().constant(),
                            obr,
                            IOobject::NO_READ,
                            IOobject::NO_WRITE,
                            true
                        )
                    );

                    phaseDict.merge(phasePropertiesDict.subDict(phases[i]));

                    phaseDict.changeKeyword
                    (
                        "transportModel",
                        viscosityModel::typeName
                    );

                    phaseDict.writeObject
                    (
                        IOstream::ASCII,
                        IOstream::currentVersion,
                        IOstream::UNCOMPRESSED,
                        true
                    );

                    phasePropertiesDict.remove(phases[i]);
                }

                phasePropertiesDict.writeObject
                (
                    IOstream::ASCII,
                    IOstream::currentVersion,
                    IOstream::UNCOMPRESSED,
                    true
                );

                WarningInFunction
                    << "Upgrading case by "
                       "converting transportProperties into phaseProperties, "
                    << IOobject::groupName
                       (
                           physicalProperties::typeName,
                           phases[0]
                       )
                    << " and "
                    << IOobject::groupName
                       (
                           physicalProperties::typeName,
                           phases[1]
                       )
                    << nl << endl;

                return phasePropertiesDict;
            }
            else
            {
                return phasePropertiesIO;
            }
        }
    }
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::twoPhaseMixture::twoPhaseMixture(const fvMesh& mesh)
:
    IOdictionary(readPhasePropertiesDict(mesh)),

    phase1Name_(wordList(lookup("phases"))[0]),
    phase2Name_(wordList(lookup("phases"))[1]),

    alpha1_
    (
        IOobject
        (
            IOobject::groupName("alpha", phase1Name_),
            mesh.time().name(),
            mesh,
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        ),
        mesh
    ),

    alpha2_
    (
        IOobject
        (
            IOobject::groupName("alpha", phase2Name_),
            mesh.time().name(),
            mesh
        ),
        1.0 - alpha1_
    )
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

bool Foam::twoPhaseMixture::read()
{
    return regIOobject::read();
}


Foam::tmp<Foam::volScalarField::Internal>
Foam::twoPhaseMixture::interfaceFraction() const
{
    const fvMesh& mesh = alpha1_.mesh();

    tmp<volScalarField::Internal> tA
    (
        volScalarField::Internal::New
        (
            "A",
            mesh,
            dimensionedScalar(dimless, 0)
        )
    );
    volScalarField::Internal& A = tA.ref();

    const surfaceVectorField& Sf = mesh.Sf();
    const labelUList& own = mesh.owner();
    const labelUList& nei = mesh.neighbour();

    const surfaceScalarField alphaf(fvc::interpolate(alpha1_));

    const volVectorField::Internal gradAlpha(fvc::grad(alpha1_));
    const volVectorField::Internal n
    (
        gradAlpha/(mag(gradAlpha) + dimensionedScalar(dimless/dimLength, small))
    );

    const scalarField& ialpha = alpha1_;
    const scalarField& ialphaf = alphaf;
    scalarField sumnSf(mesh.nCells(), 0);

    forAll(own, facei)
    {
        {
            const scalar nSf(mag(n[own[facei]] & Sf[facei]));
            A[own[facei]] += nSf*(ialphaf[facei] - ialpha[own[facei]]);
            sumnSf[own[facei]] += nSf;
        }
        {
            const scalar nSf(mag(n[nei[facei]] & Sf[facei]));
            A[nei[facei]] += nSf*(ialphaf[facei] - ialpha[nei[facei]]);
            sumnSf[nei[facei]] += nSf;
        }
    }

    forAll(mesh.boundary(), patchi)
    {
        const labelUList& own = mesh.boundary()[patchi].faceCells();
        const fvsPatchScalarField& palphaf = alphaf.boundaryField()[patchi];

        forAll(mesh.boundary()[patchi], facei)
        {
            const scalar nSf(mag(n[own[facei]] & Sf[facei]));
            A[own[facei]] += nSf*(palphaf[facei] - ialpha[own[facei]]);
            sumnSf[own[facei]] += nSf;
        }
    }

    scalarField& a = A.primitiveFieldRef();
    forAll(a, i)
    {
        if (sumnSf[i] > small)
        {
            a[i] = 2*mag(a[i])/sumnSf[i];
        }
        else
        {
            a[i] = 0;
        }
    }

    return tA;
}


// ************************************************************************* //
