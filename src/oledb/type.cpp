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
#include "type.h"
#include "math.h"
#include "ProviderInfo.h"

namespace Type {

#define CCIDATETYPE_DATE		1
#define CCIDATETYPE_TIME		2
#define CCIDATETYPE_TIMESTAMP	3

T_CCI_U_TYPE GetCASTypeUFromProviderType(LPOLESTR provType)
{
	for (int i = 0; i < ProvInfo::size_provider_types; i++)
		if (!_wcsicmp(ProvInfo::provider_types[i].szName, provType))
			return ProvInfo::provider_types[i].nCCIUType;
	return CCI_U_TYPE_UNKNOWN;
}

HRESULT GetColumnDefinitionString(DBCOLUMNDESC colDesc, CComBSTR& strType)
{
	char strColSize[15];
	T_CCI_U_TYPE uType;
	CComBSTR defaultVal;
	ULONG cPropertySets = colDesc.cPropertySets;
	bool bIsFixedLength = false;
	bool bIsNullable = true;
	bool bIsLong = false;
	bool bIsPrimaryKey = false;
	bool bIsUnique = false;
	bool bHasDefault = false;
	HRESULT hr = S_OK;

	if (cPropertySets)
	{
		if (!(colDesc.rgPropertySets))
			return E_INVALIDARG;

		// TODO: CUtlProps를 이용하면 좋지 않을까?

		//DBPROPSET_COLUMN property set이 있기만 하면 에러는 없으며 나머지 property set은 무시
		for (ULONG i = 0; i < cPropertySets; i++)
		{
			if (colDesc.rgPropertySets[i].cProperties &&
				!colDesc.rgPropertySets[i].rgProperties)
				return E_INVALIDARG;

			if (colDesc.rgPropertySets[i].guidPropertySet == DBPROPSET_COLUMN)
			{
				ULONG numProp = colDesc.rgPropertySets[i].cProperties;
				for (ULONG j = 0; j < numProp; j++)
				{
					DBPROPID propID = colDesc.rgPropertySets[i].rgProperties[j].dwPropertyID;
					VARIANT val = colDesc.rgPropertySets[i].rgProperties[j].vValue;
					VARTYPE type = colDesc.rgPropertySets[i].rgProperties[j].vValue.vt;
					DBPROPOPTIONS options =	colDesc.rgPropertySets[i].rgProperties[j].dwOptions;
					DBPROPSTATUS* status = &colDesc.rgPropertySets[i].rgProperties[j].dwStatus;
					
					if (options != DBPROPOPTIONS_REQUIRED &&
						options != DBPROPOPTIONS_OPTIONAL)
					{
						*status = DBPROPSTATUS_BADOPTION;
						return DB_E_ERRORSOCCURRED;
					}

					//프로퍼티에 맞는 타입인지 체크
					if ((propID == DBPROP_COL_FIXEDLENGTH) ||
						(propID == DBPROP_COL_NULLABLE) ||
						(propID == DBPROP_COL_ISLONG) ||
						(propID == DBPROP_COL_PRIMARYKEY) ||
						(propID == DBPROP_COL_UNIQUE))
					{
						if (type != VT_BOOL) //Type Error
						{
							*status = DBPROPSTATUS_BADVALUE;

							if (options == DBPROPOPTIONS_REQUIRED)
							{
								return DB_E_ERRORSOCCURRED;
							} else
								hr = DB_S_ERRORSOCCURRED;
						} else
						{
							switch(propID)
							{
							case DBPROP_COL_FIXEDLENGTH:
								bIsFixedLength = (V_BOOL(&val)==VARIANT_TRUE);
								*status = DBPROPSTATUS_OK;
								break;
							case DBPROP_COL_NULLABLE:
								if (bIsPrimaryKey)
								{
									*status = DBPROPSTATUS_CONFLICTING;

									if (options == DBPROPOPTIONS_REQUIRED)
									{
										return DB_E_ERRORSOCCURRED;
									} else
										hr = DB_S_ERRORSOCCURRED;
								} else
								{
									if (V_BOOL(&val)==VARIANT_FALSE)
										bIsNullable = false;

									*status = DBPROPSTATUS_OK;
								}
								break;
							case DBPROP_COL_ISLONG:
								bIsLong = (V_BOOL(&val)==VARIANT_TRUE);
								*status = DBPROPSTATUS_OK;
								break;
							case DBPROP_COL_PRIMARYKEY:
								if (bIsUnique || bHasDefault)
								{
									*status = DBPROPSTATUS_CONFLICTING;

									if (options == DBPROPOPTIONS_REQUIRED)
									{
										return DB_E_ERRORSOCCURRED;
									} else
										hr = DB_S_ERRORSOCCURRED;
								} else
								{
									bIsPrimaryKey = (V_BOOL(&val)==VARIANT_TRUE);
									*status = DBPROPSTATUS_OK;
								}
								break;
							case DBPROP_COL_UNIQUE:
								if (bIsPrimaryKey)
								{
									*status = DBPROPSTATUS_CONFLICTING;

									if (options == DBPROPOPTIONS_REQUIRED)
									{
										return DB_E_ERRORSOCCURRED;
									} else
										hr = DB_S_ERRORSOCCURRED;
								} else
								{
									bIsUnique = (V_BOOL(&val)==VARIANT_TRUE);
									*status = DBPROPSTATUS_OK;
								}
								break;
							}
						}
					} else if (propID == DBPROP_COL_DEFAULT)
					{
						if (bIsPrimaryKey)
						{
							*status = DBPROPSTATUS_CONFLICTING;

							if (options == DBPROPOPTIONS_REQUIRED)
							{
								return DB_E_ERRORSOCCURRED;
							} else
								hr = DB_S_ERRORSOCCURRED;
						} else
						{
							CComVariant variant(val);
							if (FAILED(variant.ChangeType(VT_BSTR)))
							{
								*status = DBPROPSTATUS_BADVALUE;

								if (options == DBPROPOPTIONS_REQUIRED)
								{
									return DB_E_ERRORSOCCURRED;
								} else
									hr = DB_S_ERRORSOCCURRED;
							} else
							{
								bHasDefault = true;
								defaultVal.Append(variant.bstrVal);
								*status = DBPROPSTATUS_OK;
							}
						}
					}
					else
					{
						*status = DBPROPSTATUS_NOTSUPPORTED;

						if (options == DBPROPOPTIONS_REQUIRED)
							return DB_E_ERRORSOCCURRED;
						else
							hr = DB_S_ERRORSOCCURRED;
					}
				}
			}
			else
			{
				for (ULONG j = 0; j < colDesc.rgPropertySets[i].cProperties; j++)
				{
					colDesc.rgPropertySets[i].rgProperties[j].dwStatus = DBPROPSTATUS_NOTSUPPORTED;

					if (colDesc.rgPropertySets[i].rgProperties[j].dwOptions == DBPROPOPTIONS_REQUIRED)
						return DB_E_ERRORSOCCURRED;
					else
						hr = DB_S_ERRORSOCCURRED;
				}
			}
		} 
	}

	if (colDesc.pTypeInfo)
		return DB_E_BADTYPE;

	if (colDesc.pwszTypeName && wcslen(colDesc.pwszTypeName) > 0)
	{
		/* CUBRID Provider-Specific types
		 L"CHAR"
		 L"VARCHAR"
		 L"NCHAR"
		 L"NCHAR VARYING"
		 L"BIT"
		 L"BIT VARYING"
		 L"NUMERIC"
		 L"INT"
		 L"SMALLINT"
		 L"FLOAT"
		 L"DOUBLE"
		 L"MONETARY"
		 L"DATE"
		 L"TIME"
		 L"TIMESTAMP"
		*/
		LPWSTR pwszTypeName = colDesc.pwszTypeName;
		DBLENGTH colSize = colDesc.ulColumnSize;
		T_CCI_U_TYPE targetType;

		DBTYPE validType = GetOledbTypeFromName(pwszTypeName);
		if (validType != colDesc.wType)
			return DB_E_BADTYPE;

		if (!_wcsicmp(pwszTypeName, L"CHAR") || !_wcsicmp(pwszTypeName, L"VARCHAR"))
		{
			if (bIsFixedLength) targetType = CCI_U_TYPE_CHAR;
			else targetType = CCI_U_TYPE_STRING;

			if (!colSize)
			{
				StaticTypeInfo tInfo = GetStaticTypeInfo(targetType);
				colSize = tInfo.ulSize;
			}

			if (bIsFixedLength)	strType.Append("CHAR");
			else strType.Append("VARCHAR");
			sprintf(strColSize, "(%ld)", colSize);
			strType.Append(strColSize);
		} else if (!_wcsicmp(pwszTypeName, L"NCHAR") || !_wcsicmp(pwszTypeName, L"NCHAR VARYING"))
		{
			if (bIsFixedLength) targetType = CCI_U_TYPE_NCHAR;
			else targetType = CCI_U_TYPE_VARNCHAR;

			if (!colSize)
			{
				StaticTypeInfo tInfo = GetStaticTypeInfo(targetType);
				colSize = tInfo.ulSize;
			} else
				colSize *= 2;
			
			if (bIsFixedLength)	strType.Append("NCHAR");
			else strType.Append("NCHAR VARYING");
			sprintf(strColSize, "(%ld)", colSize);
			strType.Append(strColSize);
		} else if (!_wcsicmp(pwszTypeName, L"BIT") || !_wcsicmp(pwszTypeName, L"BIT VARYING"))
		{
			if (bIsFixedLength) targetType = CCI_U_TYPE_BIT;
			else targetType = CCI_U_TYPE_VARBIT;

			if (!colSize)
			{
				StaticTypeInfo tInfo = GetStaticTypeInfo(targetType);
				colSize = tInfo.ulSize;
			} else
				colSize *= 8;
			
			if (bIsFixedLength)	strType.Append("BIT");
			else strType.Append("BIT VARYING");
			sprintf(strColSize, "(%u)", colSize);
			strType.Append(strColSize);
		} else if (!_wcsicmp(pwszTypeName, L"NUMERIC"))
		{

			if (colDesc.bPrecision < 1 || colDesc.bPrecision > 38)
				return DB_E_BADPRECISION;

			if (colDesc.bPrecision < colDesc.bScale)
				return DB_E_BADSCALE;

			strType.Append(pwszTypeName);
			sprintf(strColSize, "(%ld,%ld)", colDesc.bPrecision, colDesc.bScale);
			strType.Append(strColSize);
		} else if (!_wcsicmp(pwszTypeName, L"INT") || !_wcsicmp(pwszTypeName, L"SMALLINT") ||
					!_wcsicmp(pwszTypeName, L"FLOAT") || !_wcsicmp(pwszTypeName, L"DOUBLE") ||
					!_wcsicmp(pwszTypeName, L"MONETARY") || !_wcsicmp(pwszTypeName, L"DATE") ||
					!_wcsicmp(pwszTypeName, L"TIME") || !_wcsicmp(pwszTypeName, L"TIMESTAMP"))
		{
			strType.Append(pwszTypeName);
		}
	} else
	{		
		uType = GetCASTypeU(colDesc.wType);
		if (uType == CCI_U_TYPE_UNKNOWN)
			return DB_E_BADTYPE;

		DBLENGTH colSize;
		//컬럼 사이즈가 0인 경우는 static type info에서 컬럼 사이즈를 가져온다
		if (!colDesc.ulColumnSize)
		{
			StaticTypeInfo tInfo = GetStaticTypeInfo(uType);
			colSize = tInfo.ulSize;
		} else
			colSize = colDesc.ulColumnSize;

		switch (uType)
		{
		case CCI_U_TYPE_CHAR: 
			sprintf(strColSize, "(%ld)", colSize);
			strType.Append(strColSize);
			break;
		case CCI_U_TYPE_STRING : 
			if (bIsFixedLength)
				strType.Append(L"Char");
			else
				strType.Append(L"Varchar");
			sprintf(strColSize, "(%ld)", colSize);
			strType.Append(strColSize);
			break;
		case CCI_U_TYPE_NCHAR : 
			if (bIsFixedLength)
				strType.Append(L"NChar");
			else
				strType.Append(L"NChar Varying");

			//사용자가 원하는 길이의 두 배로 저장해야 한다
			if (colDesc.ulColumnSize)
				colSize *= 2;

			sprintf(strColSize, "(%ld)", colSize);
			strType.Append(strColSize);
			break;
		case CCI_U_TYPE_BIT : 
			if (bIsFixedLength)
				strType.Append(L"Bit");
			else
				strType.Append(L"Bit Varying");

			//사용자가 원하는 길이의 여덟 배로 저장해야 한다
			if (colDesc.ulColumnSize)
				colSize *= 8;

			sprintf(strColSize, "(%u)", colSize);
			strType.Append(strColSize);
			break;
		case CCI_U_TYPE_NUMERIC : 
			if (colDesc.bPrecision < 1 || colDesc.bPrecision > 38)
				return DB_E_BADPRECISION;

			if (colDesc.bPrecision < colDesc.bScale)
				return DB_E_BADSCALE;

			strType.Append(L"Numeric");
			sprintf(strColSize, "(%ld,%ld)", colDesc.bPrecision, colDesc.bScale);
			strType.Append(strColSize);
			break;
		case CCI_U_TYPE_INT : strType.Append(L"Integer");
			break;
		case CCI_U_TYPE_SHORT : strType.Append(L"Smallint");
			break;
		case CCI_U_TYPE_MONETARY : strType.Append(L"Monetary");
			break;
		case CCI_U_TYPE_FLOAT : strType.Append(L"Float");
			break;
		case CCI_U_TYPE_DOUBLE : strType.Append(L"Double");
			break;
		case CCI_U_TYPE_DATE : strType.Append(L"Date");
			break;
		case CCI_U_TYPE_TIME : strType.Append(L"Time");
			break;
		case CCI_U_TYPE_TIMESTAMP : strType.Append(L"Timestamp");
			break;
		case CCI_U_TYPE_SET : strType.Append(L"SET");
			break;
		case CCI_U_TYPE_MULTISET : strType.Append(L"Multiset");
			break;
		case CCI_U_TYPE_SEQUENCE : strType.Append(L"Sequence");
			break;
		case CCI_U_TYPE_OBJECT : strType.Append(L"Object");
			break;
		default : strType.Append(L"");
		}
	}

	if (bHasDefault)
	{
		strType.Append(L" DEFAULT ");
		strType.Append(defaultVal);
	}
	if (bIsPrimaryKey)
		strType.Append(L" PRIMARY KEY ");
	if (!bIsNullable)
		strType.Append(L" NOT NULL ");
	if (bIsUnique)
		strType.Append(L" UNIQUE ");

	return hr;
}

T_CCI_A_TYPE GetCASTypeA(DBTYPE wType)
{
	switch(wType)
	{
	case DBTYPE_I1:
	case DBTYPE_I2:
	case DBTYPE_I4:
	case DBTYPE_UI1:
	case DBTYPE_UI2:
	case DBTYPE_UI4:
		return CCI_A_TYPE_INT;
	case DBTYPE_I8:
	case DBTYPE_UI8:
		return CCI_A_TYPE_BIGINT;
	case DBTYPE_R4:
		return CCI_A_TYPE_FLOAT;
	case DBTYPE_R8:
		return CCI_A_TYPE_DOUBLE;
	case DBTYPE_BOOL:
	case DBTYPE_BYTES:
		return CCI_A_TYPE_BIT;
	case DBTYPE_DECIMAL:
	case DBTYPE_NUMERIC: 
	case DBTYPE_WSTR:
	case DBTYPE_BSTR:
	case DBTYPE_STR:
		return CCI_A_TYPE_STR;
	case DBTYPE_DBDATE:
	case DBTYPE_DBTIME:
	case DBTYPE_DBTIMESTAMP:
		return CCI_A_TYPE_DATE;
	}
	return CCI_A_TYPE_FIRST;
}

T_CCI_U_TYPE GetCASTypeU(DBTYPE wType)
{
	switch(wType)
	{
	case DBTYPE_I1:
		return CCI_U_TYPE_CHAR;
	case DBTYPE_I2:
		return CCI_U_TYPE_SHORT;
	case DBTYPE_I4:
		return CCI_U_TYPE_INT;
	case DBTYPE_R4:
		return CCI_U_TYPE_FLOAT;
	case DBTYPE_R8:
		return CCI_U_TYPE_DOUBLE;
	case DBTYPE_BOOL:
	case DBTYPE_BYTES:
		return CCI_U_TYPE_BIT;
	case DBTYPE_UI2:
	case DBTYPE_UI4:
	case DBTYPE_UI8:
	case DBTYPE_DECIMAL:
	case DBTYPE_NUMERIC:
		return CCI_U_TYPE_NUMERIC;
	case DBTYPE_DBDATE:
		return CCI_U_TYPE_DATE;
	case DBTYPE_DBTIME:
		return CCI_U_TYPE_TIME;
	case DBTYPE_DBTIMESTAMP:
		return CCI_U_TYPE_TIMESTAMP;
	case DBTYPE_BSTR: 
	case DBTYPE_WSTR:
		return CCI_U_TYPE_NCHAR;
	case DBTYPE_STR:
		return CCI_U_TYPE_STRING;			  
	}
	return CCI_U_TYPE_UNKNOWN;
}

HRESULT OLEDBValueToCCIValue(DBTYPE wType, T_CCI_A_TYPE* aType, T_CCI_U_TYPE* uType,
		void *pValue, DBLENGTH dwLength, void** dstValue, DBSTATUS* status, const CComPtr<IDataConvert> &spConvert)
{
	if(pValue==NULL) return E_FAIL;
	
	switch(wType)
	{
	case DBTYPE_UI1:
	case DBTYPE_I1:
		*dstValue = CoTaskMemAlloc(sizeof(int));
		*(int*)*dstValue = *(BYTE*)pValue;
		break;
	case DBTYPE_I2: 
		*dstValue = CoTaskMemAlloc(sizeof(int));
		*(int*)*dstValue = *(short*)pValue;
		break;
	case DBTYPE_I4:
		*dstValue = CoTaskMemAlloc(sizeof(int));
		*(int*)*dstValue = *(int*)pValue;
		break;
	case DBTYPE_I8:
		/*
		value = CoTaskMemAlloc(sizeof(LONGLONG));
		*(LONGLONG*)value = *(LONGLONG*)pValue;
		break;	
		*/
		*status = DBSTATUS_E_DATAOVERFLOW;
		return DB_E_ERRORSOCCURRED;
	case DBTYPE_R4: 
		*dstValue = CoTaskMemAlloc(sizeof(float));
		*(float*)*dstValue = *(float*)pValue;
		break;	
	case DBTYPE_R8:
		*dstValue = CoTaskMemAlloc(sizeof(double));
		*(double*)*dstValue = *(double*)pValue;
		break;
	case DBTYPE_BOOL:
		*dstValue = CoTaskMemAlloc(sizeof(VARIANT));
		((VARIANT *)*dstValue)->boolVal = ((VARIANT *)pValue)->boolVal;
		break;
	case DBTYPE_BYTES:
		*dstValue = CoTaskMemAlloc(sizeof(T_CCI_BIT));
		((T_CCI_BIT *)*dstValue)->size = (int)dwLength;
		((T_CCI_BIT *)*dstValue)->buf = 0;
		if(pValue && dwLength>0)
		{
			((T_CCI_BIT *)*dstValue)->buf = (char *)CoTaskMemAlloc(dwLength);
			memcpy(((T_CCI_BIT *)*dstValue)->buf, pValue, dwLength);
		}
		break;
	case DBTYPE_BSTR:
	case DBTYPE_WSTR:
		/*{
		WCHAR *src = (WCHAR *)pValue;
		DBLENGTH DstLen;
		DBSTATUS dbStat = DBSTATUS_S_OK;
		DBLENGTH cbMaxLen = dwLength + 1;
		*dstValue = CoTaskMemAlloc(cbMaxLen + 1);
		HRESULT hr = spConvert->DataConvert(wType, DBTYPE_STR, dwLength, &DstLen,
			src, *dstValue, cbMaxLen, dbStat, &dbStat, 0, 0, 0);
		if (FAILED(hr)) return hr;
		}*/
		*dstValue = CoTaskMemAlloc((dwLength + 1) * sizeof(wchar_t));
		strcpy((char *)*dstValue, CW2A((wchar_t *)pValue));
		break;
	case DBTYPE_STR:
		/*{
		char *src = (char *)pValue;
		DBLENGTH DstLen;
		DBSTATUS dbStat = DBSTATUS_S_OK;
		DBLENGTH cbMaxLen = dwLength + 1;
		*dstValue = CoTaskMemAlloc(cbMaxLen + 1);
		HRESULT hr = spConvert->DataConvert(wType, DBTYPE_STR, dwLength, &DstLen,
			src, *dstValue, cbMaxLen, dbStat, &dbStat, 0, 0, 0);
		if (FAILED(hr)) return hr;
		}*/
		*dstValue = CoTaskMemAlloc((dwLength + 1) * sizeof(char));
		strcpy((char *)*dstValue, (char *)pValue);
		break;
	case DBTYPE_DBDATE:
		*dstValue = CoTaskMemAlloc(sizeof(T_CCI_DATE));
		((T_CCI_DATE *)*dstValue)->yr = ((DBDATE *)pValue)->year;
		((T_CCI_DATE *)*dstValue)->mon = ((DBDATE *)pValue)->month;
		((T_CCI_DATE *)*dstValue)->day = ((DBDATE *)pValue)->day;
		break;
	case DBTYPE_DBTIME: 
		*dstValue = CoTaskMemAlloc(sizeof(T_CCI_DATE));
		((T_CCI_DATE *)*dstValue)->hh = ((DBTIME *)pValue)->hour;
		((T_CCI_DATE *)*dstValue)->mm = ((DBTIME *)pValue)->minute;
		((T_CCI_DATE *)*dstValue)->ss = ((DBTIME *)pValue)->second;
		break;
	case DBTYPE_DBTIMESTAMP:
		*dstValue = CoTaskMemAlloc(sizeof(T_CCI_DATE));
		((T_CCI_DATE *)*dstValue)->yr = ((DBTIMESTAMP *)pValue)->year;
		((T_CCI_DATE *)*dstValue)->mon = ((DBTIMESTAMP *)pValue)->month;
		((T_CCI_DATE *)*dstValue)->day = ((DBTIMESTAMP *)pValue)->day;
		((T_CCI_DATE *)*dstValue)->hh = ((DBTIMESTAMP *)pValue)->hour;
		((T_CCI_DATE *)*dstValue)->mm = ((DBTIMESTAMP *)pValue)->minute;
		((T_CCI_DATE *)*dstValue)->ss = ((DBTIMESTAMP *)pValue)->second;
		break;
	case DBTYPE_DECIMAL:
	case DBTYPE_NUMERIC:
		{
			DB_NUMERIC *src = (DB_NUMERIC *)pValue;
			DBLENGTH DstLen;
			DBSTATUS dbStat = DBSTATUS_S_OK;
			DBLENGTH cbMaxLen = 41;
			*dstValue = CoTaskMemAlloc(cbMaxLen + 1);
			spConvert->DataConvert(DBTYPE_NUMERIC, DBTYPE_STR, sizeof(DB_NUMERIC), &DstLen,
				src, *dstValue, cbMaxLen, dbStat, &dbStat, src->precision, src->scale, 0);
		}
		break;
	case DBTYPE_VARIANT:
		{
			VARIANT* var = (VARIANT *)pValue;
			DBTYPE targetType = var->vt;
			DBLENGTH SrcLen = dwLength, DstLen;
			DBSTATUS dbStat = DBSTATUS_S_OK;
			DBLENGTH cbMaxLen = 0;

			switch (targetType)
			{
			case VT_UI1:
			case VT_I1:   
			case VT_I2:   
							*dstValue = CoTaskMemAlloc(sizeof(short));
							*aType = CCI_A_TYPE_INT;
							*uType = CCI_U_TYPE_SHORT;
							break;
			case VT_I4:
			case VT_INT:
			case VT_UINT:
							*dstValue = CoTaskMemAlloc(sizeof(int));
							*aType = CCI_A_TYPE_INT;
							*uType = CCI_U_TYPE_INT;
							break;
			case VT_I8:
							*dstValue = CoTaskMemAlloc(sizeof(__int64));
							*aType = CCI_A_TYPE_BIGINT;
							*uType = CCI_U_TYPE_BIGINT;
							break;
			case VT_R4:
							*dstValue = CoTaskMemAlloc(sizeof(float));
							*aType = CCI_A_TYPE_FLOAT;
							*uType = CCI_U_TYPE_FLOAT;
							break;
			case VT_R8:
							*dstValue = CoTaskMemAlloc(sizeof(double));
							*aType = CCI_A_TYPE_DOUBLE;
							*uType = CCI_U_TYPE_DOUBLE;
							break;
			case VT_DATE:
							*dstValue = CoTaskMemAlloc(sizeof(T_CCI_DATE));
							((T_CCI_DATE *)*dstValue)->yr = ((DBTIMESTAMP *)pValue)->year;
							((T_CCI_DATE *)*dstValue)->mon = ((DBTIMESTAMP *)pValue)->month;
							((T_CCI_DATE *)*dstValue)->day = ((DBTIMESTAMP *)pValue)->day;
							((T_CCI_DATE *)*dstValue)->hh = ((DBTIMESTAMP *)pValue)->hour;
							((T_CCI_DATE *)*dstValue)->mm = ((DBTIMESTAMP *)pValue)->minute;
							((T_CCI_DATE *)*dstValue)->ss = ((DBTIMESTAMP *)pValue)->second;
							*aType = CCI_A_TYPE_DATE;
							*uType = CCI_U_TYPE_TIMESTAMP;
							break;
			case VT_BSTR:   targetType = DBTYPE_STR;
							*dstValue = CoTaskMemAlloc((2*dwLength) * sizeof(char) + 1);
							dwLength *= 2;
							cbMaxLen = dwLength + 1;
							*aType = CCI_A_TYPE_STR;
							*uType = CCI_U_TYPE_STRING;
							break;
			case VT_DECIMAL:
							DB_NUMERIC *src = (DB_NUMERIC *)pValue;
							SrcLen = sizeof(DB_NUMERIC);
							cbMaxLen = 41;
							*dstValue = CoTaskMemAlloc(cbMaxLen + 1);
							targetType = DBTYPE_STR;
							*aType = CCI_A_TYPE_STR;
							*uType = CCI_U_TYPE_NUMERIC;
			}
			
			if (targetType != VT_DATE)
			{
				HRESULT hr = spConvert->DataConvert(DBTYPE_VARIANT, targetType, SrcLen, &DstLen,
					var, *dstValue, cbMaxLen, dbStat, &dbStat, 0, 0, 0);
				if (FAILED(hr)) return hr;
			}
		}
		break;
	}

	return S_OK;
}

DBTYPE GetOledbTypeFromName(LPCWSTR pwszName)
{
	const struct
	{
		LPCWSTR pwszName;
		DBTYPE wType;
	} types[] = {
		{ L"DBTYPE_I1", DBTYPE_I1 },		{ L"INTEGER(1)", DBTYPE_I1 },
		{ L"TINYINT", DBTYPE_I1 },

		{ L"DBTYPE_I2", DBTYPE_I2 },		{ L"INTEGER(2)", DBTYPE_I2 },
		{ L"AUTOINC(2)", DBTYPE_I2 },		{ L"SMALLINT", DBTYPE_I2 },
		{ L"SMALLINDENTITY", DBTYPE_I2 },	{ L"YEAR", DBTYPE_I2 },

		{ L"DBTYPE_I4", DBTYPE_I4 },		{ L"INTEGER(4)", DBTYPE_I4 },
		{ L"AUTOINC(4)", DBTYPE_I4 },		{ L"INTEGER", DBTYPE_I4 },
		{ L"MEDIUMINT", DBTYPE_I4 },		{ L"INT", DBTYPE_I4 },
		{ L"IDENTITY", DBTYPE_I4 },

		{ L"DBTYPE_I8", DBTYPE_I8 },		{ L"INTEGER(8)", DBTYPE_I8 },
		{ L"BIGINT", DBTYPE_I8 },

		{ L"DBTYPE_R4", DBTYPE_R4 },		{ L"FLOAT(4)", DBTYPE_R4 },
		{ L"BFLOAT(4)", DBTYPE_R4 },		{ L"REAL", DBTYPE_R4 },
		{ L"FLOAT", DBTYPE_R4 },			{ L"FLOAT UNSIGNED", DBTYPE_R4 },

		{ L"DBTYPE_R8", DBTYPE_R8 },		{ L"FLOAT(8)", DBTYPE_R8 },
		{ L"BFLOAT(8)", DBTYPE_R8 },		{ L"MONEY", DBTYPE_R8 },
		{ L"DECIMAL", DBTYPE_R8 },			{ L"DOUBLE", DBTYPE_R8 },
		{ L"DOUBLE UNSIGNED", DBTYPE_R8 },	{ L"DOUBLE PRECISION", DBTYPE_R8 },
		{ L"NUMERIC", DBTYPE_R8 },			{ L"NUMERICSA", DBTYPE_R8 },
		{ L"NUMERICSTS", DBTYPE_R8 },		{ L"MONETARY", DBTYPE_R8 },

		{ L"DBTYPE_CY", DBTYPE_CY },		{ L"CURRENCY", DBTYPE_CY },

		{ L"DBTYPE_BOOL", DBTYPE_BOOL },	{ L"LOGICAL", DBTYPE_BOOL },
		//{ L"BIT", DBTYPE_BOOL },

		{ L"DBTYPE_UI1", DBTYPE_UI1 },		{ L"UNSIGNED(1)", DBTYPE_UI1 },
		{ L"UTINYINT", DBTYPE_UI1 },		{ L"TINYINT UNSIGNED", DBTYPE_UI1 },

		{ L"DBTYPE_UI2", DBTYPE_UI2 },		{ L"UNSIGNED(2)", DBTYPE_UI2 },
		{ L"USMALLINT", DBTYPE_UI2 },		{ L"SMALLINT UNSIGNED", DBTYPE_UI2 },

		{ L"DBTYPE_UI4", DBTYPE_UI4 },		{ L"UNSIGNED(4)", DBTYPE_UI4 },
		{ L"UINTEGER", DBTYPE_UI4 },		{ L"INTEGER UNSIGNED", DBTYPE_UI4 },
		{ L"MEDIUMINT UNSIGNED", DBTYPE_UI4 },

		{ L"DBTYPE_UI8", DBTYPE_UI8 },		{ L"UNSIGNED(8)", DBTYPE_UI8 },
		{ L"BIGINT UNSIGNED", DBTYPE_UI8 },	{ L"UBIGINT", DBTYPE_UI8 },

		{ L"DBTYPE_STR", DBTYPE_STR },		{ L"CHARACTER", DBTYPE_STR },
		{ L"LSTRING", DBTYPE_STR },			{ L"ZSTRING", DBTYPE_STR },
		{ L"NOTE", DBTYPE_STR },			{ L"CHAR", DBTYPE_STR },
		{ L"VARCHAR", DBTYPE_STR },			{ L"LONGVARCHAR", DBTYPE_STR },
		{ L"BLOB", DBTYPE_STR },			{ L"TINYBLOB", DBTYPE_STR },
		{ L"MEDIUMBLOB", DBTYPE_STR },		{ L"LONGBLOB", DBTYPE_STR },
		{ L"TEXT", DBTYPE_STR },			{ L"TINYTEXT", DBTYPE_STR },
		{ L"MEDIUMTEXT", DBTYPE_STR },		{ L"LONGTEXT", DBTYPE_STR },
		{ L"NULL", DBTYPE_STR },			{ L"ENUM", DBTYPE_STR },
		{ L"SET", DBTYPE_STR },

		{ L"NCHAR", DBTYPE_WSTR },			{ L"NCHAR VARYING", DBTYPE_WSTR },

		{ L"DBTYPE_BYTES", DBTYPE_BYTES },	{ L"LVAR", DBTYPE_BYTES },
		{ L"LONG VARBINARY", DBTYPE_BYTES },{ L"LONGVARBINARY", DBTYPE_BYTES },
		{ L"BINARY", DBTYPE_BYTES },		{ L"BIT", DBTYPE_BYTES },
		{ L"BIT VARYING", DBTYPE_BYTES },

		{ L"DBTYPE_DBDATE", DBTYPE_DBDATE },{ L"DATE", DBTYPE_DBDATE },

		{ L"DBTYPE_DBTIME", DBTYPE_DBTIME },{ L"TIME", DBTYPE_DBTIME },

		{ L"DBTYPE_DBTIMESTAMP", DBTYPE_DBTIMESTAMP },
		{ L"TIMESTAMP", DBTYPE_DBTIMESTAMP },
		{ L"DATETIME", DBTYPE_DBTIMESTAMP },
	};

	if(pwszName==NULL) return DBTYPE_WSTR;

	for(int i=1;i<sizeof(types)/sizeof(*types);i++)
	{
		if(_wcsicmp(pwszName, types[i].pwszName)==0)
			return types[i].wType;
	}

	return DBTYPE_STR;
}

static HRESULT CompareSTR(DBCOMPAREOP CompareOp, const char *szRowValue, const char *szFindValue)
{
	DBCOMPAREOP LocalOp = CompareOp & ~DBCOMPAREOPS_CASESENSITIVE & ~DBCOMPAREOPS_CASEINSENSITIVE;

	switch(LocalOp)
	{
	case DBCOMPAREOPS_LT:
	case DBCOMPAREOPS_LE:
	case DBCOMPAREOPS_EQ:
	case DBCOMPAREOPS_GE:
	case DBCOMPAREOPS_GT:
	case DBCOMPAREOPS_NE:
		{
			// 디폴트는 case sensitive
			int res;
			if(CompareOp & DBCOMPAREOPS_CASEINSENSITIVE)
				res = _stricmp(szRowValue, szFindValue);
			else
				res = strcmp(szRowValue, szFindValue);

			if( ( LocalOp==DBCOMPAREOPS_LT && res< 0 ) ||
				( LocalOp==DBCOMPAREOPS_LE && res<=0 ) ||
				( LocalOp==DBCOMPAREOPS_EQ && res==0 ) ||
				( LocalOp==DBCOMPAREOPS_GE && res>=0 ) ||
				( LocalOp==DBCOMPAREOPS_GT && res> 0 ) ||
				( LocalOp==DBCOMPAREOPS_NE && res!=0 ) )
				return S_OK;

			return S_FALSE;
		}

	case DBCOMPAREOPS_BEGINSWITH:
		{
			int res;
			size_t len = strlen(szFindValue);
			if(CompareOp & DBCOMPAREOPS_CASEINSENSITIVE)
				res = _strnicmp(szRowValue, szFindValue, len);
			else
				res = strncmp(szRowValue, szFindValue, len);
			return res==0 ? S_OK : S_FALSE;
		}
	case DBCOMPAREOPS_NOTBEGINSWITH:
		{
			int res;
			size_t len = strlen(szFindValue);
			if(CompareOp & DBCOMPAREOPS_CASEINSENSITIVE)
				res = _strnicmp(szRowValue, szFindValue, len);
			else
				res = strncmp(szRowValue, szFindValue, len);
			return res!=0 ? S_OK : S_FALSE;
		}

	// TODO: case insensitive
	case DBCOMPAREOPS_CONTAINS:
		{
			const char *res;
			if(CompareOp & DBCOMPAREOPS_CASEINSENSITIVE)
			{
				CStringA row(szRowValue), find(szFindValue);
				row.MakeUpper(); find.MakeUpper();
				res = strstr(row, find);
			}
			else
				res = strstr(szRowValue, szFindValue);
			return res!=0 ? S_OK : S_FALSE;
		}
	case DBCOMPAREOPS_NOTCONTAINS:
		{
			const char *res;
			if(CompareOp & DBCOMPAREOPS_CASEINSENSITIVE)
			{
				CStringA row(szRowValue), find(szFindValue);
				row.MakeUpper(); find.MakeUpper();
				res = strstr(row, find);
			}
			else
				res = strstr(szRowValue, szFindValue);
			return res==0 ? S_OK : S_FALSE;
		}
	}

	return DB_E_BADCOMPAREOP;
}

static HRESULT CompareI4(DBCOMPAREOP CompareOp, const char *szRowValue, const char *szFindValue)
{
	if( CompareOp!=DBCOMPAREOPS_LT && CompareOp!=DBCOMPAREOPS_LE &&
		CompareOp!=DBCOMPAREOPS_EQ && CompareOp!=DBCOMPAREOPS_NE &&
		CompareOp!=DBCOMPAREOPS_GE && CompareOp!=DBCOMPAREOPS_GT )
		return DB_E_BADCOMPAREOP;

	int nRowValue = atoi(szRowValue);
	int nFindValue = atoi(szFindValue);

	if( ( CompareOp==DBCOMPAREOPS_LT && nRowValue< nFindValue ) ||
		( CompareOp==DBCOMPAREOPS_LE && nRowValue<=nFindValue ) ||
		( CompareOp==DBCOMPAREOPS_EQ && nRowValue==nFindValue ) ||
		( CompareOp==DBCOMPAREOPS_GE && nRowValue>=nFindValue ) ||
		( CompareOp==DBCOMPAREOPS_GT && nRowValue> nFindValue ) ||
		( CompareOp==DBCOMPAREOPS_NE && nRowValue!=nFindValue ) )
		return S_OK;
	return S_FALSE;
}

static HRESULT CompareI2(DBCOMPAREOP CompareOp, const char *szRowValue, const char *szFindValue)
{
	if( CompareOp!=DBCOMPAREOPS_LT && CompareOp!=DBCOMPAREOPS_LE &&
		CompareOp!=DBCOMPAREOPS_EQ && CompareOp!=DBCOMPAREOPS_NE &&
		CompareOp!=DBCOMPAREOPS_GE && CompareOp!=DBCOMPAREOPS_GT )
		return DB_E_BADCOMPAREOP;

	short nRowValue = (short)atoi(szRowValue);
	short nFindValue = (short)atoi(szFindValue);

	if( ( CompareOp==DBCOMPAREOPS_LT && nRowValue< nFindValue ) ||
		( CompareOp==DBCOMPAREOPS_LE && nRowValue<=nFindValue ) ||
		( CompareOp==DBCOMPAREOPS_EQ && nRowValue==nFindValue ) ||
		( CompareOp==DBCOMPAREOPS_GE && nRowValue>=nFindValue ) ||
		( CompareOp==DBCOMPAREOPS_GT && nRowValue> nFindValue ) ||
		( CompareOp==DBCOMPAREOPS_NE && nRowValue!=nFindValue ) )
		return S_OK;
	return S_FALSE;
}

static HRESULT CompareR4(DBCOMPAREOP CompareOp, const char *szRowValue, const char *szFindValue)
{
	if( CompareOp!=DBCOMPAREOPS_LT && CompareOp!=DBCOMPAREOPS_LE &&
		CompareOp!=DBCOMPAREOPS_EQ && CompareOp!=DBCOMPAREOPS_NE &&
		CompareOp!=DBCOMPAREOPS_GE && CompareOp!=DBCOMPAREOPS_GT )
		return DB_E_BADCOMPAREOP;

	float fRowValue = (float)atof(szRowValue);
	float fFindValue = (float)atof(szFindValue);

	if( ( CompareOp==DBCOMPAREOPS_LT && fRowValue< fFindValue ) ||
		( CompareOp==DBCOMPAREOPS_LE && fRowValue<=fFindValue ) ||
		( CompareOp==DBCOMPAREOPS_EQ && fRowValue==fFindValue ) ||
		( CompareOp==DBCOMPAREOPS_GE && fRowValue>=fFindValue ) ||
		( CompareOp==DBCOMPAREOPS_GT && fRowValue> fFindValue ) ||
		( CompareOp==DBCOMPAREOPS_NE && fRowValue!=fFindValue ) )
		return S_OK;
	return S_FALSE;
}

static HRESULT CompareR8(DBCOMPAREOP CompareOp, const char *szRowValue, const char *szFindValue)
{
	if( CompareOp!=DBCOMPAREOPS_LT && CompareOp!=DBCOMPAREOPS_LE &&
		CompareOp!=DBCOMPAREOPS_EQ && CompareOp!=DBCOMPAREOPS_NE &&
		CompareOp!=DBCOMPAREOPS_GE && CompareOp!=DBCOMPAREOPS_GT )
		return DB_E_BADCOMPAREOP;

	double dRowValue = atof(szRowValue);
	double dFindValue = atof(szFindValue);

	if( ( CompareOp==DBCOMPAREOPS_LT && dRowValue< dFindValue ) ||
		( CompareOp==DBCOMPAREOPS_LE && dRowValue<=dFindValue ) ||
		( CompareOp==DBCOMPAREOPS_EQ && dRowValue==dFindValue ) ||
		( CompareOp==DBCOMPAREOPS_GE && dRowValue>=dFindValue ) ||
		( CompareOp==DBCOMPAREOPS_GT && dRowValue> dFindValue ) ||
		( CompareOp==DBCOMPAREOPS_NE && dRowValue!=dFindValue ) )
		return S_OK;
	return S_FALSE;
}

HRESULT Compare(DBCOMPAREOP CompareOp, DBTYPE wType, const char *szRowValue, const char *szFindValue)
{
	switch(wType)
	{
	case DBTYPE_STR:
	case DBTYPE_WSTR:
		return CompareSTR(CompareOp, szRowValue, szFindValue);
		break;

	case DBTYPE_I4:
		return CompareI4(CompareOp, szRowValue, szFindValue);

	case DBTYPE_I2:
		return CompareI2(CompareOp, szRowValue, szFindValue);

	case DBTYPE_R4:
		return CompareR4(CompareOp, szRowValue, szFindValue);

	case DBTYPE_R8:
		return CompareR8(CompareOp, szRowValue, szFindValue);

	default:
		return DB_E_BADCOMPAREOP;
		break;
	}
}

long GetFindCompareOps(DBTYPE wType)
{
	switch(wType)
	{
	case DBTYPE_STR:
	case DBTYPE_WSTR:
		return DBPROPVAL_CO_EQUALITY | DBPROPVAL_CO_STRING | DBPROPVAL_CO_CONTAINS
				| DBPROPVAL_CO_BEGINSWITH | DBPROPVAL_CO_CASEINSENSITIVE;
	case DBTYPE_I4:
	case DBTYPE_I2:
	case DBTYPE_R4:
	case DBTYPE_R8:
		return DBPROPVAL_CO_EQUALITY;
	}
	return 0;
}

// 원래 VARIANT_BOOL은 VARIANT_TRUE와 VARIANT_FALSE만을 갖지만 1을 가지는 경우도 있다.
// 이경우 GetDBStatus에서 DBSTATUS_S_ISNULL을 반환하고, 값은 무시되므로 괜찮다.
static StaticTypeInfo static_type_infos[] = {
	// CCI type,           OLEDB type,     size, prefix, suffix, case sensitive, searchable,        unsigned,      fixed length,  long,          fixed p&s
	{ CCI_U_TYPE_UNKNOWN,  DBTYPE_STR,  4000, L"",   L"",  VARIANT_TRUE, DB_SEARCHABLE,    1,             VARIANT_FALSE, VARIANT_FALSE, VARIANT_TRUE  },
	{ CCI_U_TYPE_CHAR,     DBTYPE_STR,  1073741823, L"'",  L"'", VARIANT_TRUE,  DB_SEARCHABLE,      1,             VARIANT_TRUE,  VARIANT_TRUE,  VARIANT_TRUE  },
	{ CCI_U_TYPE_STRING,   DBTYPE_STR,  1073741823, L"'",  L"'", VARIANT_TRUE,  DB_SEARCHABLE,      1,             VARIANT_FALSE, VARIANT_TRUE,  VARIANT_TRUE  },
	// TODO: NCHAR와 WSTR은 좀 차이가 있다. 그래서 문제가 생길 여지가 좀 있는데
	//       그렇다고 STR으로 할 수는 없어서, WSTR으로 한다.
	{ CCI_U_TYPE_NCHAR,    DBTYPE_WSTR,  536870911/2, L"N'", L"'", VARIANT_TRUE,  DB_SEARCHABLE,      1,             VARIANT_TRUE,  VARIANT_TRUE,  VARIANT_TRUE  },
	{ CCI_U_TYPE_VARNCHAR, DBTYPE_WSTR,  536870911/2, L"N'", L"'", VARIANT_TRUE,  DB_SEARCHABLE,      1,             VARIANT_FALSE, VARIANT_TRUE,  VARIANT_TRUE  },
	// TODO: 크기를 1073741823로 해야 하는지, 268435455 또는 134217727으로 해야 하는지 모르겠음
	//       LTM에서는 'F111...'과 같은 데이터를 만들어서 prefix는 16진수를 쓰는 걸로 함
	{ CCI_U_TYPE_BIT,     DBTYPE_BYTES, 1073741823, L"X'", L"'", VARIANT_FALSE, DB_ALL_EXCEPT_LIKE, 1,             VARIANT_TRUE,  VARIANT_TRUE,  VARIANT_TRUE  },
	{ CCI_U_TYPE_VARBIT,  DBTYPE_BYTES, 1073741823, L"X'", L"'", VARIANT_FALSE, DB_ALL_EXCEPT_LIKE, 1,             VARIANT_FALSE, VARIANT_TRUE,  VARIANT_TRUE  },
	{ CCI_U_TYPE_NUMERIC,  DBTYPE_NUMERIC,      38, L"",   L"",  VARIANT_FALSE, DB_ALL_EXCEPT_LIKE, VARIANT_FALSE, VARIANT_FALSE, VARIANT_FALSE, VARIANT_TRUE  },
	{ CCI_U_TYPE_INT,      DBTYPE_I4,           10, L"",   L"",  VARIANT_FALSE, DB_ALL_EXCEPT_LIKE, VARIANT_FALSE, VARIANT_TRUE,  VARIANT_FALSE, VARIANT_TRUE  },
	{ CCI_U_TYPE_SHORT,    DBTYPE_I2,            5, L"",   L"",  VARIANT_FALSE, DB_ALL_EXCEPT_LIKE, VARIANT_FALSE, VARIANT_TRUE,  VARIANT_FALSE, VARIANT_TRUE  },
	{ CCI_U_TYPE_MONETARY, DBTYPE_R8,           15, L"",   L"",  VARIANT_FALSE, DB_ALL_EXCEPT_LIKE, VARIANT_FALSE, VARIANT_TRUE,  VARIANT_FALSE, VARIANT_FALSE },
	{ CCI_U_TYPE_FLOAT,    DBTYPE_R4,            7, L"",   L"",  VARIANT_FALSE, DB_ALL_EXCEPT_LIKE, VARIANT_FALSE, VARIANT_TRUE,  VARIANT_FALSE, VARIANT_FALSE },
	{ CCI_U_TYPE_DOUBLE,   DBTYPE_R8,           15, L"",   L"",  VARIANT_FALSE, DB_ALL_EXCEPT_LIKE, VARIANT_FALSE, VARIANT_TRUE,  VARIANT_FALSE, VARIANT_FALSE },
	{ CCI_U_TYPE_DATE,     DBTYPE_DBDATE,       10, L"'",  L"'", VARIANT_FALSE, DB_ALL_EXCEPT_LIKE, 1,             VARIANT_TRUE,  VARIANT_FALSE, VARIANT_TRUE  },
	// NOTE: time의 'am', 'pm' 자리까지 생각해서 size를 정하면
	//       LTM은 1/100초 자리로 생각을 함. 그래서 뺌
	{ CCI_U_TYPE_TIME,     DBTYPE_DBTIME,        8, L"'",  L"'", VARIANT_FALSE, DB_ALL_EXCEPT_LIKE, 1,             VARIANT_TRUE,  VARIANT_FALSE, VARIANT_TRUE  },
	{ CCI_U_TYPE_TIMESTAMP, DBTYPE_DBTIMESTAMP, 19, L"'",  L"'", VARIANT_FALSE, DB_ALL_EXCEPT_LIKE, 1,             VARIANT_TRUE,  VARIANT_FALSE, VARIANT_TRUE  },
	// TODO: mapping to DBTYPE_IUnknown?
	{ CCI_U_TYPE_OBJECT,   DBTYPE_STR,  1073741823, L"'",  L"'", VARIANT_TRUE,  DB_SEARCHABLE,      1,             VARIANT_FALSE, VARIANT_TRUE,  VARIANT_TRUE  },
	{ CCI_U_TYPE_MULTISET,   DBTYPE_STR,  1073741823, L"'",  L"'", VARIANT_TRUE,  DB_SEARCHABLE,      1,             VARIANT_FALSE, VARIANT_TRUE,  VARIANT_TRUE  },
	{ CCI_U_TYPE_SET,   DBTYPE_STR,  1073741823, L"'",  L"'", VARIANT_TRUE,  DB_SEARCHABLE,      1,             VARIANT_FALSE, VARIANT_TRUE,  VARIANT_TRUE  },
};

const StaticTypeInfo &GetStaticTypeInfo(T_CCI_U_TYPE nCCIUType)
{
	for(int i=1;i<sizeof(static_type_infos)/sizeof(*static_type_infos);i++)
	{
		if(static_type_infos[i].nCCIUType==nCCIUType)
			return static_type_infos[i];
	}
	return static_type_infos[0];
}

DynamicTypeInfo GetDynamicTypeInfo(T_CCI_U_TYPE nCCIUType, int nPrecision, int nScale, bool bNullable)
{
	/* 스펙엔 'this is the size of the data type'라고 되어 있어서
	 * ulColumnSize = StaticTypeInfo::ulSize (문자열로 봤을 때의 길이) 라고 생각했으나
	 * LTM을 보니 저장공간의 크기를 말하는 것 같다.
	 */
	DynamicTypeInfo dyn_info;
	const StaticTypeInfo &sta_info = GetStaticTypeInfo(nCCIUType);

	dyn_info.ulFlags = 0;
	dyn_info.ulFlags |= DBCOLUMNFLAGS_WRITEUNKNOWN;
	dyn_info.ulFlags |= DBCOLUMNFLAGS_ISFIXEDLENGTH;
	if(bNullable)
		dyn_info.ulFlags |= DBCOLUMNFLAGS_ISNULLABLE | DBCOLUMNFLAGS_MAYBENULL;
	dyn_info.ulColumnSize = (ULONG)~0;
	dyn_info.bPrecision = ~0;
	dyn_info.bScale = ~0;
	dyn_info.ulCharMaxLength = (ULONG)~0;
	dyn_info.ulCharOctetLength = (ULONG)~0;
	dyn_info.nNumericPrecision = (USHORT)~0;
	dyn_info.nNumericScale = -1;
	dyn_info.ulDateTimePrecision = (ULONG)~0;

	switch(nCCIUType)
	{
	case CCI_U_TYPE_STRING:
	case CCI_U_TYPE_VARNCHAR:
		dyn_info.ulFlags &= ~DBCOLUMNFLAGS_ISFIXEDLENGTH; // 주의 break 없음
	case CCI_U_TYPE_CHAR:
	case CCI_U_TYPE_NCHAR:
		if(nCCIUType==CCI_U_TYPE_VARNCHAR || nCCIUType==CCI_U_TYPE_NCHAR)
			dyn_info.ulColumnSize = nPrecision / 2;
		else
			dyn_info.ulColumnSize = nPrecision;

		dyn_info.ulCharMaxLength = dyn_info.ulColumnSize;
		dyn_info.ulCharOctetLength = dyn_info.ulCharMaxLength;
		if(nCCIUType==CCI_U_TYPE_VARNCHAR || nCCIUType==CCI_U_TYPE_NCHAR)
		{
			dyn_info.ulCharOctetLength *= 2; // TODO: 좀 다르긴 하지만 LTM을 위해 일단 2배를 한다.
		}
		break;

	case CCI_U_TYPE_VARBIT:
		dyn_info.ulFlags &= ~DBCOLUMNFLAGS_ISFIXEDLENGTH; // 주의 break 없음
	case CCI_U_TYPE_BIT:
		// TODO: Maximum length니까 올림을 하는게 맞을 것 같은데, 하면 LTM은
		//       BYTE단위로 데이터를 만들어서 BIT로 보면 초과해서 에러가 난다.
		dyn_info.ulColumnSize = (int)(nPrecision/8);
		dyn_info.ulCharMaxLength = dyn_info.ulColumnSize;
		dyn_info.ulCharOctetLength = dyn_info.ulCharMaxLength;
		break;

	case CCI_U_TYPE_NUMERIC:
		dyn_info.ulColumnSize = sizeof(DB_NUMERIC);
		dyn_info.bPrecision = nPrecision;
		dyn_info.bScale = nScale;
		dyn_info.nNumericPrecision = dyn_info.bPrecision;
		dyn_info.nNumericScale = dyn_info.bScale;
		break;

	case CCI_U_TYPE_INT:
		dyn_info.ulColumnSize = 4;
		dyn_info.bPrecision = (BYTE)sta_info.ulSize;
		dyn_info.nNumericPrecision = dyn_info.bPrecision;
		break;

	case CCI_U_TYPE_SHORT:
		dyn_info.ulColumnSize = 2;
		dyn_info.bPrecision = (BYTE)sta_info.ulSize;
		dyn_info.nNumericPrecision = dyn_info.bPrecision;
		break;

	case CCI_U_TYPE_MONETARY:
		dyn_info.ulColumnSize = sizeof(double);
		dyn_info.bPrecision = (BYTE)sta_info.ulSize;
		dyn_info.nNumericPrecision = dyn_info.bPrecision;
		break;

	case CCI_U_TYPE_FLOAT:
		dyn_info.ulColumnSize = sizeof(float);
		dyn_info.bPrecision = (BYTE)sta_info.ulSize;
		dyn_info.nNumericPrecision = dyn_info.bPrecision;
		break;

	case CCI_U_TYPE_DOUBLE:
		dyn_info.ulColumnSize = sizeof(double);
		dyn_info.bPrecision = (BYTE)sta_info.ulSize;
		dyn_info.nNumericPrecision = dyn_info.bPrecision;
		break;

	case CCI_U_TYPE_DATE:
		dyn_info.ulColumnSize = sizeof(DBDATE);
		break;

	case CCI_U_TYPE_TIME:
		dyn_info.ulColumnSize = sizeof(DBTIME);
		break;

	case CCI_U_TYPE_TIMESTAMP:
		dyn_info.ulColumnSize = sizeof(DBTIMESTAMP);
		dyn_info.bPrecision = (BYTE)sta_info.ulSize;
		dyn_info.bScale = 0;
		dyn_info.ulDateTimePrecision = dyn_info.bScale;
		break;

	case CCI_U_TYPE_OBJECT:
		//dyn_info.ulColumnSize = nPrecision;
		//OID 스트링 max length를 15로 보고 수정
		dyn_info.ulColumnSize = 15;
		dyn_info.ulCharMaxLength = dyn_info.ulColumnSize;
		dyn_info.ulCharOctetLength = dyn_info.ulCharMaxLength;
		break;

	case CCI_U_TYPE_SET:
	case CCI_U_TYPE_MULTISET:
		ATLTRACE(atlTraceDBProvider, 2, _T("Here!!\n"));
		dyn_info.ulColumnSize = nPrecision;
		dyn_info.ulCharMaxLength = dyn_info.ulColumnSize;
		dyn_info.ulCharOctetLength = dyn_info.ulCharMaxLength;
		break;
	default:
		dyn_info.ulColumnSize = sta_info.ulSize;
		dyn_info.ulCharMaxLength = sta_info.ulSize;
		dyn_info.ulCharOctetLength = sta_info.ulSize;
		break;
	}

	if(dyn_info.ulColumnSize>8000) // 8000은 atldbcli.h의 CDynamicAccessor가 쓰는 값
		dyn_info.ulFlags |= DBCOLUMNFLAGS_ISLONG;

	return dyn_info;
}

static HRESULT GetTableInfo_FetchData(int hReq, TableInfo &info)
{
	char *strval;
	int intval, ind, res;
	T_CCI_ERROR err_buf;

	res = cci_fetch(hReq, &err_buf);
	if(res<0) return E_FAIL;

	res = cci_get_data(hReq, 12, CCI_A_TYPE_STR, &strval, &ind);
	if(res<0) return E_FAIL;
	info.strSourceClass = CA2W(strval);

	res = cci_get_data(hReq, 10, CCI_A_TYPE_INT, &intval, &ind);
	if(res<0) return E_FAIL;
	info.iOrdinal = intval;

	res = cci_get_data(hReq, 8, CCI_A_TYPE_INT, &intval, &ind);
	if(res<0) return E_FAIL;
	info.bIsUnique = (intval==1);
	::VariantClear(&info.varDefault);
	res = cci_get_data(hReq, 9, CCI_A_TYPE_STR, &strval, &ind);
	if(res<0) return E_FAIL;
	if(ind!=-1)
	{
		CComVariant var = strval;
		var.Detach(&info.varDefault);
	}

	return S_OK;
}

HRESULT GetTableInfo(int hConn, char *szTableName, CAtlArray<TableInfo> &infos)
{
	// 기존의 데이터 모두 삭제
	infos.RemoveAll();

	T_CCI_ERROR err_buf;
	int hReq = cci_schema_info(hConn, CCI_SCH_ATTRIBUTE, szTableName, NULL,
					CCI_CLASS_NAME_PATTERN_MATCH | CCI_ATTR_NAME_PATTERN_MATCH, &err_buf);
	if(hReq<0)
	{
		ATLTRACE2("GetTableInfo: cci_schema_info fail\n");
		return E_FAIL;
	}

	int res = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &err_buf);
	if(res==CCI_ER_NO_MORE_DATA) goto done;
	if(res<0) goto error;

