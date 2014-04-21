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

#pragma once

namespace Type {

HRESULT GetColumnDefinitionString(DBCOLUMNDESC colDesc, CComBSTR &strType);

T_CCI_A_TYPE GetCASTypeA(DBTYPE wType);
T_CCI_U_TYPE GetCASTypeU(DBTYPE wType);
// wType의 pValue를 GetCASTypeA에서 반환한 타입에 맞는 값으로 변환한다.
// 반환된 값을 free를 해주어야 한다.
HRESULT OLEDBValueToCCIValue(DBTYPE wType, T_CCI_A_TYPE* aType, T_CCI_U_TYPE* uType, 
			void *pValue, DBLENGTH dwLength, void** dstValue, DBSTATUS* status, const CComPtr<IDataConvert> &spConvert);

DBTYPE GetOledbTypeFromName(LPCWSTR pwszName);

// szRowValue와 pFindValue의 관계가 CompareOp를 만족하는지 검사한다.
// szRowValue는 c string, pFindValue는 rBinding.wType에 의존하는 값이다.
HRESULT Compare(DBCOMPAREOP CompareOp, DBTYPE wType, const char *szRowValue, const char *szFindValue);

// 각 타입에 대해 CompareOp로 줄 수 있는 값을 반환한다.
long GetFindCompareOps(DBTYPE wType);

/* StaticTypeInfo
 *    column 정의와 상관없이 일정한 정보
 *    ProviderTypes Schema Rowset에서 쓰는 정보
 */
struct StaticTypeInfo
{
	T_CCI_U_TYPE nCCIUType;
	USHORT nOLEDBType;
	ULONG ulSize;
	LPWSTR szPrefix;
	LPWSTR szSuffix;
	VARIANT_BOOL bCaseSensitive;
	ULONG ulSearchable;
	VARIANT_BOOL bUnsignedAttribute;
	VARIANT_BOOL bIsFixedLength;
	VARIANT_BOOL bIsLong;
	VARIANT_BOOL bFixedPrecScale;
};

// CCI CUBRID type에 대응하는 StaticTypeInfo의 참조를 반환한다.
const StaticTypeInfo &GetStaticTypeInfo(T_CCI_U_TYPE nCCIUType);

inline const StaticTypeInfo &GetStaticTypeInfo(int nCCIUType)
{
	return GetStaticTypeInfo((T_CCI_U_TYPE)CCI_GET_COLLECTION_DOMAIN(nCCIUType));
}

inline const StaticTypeInfo &GetStaticTypeInfo(const T_CCI_COL_INFO *info, int iOrdinal)
{
	return GetStaticTypeInfo((int)CCI_GET_RESULT_INFO_TYPE(info, iOrdinal));
}

/* DynamicTypeInfo
 *    column 정의에 따라 바뀌는 정보
 *    IColumnsInfo등에서 쓰는 정보
 */
struct DynamicTypeInfo
{
	ULONG ulColumnSize;
	ULONG ulFlags;
	BYTE bPrecision;
	BYTE bScale;
	ULONG ulCharMaxLength;
	ULONG ulCharOctetLength;
	USHORT nNumericPrecision;
	short nNumericScale;
	ULONG ulDateTimePrecision;
};

DynamicTypeInfo GetDynamicTypeInfo(T_CCI_U_TYPE nCCIUType, int nPrecision, int nScale, bool bNullable);

inline DynamicTypeInfo GetDynamicTypeInfo(int nCCIUType, int nPrecision, int nScale, bool bNullable)
{
	return GetDynamicTypeInfo((T_CCI_U_TYPE)CCI_GET_COLLECTION_DOMAIN(nCCIUType),
							  nPrecision, nScale, bNullable);
}

inline DynamicTypeInfo GetDynamicTypeInfo(const T_CCI_COL_INFO *info, int iOrdinal)
{
	//SELECT "ABC" FROM 과 같이 상수를 selection하는 경우 precision값이 -1이 나온다.
	//이 경우는 컬럼 이름의 길이를 precision으로 넘겨 준다.
	int nPrecision = CCI_GET_RESULT_INFO_PRECISION(info, iOrdinal);
	if(nPrecision==-1) nPrecision = (int)strlen(CCI_GET_RESULT_INFO_NAME(info, iOrdinal));
	return GetDynamicTypeInfo(
				(int)CCI_GET_RESULT_INFO_TYPE(info, iOrdinal),
				nPrecision,
				CCI_GET_RESULT_INFO_SCALE(info, iOrdinal),
				CCI_GET_RESULT_INFO_IS_NON_NULL(info, iOrdinal)==0);
}

struct TableInfo
{
	DBORDINAL iOrdinal;
	CComVariant varDefault;
	CComBSTR strSourceClass;
	bool bIsUnique;
};

// 값이 거의 문자열이므로 구조체 복사 비용이 크다.
// 그래서 반환값 대신 인자로 넘겨주도록 했다.
HRESULT GetTableInfo(int hConn, char *szTableName, CAtlArray<TableInfo> &infos);

} // end of namespace Type
