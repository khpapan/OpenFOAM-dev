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

#include "fvScalarMatrix.H"
#include "Residuals.H"
#include "extrapolatedCalculatedFvPatchFields.H"

// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

template<>
void Foam::fvMatrix<Foam::scalar>::setComponentReference
(
    const label patchi,
    const label facei,
    const direction,
    const scalar value
)
{
    if (psi_.needReference())
    {
        if (Pstream::master())
        {
            internalCoeffs_[patchi][facei] +=
                diag()[psi_.mesh().boundary()[patchi].faceCells()[facei]];

            boundaryCoeffs_[patchi][facei] +=
                diag()[psi_.mesh().boundary()[patchi].faceCells()[facei]]
               *value;
        }
    }
}


template<>
Foam::autoPtr<Foam::fvMatrix<Foam::scalar>::fvSolver>
Foam::fvMatrix<Foam::scalar>::solver
(
    const dictionary& solverControls
)
{
    if (debug)
    {
        Info(this->mesh().comm())
            << "fvMatrix<scalar>::solver(const dictionary& solverControls) : "
               "solver for fvMatrix<scalar>"
            << endl;
    }

    scalarField saveDiag(diag());
    addBoundaryDiag(diag(), 0);

    autoPtr<fvMatrix<scalar>::fvSolver> solverPtr
    (
        new fvMatrix<scalar>::fvSolver
        (
            *this,
            lduMatrix::solver::New
            (
                psi_.name(),
                *this,
                boundaryCoeffs_,
                internalCoeffs_,
                psi_.boundaryField().scalarInterfaces(),
                solverControls
            )
        )
    );

    diag() = saveDiag;

    return solverPtr;
}


template<>
Foam::solverPerformance Foam::fvMatrix<Foam::scalar>::fvSolver::solve
(
    const dictionary& solverControls
)
{
    VolField<scalar>& psi =
        const_cast<VolField<scalar>&>
        (fvMat_.psi());

    scalarField saveDiag(fvMat_.diag());
    fvMat_.addBoundaryDiag(fvMat_.diag(), 0);

    scalarField totalSource(fvMat_.source());
    fvMat_.addBoundarySource(totalSource, false);

    // Assign new solver controls
    solver_->read(solverControls);

    solverPerformance solverPerf = solver_->solve
    (
        psi.primitiveFieldRef(),
        totalSource
    );

    if (solverPerformance::debug)
    {
        solverPerf.print(Info(fvMat_.mesh().comm()));
    }

    fvMat_.diag() = saveDiag;

    psi.correctBoundaryConditions();

    Residuals<scalar>::append(psi.mesh(), solverPerf);

    return solverPerf;
}


template<>
Foam::solverPerformance Foam::fvMatrix<Foam::scalar>::solveSegregated
(
    const dictionary& solverControls
)
{
    if (debug)
    {
        Info(this->mesh().comm())
            << "fvMatrix<scalar>::solveSegregated"
               "(const dictionary& solverControls) : "
               "solving fvMatrix<scalar>"
            << endl;
    }

    VolField<scalar>& psi =
       const_cast<VolField<scalar>&>(psi_);

    scalarField saveDiag(diag());
    addBoundaryDiag(diag(), 0);

    scalarField totalSource(source_);
    addBoundarySource(totalSource, false);

    // Solver call
    solverPerformance solverPerf = lduMatrix::solver::New
    (
        psi.name(),
        *this,
        boundaryCoeffs_,
        internalCoeffs_,
        psi_.boundaryField().scalarInterfaces(),
        solverControls
    )->solve(psi.primitiveFieldRef(), totalSource);

    if (solverPerformance::debug)
    {
        solverPerf.print(Info(mesh().comm()));
    }

    diag() = saveDiag;

    psi.correctBoundaryConditions();

    Residuals<scalar>::append(psi.mesh(), solverPerf);

    return solverPerf;
}


template<>
Foam::tmp<Foam::scalarField> Foam::fvMatrix<Foam::scalar>::residual() const
{
    scalarField boundaryDiag(psi_.size(), 0.0);
    addBoundaryDiag(boundaryDiag, 0);

    tmp<scalarField> tres
    (
        lduMatrix::residual
        (
            psi_.primitiveField(),
            source_ - boundaryDiag*psi_.primitiveField(),
            boundaryCoeffs_,
            psi_.boundaryField().scalarInterfaces(),
            0
        )
    );

    addBoundarySource(tres.ref());

    return tres;
}


template<>
Foam::tmp<Foam::volScalarField> Foam::fvMatrix<Foam::scalar>::H() const
{
    tmp<volScalarField> tHphi
    (
        volScalarField::New
        (
            "H("+psi_.name()+')',
            psi_.mesh(),
            dimensions_/dimVolume,
            extrapolatedCalculatedFvPatchScalarField::typeName
        )
    );
    volScalarField& Hphi = tHphi.ref();

    Hphi.primitiveFieldRef() = (lduMatrix::H(psi_.primitiveField()) + source_);
    addBoundarySource(Hphi.primitiveFieldRef());

    Hphi.primitiveFieldRef() /= psi_.mesh().V();
    Hphi.correctBoundaryConditions();

    return tHphi;
}


template<>
Foam::tmp<Foam::volScalarField> Foam::fvMatrix<Foam::scalar>::H1() const
{
    tmp<volScalarField> tH1
    (
        volScalarField::New
        (
            "H(1)",
            psi_.mesh(),
            dimensions_/(dimVolume*psi_.dimensions()),
            extrapolatedCalculatedFvPatchScalarField::typeName
        )
    );
    volScalarField& H1_ = tH1.ref();

    H1_.primitiveFieldRef() = lduMatrix::H1();
    // addBoundarySource(Hphi.primitiveField());

    H1_.primitiveFieldRef() /= psi_.mesh().V();
    H1_.correctBoundaryConditions();

    return tH1;
}


// ************************************************************************* //