	size_t cur = 0;
	while(1)
	{
		infos.SetCount(infos.GetCount()+1);
		HRESULT hr = GetTableInfo_FetchData(hReq, infos[cur]);
		if(FAILED(hr)) goto error;

		res = cci_cursor(hReq, 1, CCI_CURSOR_CURRENT, &err_buf);
		if(res==CCI_ER_NO_MORE_DATA) goto done;
		if(res<0) goto error;

		cur++;
	}

error:
	ATLTRACE2("GetTableInfo: fail to fetch data\n");
	cci_close_req_handle(hReq);
	infos.RemoveAll();
	return E_FAIL;
done:
	cci_close_req_handle(hReq);
	return S_OK;
}

}

/*
create table ztest (c1 char, c2 char(20), c3 varchar, c4 varchar(20), c5 nchar, c6 nchar(20),
c7 nchar varying, c8 nchar varying(20), c9 bit, c10 bit(20), c11 bit varying, c12 bit varying(20),
c13 numeric, c14 numeric(10), c15 numeric(15,8), c16 int, c17 smallint, c18 float, c19 float(5),
c20 float(14), c21 double, c22 monetary, c23 date, c24 time, c25 timestamp, c26 set(varchar(20), int),
c27 set(), c28 multiset(float, int), c29 list(float,int), c30 sequence())

***** cci_schema_info의 결과값 *****
name, domain, scale, precision, indexed, non_null, shared, unique, default, attr_order, class_name, source_class
c1	1	0	1			0	0	0	0	null	1	ztest	ztest	: character(1)
c2	1	0	20			0	0	0	0	null	2	ztest	ztest	: character(20)
c3	2	0	1073741823	0	0	0	0	null	3	ztest	ztest	: character varying(1073741823)
c4	2	0	20			0	0	0	0	null	4	ztest	ztest	: character varying(20)
c5	3	0	1			0	0	0	0	null	5	ztest	ztest	: national character(1)
c6	3	0	20			0	0	0	0	null	6	ztest	ztest	: national character(20)
c7	4	0	536870911	0	0	0	0	null	7	ztest	ztest	: national character varying(536870911)
c8	4	0	20			0	0	0	0	null	8	ztest	ztest	: national character varying(20)
c9	5	0	1			0	0	0	0	null	9	ztest	ztest	: bit(1)
c10	5	0	20			0	0	0	0	null	10	ztest	ztest	: bit(20)
c11	6	0	1073741823	0	0	0	0	null	11	ztest	ztest	: bit varying(1073741823)
c12	6	0	20			0	0	0	0	null	12	ztest	ztest	: bit varying(20)
c13	7	0	15			0	0	0	0	null	13	ztest	ztest	: numeric(15,0)
c14	7	0	10			0	0	0	0	null	14	ztest	ztest	: numeric(10,0)
c15	7	8	15			0	0	0	0	null	15	ztest	ztest	: numeric(15,8)
c16	8	0	0			0	0	0	0	null	16	ztest	ztest	: integer
c17	9	0	0			0	0	0	0	null	17	ztest	ztest	: smallint
c18	11	0	0			0	0	0	0	null	18	ztest	ztest	: float
c19	11	0	0			0	0	0	0	null	19	ztest	ztest	: float
c20	11	0	0			0	0	0	0	null	20	ztest	ztest	: float
c21	12	0	0			0	0	0	0	null	21	ztest	ztest	: double
c22	10	0	0			0	0	0	0	null	22	ztest	ztest	: monetary
c23	13	0	0			0	0	0	0	null	23	ztest	ztest	: date
c24	14	0	0			0	0	0	0	null	24	ztest	ztest	: time
c25	15	0	0			0	0	0	0	null	25	ztest	ztest	: timestamp
c26	-1	0	0			0	0	0	0	null	26	ztest	ztest	: set_of(character varying(20), integer)
c27	-1	0	0			0	0	0	0	null	27	ztest	ztest	: set_of()
c28	-1	0	0			0	0	0	0	null	28	ztest	ztest	: multiset_of(float, integer)
c29	-1	0	0			0	0	0	0	null	29	ztest	ztest	: sequence_of(float, integer)
c30	-1	0	0			0	0	0	0	null	30	ztest	ztest	: sequence_of()

***** cci_get_result_info의 결과값 *****
type, is_non_null, scale, precision, col_name, real_attr, class_name
1	0	0	1			c1	null	ztest
1	0	0	20			c2	null	ztest
2	0	0	1073741823	c3	null	ztest
2	0	0	20			c4	null	ztest
3	0	0	1			c5	null	ztest
3	0	0	20			c6	null	ztest
4	0	0	536870911	c7	null	ztest
4	0	0	20			c8	null	ztest
5	0	0	1			c9	null	ztest
5	0	0	20			c10	null	ztest
6	0	0	1073741823	c11	null	ztest
6	0	0	20			c12	null	ztest
7	0	0	15			c13	null	ztest
7	0	0	10			c14	null	ztest
7	0	8	15			c15	null	ztest
8	0	0	0			c16	null	ztest
9	0	0	0			c17	null	ztest
11	0	0	0			c18	null	ztest
11	0	0	0			c19	null	ztest
11	0	0	0			c20	null	ztest
12	0	0	0			c21	null	ztest
10	0	0	0			c22	null	ztest
13	0	0	0			c23	null	ztest
14	0	0	0			c24	null	ztest
15	0	0	0			c25	null	ztest
-1	0	0	0			c26	null	ztest
-1	0	0	0			c27	null	ztest
-1	0	0	0			c28	null	ztest
-1	0	0	0			c29	null	ztest
-1	0	0	0			c30	null	ztest
*/
