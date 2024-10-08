/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2012-2024 OpenFOAM Foundation
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

#include "codedFvModel.H"
#include "fvMatrices.H"
#include "dynamicCode.H"
#include "dynamicCodeContext.H"
#include "addToRunTimeSelectionTable.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
namespace fv
{
    defineTypeNameAndDebug(codedFvModel, 0);
    addToRunTimeSelectionTable(fvModel, codedFvModel, dictionary);
}
}


const Foam::wordList Foam::fv::codedFvModel::codeKeys
{
    "codeAddSup",
    "codeAddRhoSup",
    "codeAddAlphaRhoSup",
    "codeInclude",
    "localCode"
};

const Foam::wordList Foam::fv::codedFvModel::codeDictVars
{
    word::null,
    word::null,
    word::null,
    word::null,
    word::null
};


// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

void Foam::fv::codedFvModel::readCoeffs(const dictionary& dict)
{
    fieldName_ = dict.lookup<word>("field");
}


Foam::word Foam::fv::codedFvModel::fieldPrimitiveTypeName() const
{
    #define fieldPrimitiveTypeNameTernary(Type, nullArg)                       \
        mesh().foundObject<VolField<Type>>(fieldName_)                         \
      ? pTraits<Type>::typeName                                                \
      :

    return FOR_ALL_FIELD_TYPES(fieldPrimitiveTypeNameTernary) word::null;
}


void Foam::fv::codedFvModel::prepare
(
    dynamicCode& dynCode,
    const dynamicCodeContext& context
) const
{
    const word primitiveTypeName = fieldPrimitiveTypeName();

    // Set additional rewrite rules
    dynCode.setFilterVariable("typeName", name());
    dynCode.setFilterVariable("TemplateType", primitiveTypeName);
    dynCode.setFilterVariable("SourceType", primitiveTypeName + "Source");

    // compile filtered C template
    dynCode.addCompileFile("codedFvModelTemplate.C");

    // copy filtered H template
    dynCode.addCopyFile("codedFvModelTemplate.H");

    // Make verbose if debugging
    dynCode.setFilterVariable("verbose", Foam::name(bool(debug)));

    // define Make/options
    dynCode.setMakeOptions
    (
        "EXE_INC = -g \\\n"
        "-I$(LIB_SRC)/finiteVolume/lnInclude \\\n"
        "-I$(LIB_SRC)/meshTools/lnInclude \\\n"
        "-I$(LIB_SRC)/sampling/lnInclude \\\n"
        "-I$(LIB_SRC)/fvModels/general/lnInclude \\\n"
        + context.options()
        + "\n\nLIB_LIBS = \\\n"
        + "    -lmeshTools \\\n"
        + "    -lfvModels \\\n"
        + "    -lsampling \\\n"
        + "    -lfiniteVolume \\\n"
        + context.libs()
    );
}


Foam::fvModel& Foam::fv::codedFvModel::redirectFvModel() const
{
    if (!redirectFvModelPtr_.valid())
    {
        updateLibrary(coeffsDict_);

        dictionary constructDict(coeffsDict_);
        constructDict.set("type", name());
        redirectFvModelPtr_ = fvModel::New
        (
            name(),
            mesh(),
            constructDict
        );
    }

    return redirectFvModelPtr_();
}


template<class Type>
void Foam::fv::codedFvModel::addSupType
(
    const VolField<Type>& field,
    fvMatrix<Type>& eqn
) const
{
    if (fieldPrimitiveTypeName() != word::null)
    {
        if (debug)
        {
            Info<< "codedFvModel::addSup for source " << name() << endl;
        }

        redirectFvModel().addSup(field, eqn);
    }
}


template<class Type>
void Foam::fv::codedFvModel::addSupType
(
    const volScalarField& rho,
    const VolField<Type>& field,
    fvMatrix<Type>& eqn
) const
{
    if (fieldPrimitiveTypeName() != word::null)
    {
        if (debug)
        {
            Info<< "codedFvModel::addSup for source " << name() << endl;
        }

        redirectFvModel().addSup(rho, field, eqn);
    }
}


template<class Type>
void Foam::fv::codedFvModel::addSupType
(
    const volScalarField& alpha,
    const volScalarField& rho,
    const VolField<Type>& field,
    fvMatrix<Type>& eqn
) const
{
    if (fieldPrimitiveTypeName() != word::null)
    {
        if (debug)
        {
            Info<< "codedFvModel::addSup for source " << name() << endl;
        }

        redirectFvModel().addSup(alpha, rho, field, eqn);
    }
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::fv::codedFvModel::codedFvModel
(
    const word& name,
    const word& modelType,
    const fvMesh& mesh,
    const dictionary& dict
)
:
    fvModel(name, modelType, mesh, dict),
    codedBase(name, coeffs(dict), codeKeys, codeDictVars),
    fieldName_(word::null),
    coeffsDict_(coeffs(dict))
{
    readCoeffs(coeffsDict_);
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

Foam::wordList Foam::fv::codedFvModel::addSupFields() const
{
    return wordList(1, fieldName_);
}


FOR_ALL_FIELD_TYPES(IMPLEMENT_FV_MODEL_ADD_FIELD_SUP, fv::codedFvModel)


FOR_ALL_FIELD_TYPES(IMPLEMENT_FV_MODEL_ADD_RHO_FIELD_SUP, fv::codedFvModel)


FOR_ALL_FIELD_TYPES
(
    IMPLEMENT_FV_MODEL_ADD_ALPHA_RHO_FIELD_SUP,
    fv::codedFvModel
)


bool Foam::fv::codedFvModel::movePoints()
{
    return redirectFvModel().movePoints();
}


void Foam::fv::codedFvModel::topoChange(const polyTopoChangeMap& map)
{
    redirectFvModel().topoChange(map);
}


void Foam::fv::codedFvModel::mapMesh(const polyMeshMap& map)
{
    redirectFvModel().mapMesh(map);
}


void Foam::fv::codedFvModel::distribute(const polyDistributionMap& map)
{
    redirectFvModel().distribute(map);
}


bool Foam::fv::codedFvModel::read(const dictionary& dict)
{
    if (fvModel::read(dict))
    {
        redirectFvModelPtr_.clear();
        coeffsDict_ = coeffs(dict);
        readCoeffs(coeffsDict_);
        codedBase::read(coeffsDict_);
        updateLibrary(coeffsDict_);
        return true;
    }
    else
    {
        return false;
    }
}


// ************************************************************************* //
