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

#include "patchSummaryTemplates.H"
#include "genericFieldBase.H"
#include "IOmanip.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

template<class PatchField>
Foam::word Foam::patchFieldType(const PatchField& pf)
{
    if (isA<genericFieldBase>(pf))
    {
        return refCast<const genericFieldBase>(pf).actualTypeName();
    }
    else
    {
        return pf.type();
    }
}


template<class GeoField>
void Foam::addToFieldList
(
    PtrList<GeoField>& fieldList,
    const IOobject& obj,
    const label fieldi,
    const typename GeoField::Mesh& mesh
)
{
    if (obj.headerClassName() == GeoField::typeName)
    {
        fieldList.set
        (
            fieldi,
            new GeoField(obj, mesh)
        );
        Info<< "    " << GeoField::typeName << tab << obj.name() << endl;
    }
}


template<class GeoField>
void Foam::outputFieldList
(
    const PtrList<GeoField>& fieldList,
    const label patchi
)
{
    forAll(fieldList, fieldi)
    {
        if (fieldList.set(fieldi))
        {
            Info<< "    " << pTraits<typename GeoField::value_type>::typeName
                << tab << tab
                << fieldList[fieldi].name() << tab << tab
                << patchFieldType(fieldList[fieldi].boundaryField()[patchi])
                << nl;
        }
    }
}


template<class GeoField>
void Foam::collectFieldList
(
    const PtrList<GeoField>& fieldList,
    const label patchi,
    HashTable<word>& fieldToType
)
{
    forAll(fieldList, fieldi)
    {
        if (fieldList.set(fieldi))
        {
            fieldToType.insert
            (
                fieldList[fieldi].name(),
                patchFieldType(fieldList[fieldi].boundaryField()[patchi])
            );
        }
    }
}


// ************************************************************************* //
