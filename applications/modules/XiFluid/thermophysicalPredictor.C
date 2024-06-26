/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2022-2024 OpenFOAM Foundation
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

#include "XiFluid.H"
#include "fvcSnGrad.H"
#include "fvcLaplacian.H"
#include "fvcDdt.H"
#include "fvcMeshPhi.H"
#include "fvmDiv.H"
#include "fvmSup.H"

// * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * * //

void Foam::solvers::XiFluid::ftSolve
(
    const fv::convectionScheme<scalar>& mvConvection
)
{
    volScalarField& ft = thermo_.Y("ft");

    fvScalarMatrix ftEqn
    (
        fvm::ddt(rho, ft)
      + mvConvection.fvmDiv(phi, ft)
      - fvm::laplacian(thermophysicalTransport.DEff(ft), ft)
     ==
        fvModels().source(rho, ft)
    );

    fvConstraints().constrain(ftEqn);

    ftEqn.solve();

    fvConstraints().constrain(ft);
}


Foam::dimensionedScalar Foam::solvers::XiFluid::StCorr
(
    const volScalarField& c,
    const surfaceScalarField& nf,
    const dimensionedScalar& dMgb
) const
{
    dimensionedScalar StCorr("StCorr", dimless, 1.0);

    if (ign.igniting())
    {
        // Calculate volume of ignition kernel
        const dimensionedScalar Vk
        (
            "Vk",
            dimVolume,
            gSum(c*mesh.V().primitiveField())
        );
        dimensionedScalar Ak("Ak", dimArea, 0.0);

        if (Vk.value() > small)
        {
            // Calculate kernel area from its volume
            // and the dimensionality of the case

            switch(mesh.nGeometricD())
            {
                case 3:
                {
                    // Assume it is part-spherical
                    const scalar sphereFraction
                    (
                        combustionProperties.lookup<scalar>
                        (
                            "ignitionSphereFraction"
                        )
                    );

                    Ak = sphereFraction*4.0*constant::mathematical::pi
                       *pow
                        (
                            3.0*Vk
                           /(sphereFraction*4.0*constant::mathematical::pi),
                            2.0/3.0
                        );
                }
                break;

                case 2:
                {
                    // Assume it is part-circular
                    const dimensionedScalar thickness
                    (
                        combustionProperties.lookup("ignitionThickness")
                    );

                    const scalar circleFraction
                    (
                        combustionProperties.lookup<scalar>
                        (
                            "ignitionCircleFraction"
                        )
                    );

                    Ak = circleFraction*constant::mathematical::pi*thickness
                       *sqrt
                        (
                            4.0*Vk
                           /(
                               circleFraction
                              *thickness
                              *constant::mathematical::pi
                            )
                        );
                }
                break;

                case 1:
                    // Assume it is plane or two planes
                    Ak = dimensionedScalar
                    (
                        combustionProperties.lookup("ignitionKernelArea")
                    );
                break;
            }

            // Calculate kernel area from b field consistent with the
            // discretisation of the b equation.
            const volScalarField mgb
            (
                fvc::div(nf, b, "div(phiSt,b)") - b*fvc::div(nf) + dMgb
            );
            const dimensionedScalar AkEst = gSum(mgb*mesh.V().primitiveField());

            StCorr.value() = max(min((Ak/AkEst).value(), 10.0), 1.0);

            Info<< "StCorr = " << StCorr.value() << endl;
        }
    }

    return StCorr;
}


