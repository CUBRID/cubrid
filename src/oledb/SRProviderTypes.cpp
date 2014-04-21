/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

#include "stdafx.h"
#include "Session.h"
#include "type.h"
#include "ProviderInfo.h"

HRESULT CSRProviderTypes::Execute(LONG * /*pcRowsAffected*/,
				ULONG cRestrictions, const VARIANT *rgRestrictions)
{
	ATLTRACE2(atlTraceDBProvider, 2, "CSRProviderTypes::Execute\n");

	for(int i=0;i<ProvInfo::size_provider_types;i++)
	{
		CPROVIDER_TYPERow ptrData;
		const Type::StaticTypeInfo &sta_info = Type::GetStaticTypeInfo(ProvInfo::provider_types[i].nCCIUType);

		wcscpy(ptrData.m_szName, ProvInfo::provider_types[i].szName);
		wcscpy(ptrData.m_szCreateParams, ProvInfo::provider_types[i].szCreateParams);

		wcscpy(ptrData.m_szPrefix, sta_info.szPrefix);
		wcscpy(ptrData.m_szSuffix, sta_info.szSuffix);
		ptrData.m_nType = sta_info.nOLEDBType;
		ptrData.m_ulSize = sta_info.ulSize;
		ptrData.m_bCaseSensitive = sta_info.bCaseSensitive;
		ptrData.m_ulSearchable = sta_info.ulSearchable;
		ptrData.m_bUnsignedAttribute = sta_info.bUnsignedAttribute;
		ptrData.m_bIsLong = sta_info.bIsLong;
		ptrData.m_bIsFixedLength = sta_info.bIsFixedLength;
		ptrData.m_bFixedPrecScale = sta_info.bFixedPrecScale;

		ptrData.m_bIsNullable = VARIANT_TRUE;
		if(sta_info.nOLEDBType==DBTYPE_NUMERIC)
		{
			ptrData.m_nMaxScale = (short)sta_info.ulSize;
			ptrData.m_nMinScale = 0;
		}

		_ATLTRY
		{
			// DATA_TYPE 순으로 정렬해야 한다.
			size_t nPos;
			for( nPos=0 ; nPos<m_rgRowData.GetCount() ; nPos++ )
			{
				if(m_rgRowData[nPos].m_nType>ptrData.m_nType) break;
			}
			m_rgRowData.InsertAt(nPos, ptrData);
		}
		_ATLCATCHALL()
		{
			ATLTRACE2("out of memory\n");
			return E_OUTOFMEMORY;
		}
	}

	return S_OK;
}

DBSTATUS CSRProviderTypes::GetDBStatus(CSimpleRow *pRow, ATLCOLUMNINFO *pInfo)
{
	ATLTRACE2(atlTraceDBProvider, 3, "CSRProviderTypes::GetDBStatus\n");
	switch(pInfo->iOrdinal)
	{
	case 1: // TYPE_NAME
	case 2: // DATA_TYPE
	case 3: // COLUMN_SIZE
	case 4: // LITERAL_PREFIX
	case 5: // LITERAL_SUFFIX
	case 7: // IS_NULLABLE
	case 8: // CASE_SENSITIVE
	case 9: // SEARCHABLE
	case 11: // FIXED_PREC_SCALE
	case 19: // IS_LONG
	case 21: // IS_FIXEDLENGTH
		return DBSTATUS_S_OK;
	case 6: // CREATE_PARAMS
		if(wcslen(m_rgRowData[pRow->m_iRowset].m_szCreateParams)==0)
			return DBSTATUS_S_ISNULL;
		else
			return DBSTATUS_S_OK;
	case 10: // UNSIGNED_ATTRIBUTE
		if(m_rgRowData[pRow->m_iRowset].m_bUnsignedAttribute==1)
			return DBSTATUS_S_ISNULL;
		else
			return DBSTATUS_S_OK;
	case 14: // MINIMUM_SCALE
	case 15: // MAXIMUM_SCALE
		if(m_rgRowData[pRow->m_iRowset].m_nType!=DBTYPE_NUMERIC)
			return DBSTATUS_S_ISNULL;
		else
			return DBSTATUS_S_OK;
	default:
		return DBSTATUS_S_ISNULL;
	}
}