void Foam::solvers::XiFluid::bSolve
(
    const fv::convectionScheme<scalar>& mvConvection
)
{
    volScalarField& b(b_);
    volScalarField& Xi(Xi_);

    // progress variable
    // ~~~~~~~~~~~~~~~~~
    const volScalarField c("c", scalar(1) - b);

    // Unburnt gas density
    // ~~~~~~~~~~~~~~~~~~~
    const volScalarField rhou(thermo.rhou());

    // Calculate flame normal etc.
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~

    volVectorField n("n", fvc::grad(b));

    volScalarField mgb(mag(n));

    const dimensionedScalar dMgb = 1.0e-3*
        (b*c*mgb)().weightedAverage(mesh.V())
       /((b*c)().weightedAverage(mesh.V()) + small)
      + dimensionedScalar(mgb.dimensions(), small);

    mgb += dMgb;

    const surfaceVectorField SfHat(mesh.Sf()/mesh.magSf());
    surfaceVectorField nfVec(fvc::interpolate(n));
    nfVec += SfHat*(fvc::snGrad(b) - (SfHat & nfVec));
    nfVec /= (mag(nfVec) + dMgb);
    surfaceScalarField nf((mesh.Sf() & nfVec));
    n /= mgb;

    // Calculate turbulent flame speed flux
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    const surfaceScalarField phiSt
    (
        "phiSt",
        fvc::interpolate(rhou*StCorr(c, nf, dMgb)*Su*Xi)*nf
    );

    const scalar StCoNum = max
    (
        mesh.surfaceInterpolation::deltaCoeffs()
       *mag(phiSt)/(fvc::interpolate(rho)*mesh.magSf())
    ).value()*runTime.deltaTValue();

    Info<< "Max St-Courant Number = " << StCoNum << endl;

    // Create b equation
    // ~~~~~~~~~~~~~~~~~
    fvScalarMatrix bEqn
    (
        fvm::ddt(rho, b)
      + mvConvection.fvmDiv(phi, b)
      + fvm::div(phiSt, b)
      - fvm::Sp(fvc::div(phiSt), b)
      - fvm::laplacian(thermophysicalTransport.DEff(b), b)
     ==
        fvModels().source(rho, b)
    );


    // Add ignition cell contribution to b-equation
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    forAll(ign.sites(), i)
    {
        const ignitionSite& ignSite = ign.sites()[i];

        if (ignSite.igniting())
        {
            forAll(ignSite.cells(), icelli)
            {
                label ignCell = ignSite.cells()[icelli];
                Info<< "Igniting cell " << ignCell;

                Info<< " state :"
                    << ' ' << b[ignCell]
                    << ' ' << Xi[ignCell]
                    << ' ' << Su[ignCell]
                    << ' ' << mgb[ignCell]
                    << endl;

                bEqn.diag()[ignSite.cells()[icelli]] +=
                (
                    ignSite.strength()*ignSite.cellVolumes()[icelli]
                   *rhou[ignSite.cells()[icelli]]/ignSite.duration()
                )/(b[ignSite.cells()[icelli]] + 0.001);
            }
        }
    }


    // Solve for b
    // ~~~~~~~~~~~
    bEqn.relax();

    fvConstraints().constrain(bEqn);

    bEqn.solve();

    fvConstraints().constrain(b);

    Info<< "min(b) = " << min(b).value() << endl;


    // Calculate coefficients for Gulder's flame speed correlation
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    const volScalarField up(uPrimeCoef*sqrt((2.0/3.0)*momentumTransport->k()));
    // volScalarField up(sqrt(mag(diag(n * n) & diag(momentumTransport->r()))));

    const volScalarField epsilon
    (
        pow(uPrimeCoef, 3)*momentumTransport->epsilon()
    );

    const volScalarField tauEta(sqrt(thermo.muu()/(rhou*epsilon)));

    const volScalarField Reta
    (
        up
      / (
            sqrt(epsilon*tauEta)
          + dimensionedScalar(up.dimensions(), 1e-8)
        )
    );

    // volScalarField l = 0.337*k*sqrt(k)/epsilon;
    // Reta *= max((l - dimensionedScalar(dimLength, 1.5e-3))/l, 0);

    // Calculate Xi flux
    // ~~~~~~~~~~~~~~~~~
    const surfaceScalarField phiXi
    (
        phiSt
      - fvc::interpolate
        (
            fvc::laplacian(thermophysicalTransport.DEff(b), b)/mgb
        )*nf
      + fvc::interpolate(rho)*fvc::interpolate(Su*(1.0/Xi - Xi))*nf
    );


    // Calculate mean and turbulent strain rates
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    const volVectorField Ut(U + Su*Xi*n);
    const volScalarField sigmat((n & n)*fvc::div(Ut) - (n & fvc::grad(Ut) & n));

    const volScalarField sigmas
    (
        ((n & n)*fvc::div(U) - (n & fvc::grad(U) & n))/Xi
      + (
            (n & n)*fvc::div(Su*n)
          - (n & fvc::grad(Su*n) & n)
        )*(Xi + scalar(1))/(2*Xi)
    );


    // Calculate the unstrained laminar flame speed
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    const volScalarField Su0(unstrainedLaminarFlameSpeed()());


    // Calculate the laminar flame speed in equilibrium with the applied strain
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    const volScalarField SuInf
    (
        Su0*max(scalar(1) - sigmas/sigmaExt, scalar(0.01))
    );

    if (SuModel == "unstrained")
    {
        Su == Su0;
    }
    else if (SuModel == "equilibrium")
    {
        Su == SuInf;
    }
    else if (SuModel == "transport")
    {
        // Solve for the strained laminar flame speed
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

        const volScalarField Rc
        (
            (sigmas*SuInf*(Su0 - SuInf) + sqr(SuMin)*sigmaExt)
            /(sqr(Su0 - SuInf) + sqr(SuMin))
        );

        fvScalarMatrix SuEqn
        (
            fvm::ddt(rho, Su)
          + fvm::div(phi + phiXi, Su, "div(phiXi,Su)")
          - fvm::Sp(fvc::div(phiXi), Su)
          ==
          - fvm::SuSp(-rho*Rc*Su0/Su, Su)
          - fvm::SuSp(rho*(sigmas + Rc), Su)
          + fvModels().source(rho, Su)
        );

        SuEqn.relax();

        fvConstraints().constrain(SuEqn);

        SuEqn.solve();

        fvConstraints().constrain(Su);

        // Limit the maximum Su
        // ~~~~~~~~~~~~~~~~~~~~
        Su.min(SuMax);
        Su.max(SuMin);
    }
    else
    {
        FatalErrorInFunction
            << "Unknown Su model " << SuModel
            << abort(FatalError);
    }


    // Calculate Xi according to the selected flame wrinkling model
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    if (XiModel == "fixed")
    {
        // Do nothing, Xi is fixed!
    }
    else if (XiModel == "algebraic")
    {
        // Simple algebraic model for Xi based on Gulders correlation
        // with a linear correction function to give a plausible profile for Xi
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        Xi == scalar(1) +
            (scalar(1) + (2*XiShapeCoef)*(scalar(0.5) - b))
           *XiCoef*sqrt(up/(Su + SuMin))*Reta;
    }
    else if (XiModel == "transport")
    {
        // Calculate Xi transport coefficients based on Gulders correlation
        // and DNS data for the rate of generation
        // with a linear correction function to give a plausible profile for Xi
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

        const volScalarField XiEqStar
        (
            scalar(1.001) + XiCoef*sqrt(up/(Su + SuMin))*Reta
        );

        const volScalarField XiEq
        (
            scalar(1.001)
          + (
                scalar(1)
              + (2*XiShapeCoef)
               *(scalar(0.5) - min(max(b, scalar(0)), scalar(1)))
            )*(XiEqStar - scalar(1.001))
        );

        const volScalarField Gstar(0.28/tauEta);
        const volScalarField R(Gstar*XiEqStar/(XiEqStar - scalar(1)));
        const volScalarField G(R*(XiEq - scalar(1.001))/XiEq);

        // R *= (Gstar + 2*mag(dev(symm(fvc::grad(U)))))/Gstar;

        // Solve for the flame wrinkling
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        fvScalarMatrix XiEqn
        (
            fvm::ddt(rho, Xi)
          + fvm::div(phi + phiXi, Xi, "div(phiXi,Xi)")
          - fvm::Sp(fvc::div(phiXi), Xi)
         ==
            rho*R
          - fvm::Sp(rho*(R - G), Xi)
          - fvm::Sp
            (
                rho*max
                (
                    sigmat - sigmas,
                    dimensionedScalar(sigmat.dimensions(), 0)
                ),
                Xi
            )
          + fvModels().source(rho, Xi)
        );

        XiEqn.relax();

        fvConstraints().constrain(XiEqn);

        XiEqn.solve();

        fvConstraints().constrain(Xi);

        // Correct boundedness of Xi
        // ~~~~~~~~~~~~~~~~~~~~~~~~~
        Xi.max(1.0);
        Info<< "max(Xi) = " << max(Xi).value() << endl;
        Info<< "max(XiEq) = " << max(XiEq).value() << endl;
    }
    else
    {
        FatalErrorInFunction
            << "Unknown Xi model " << XiModel
            << abort(FatalError);
    }

    Info<< "Combustion progress = "
        << 100*(scalar(1) - b)().weightedAverage(mesh.V()).value() << "%"
        << endl;

    St = Xi*Su;
}


void Foam::solvers::XiFluid::EauSolve
(
    const fv::convectionScheme<scalar>& mvConvection
)
{
    volScalarField& heau = thermo_.heu();

    const volScalarField::Internal rhoByRhou(rho()/thermo.rhou()());

    fvScalarMatrix heauEqn
    (
        fvm::ddt(rho, heau) + mvConvection.fvmDiv(phi, heau)
      + rhoByRhou
       *(
            (fvc::ddt(rho, K) + fvc::div(phi, K))()
          + pressureWork
            (
                heau.name() == "eau"
              ? mvConvection.fvcDiv(phi, p/rho)()
              : -dpdt
            )
        )
      + thermophysicalTransport.divq(heau)

        // These terms cannot be used in partially-premixed combustion due to
        // the resultant inconsistency between ft and heau transport.
        // A possible solution would be to solve for ftu as well as ft.
        //- fvm::div(muEff*fvc::grad(b)/(b + 0.001), heau)
        //+ fvm::Sp(fvc::div(muEff*fvc::grad(b)/(b + 0.001)), heau)

     ==
        fvModels().source(rho, heau)
    );

    fvConstraints().constrain(heauEqn);

    heauEqn.solve();

    fvConstraints().constrain(heau);
}


void Foam::solvers::XiFluid::EaSolve
(
    const fv::convectionScheme<scalar>& mvConvection
)
{
    volScalarField& hea = thermo_.he();

    fvScalarMatrix EaEqn
    (
        fvm::ddt(rho, hea) + mvConvection.fvmDiv(phi, hea)
      + fvc::ddt(rho, K) + fvc::div(phi, K)
      + pressureWork
        (
            hea.name() == "ea"
          ? mvConvection.fvcDiv(phi, p/rho)()
          : -dpdt
        )
      + thermophysicalTransport.divq(hea)
     ==
        (
            buoyancy.valid()
          ? fvModels().source(rho, hea) + rho*(U & buoyancy->g)
          : fvModels().source(rho, hea)
        )
    );

    EaEqn.relax();

    fvConstraints().constrain(EaEqn);

    EaEqn.solve();

    fvConstraints().constrain(hea);
}


void Foam::solvers::XiFluid::thermophysicalPredictor()
{
    tmp<fv::convectionScheme<scalar>> mvConvection
    (
        fv::convectionScheme<scalar>::New
        (
            mesh,
            fields,
            phi,
            mesh.schemes().div("div(phi,ft_b_ha_hau)")
        )
    );

    if (thermo_.containsSpecie("ft"))
    {
        ftSolve(mvConvection());
    }

    if (ign.ignited())
    {
        bSolve(mvConvection());
        EauSolve(mvConvection());
    }

    EaSolve(mvConvection());

    if (!ign.ignited())
    {
        thermo_.heu() == thermo.he();
    }

    thermo_.correct();
}


// ************************************************************************* //
