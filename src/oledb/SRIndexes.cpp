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
#include "Error.h"

CSRIndexes::~CSRIndexes()
{
	if(m_spUnkSite)
	{
		CCUBRIDSession* pSession = CCUBRIDSession::GetSessionPtr(this);
		if (pSession->m_cSessionsOpen == 1)	pSession->RowsetCommit();
	}
}

static void GetRestrictions(ULONG cRestrictions, const VARIANT *rgRestrictions, char *table_name, char* index_name)
{
	// restriction이 없다고 항상 cRestrictions==0은 아니다.
	// 따라서 vt!=VT_EMPTY인지 등도 검사해줘야 한다.

	if(cRestrictions>=3 && V_VT(&rgRestrictions[2])==VT_BSTR && V_BSTR(&rgRestrictions[2])!=NULL)
	{	// INDEX_NAME restriction
		CW2A name(V_BSTR(&rgRestrictions[2]));
		ATLTRACE2("\tIndex Name = %s\n", (LPSTR)name);

		strncpy(index_name, name, 255);
		table_name[255] = 0; // ensure zero-terminated string
	}

	if(cRestrictions>=5 && V_VT(&rgRestrictions[4])==VT_BSTR && V_BSTR(&rgRestrictions[4])!=NULL)
	{	// TABLE_NAME restriction
		CW2A name(V_BSTR(&rgRestrictions[4]));
		ATLTRACE2("\tTable Name = %s\n", (LPSTR)name);

		strncpy(table_name, name, 255);
		table_name[255] = 0; // ensure zero-terminated string
	}
}

// S_OK : 성공
// S_FALSE : 데이터는 가져왔지만 Consumer가 원하는 데이터가 아님
// E_FAIL : 실패
static HRESULT FetchData(int hReq, CIndexesRow &irData, const char *index_name)
{
	char *value;
	int ind, res, int_val;
	T_CCI_ERROR err_buf;

	res = cci_fetch(hReq, &err_buf);
	if (res==CCI_ER_NO_MORE_DATA) return S_OK;
	if(res<0) return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);

	res = cci_get_data(hReq, 1, CCI_A_TYPE_INT, &int_val, &ind); //index type
	if(res<0) return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));
	if (int_val == 0)
		irData.m_bUnique = ATL_VARIANT_TRUE;

	res = cci_get_data(hReq, 2, CCI_A_TYPE_STR, &value, &ind); //index name
	if(res<0) return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));

	if (index_name[0] && _stricmp(index_name, value))
		return S_FALSE;
	wcscpy(irData.m_szIndexName, CA2W(value));

	res = cci_get_data(hReq, 3, CCI_A_TYPE_STR, &value, &ind); //attribute name
	if(res<0) return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));
	wcscpy(irData.m_szColumnName, CA2W(value));

	res = cci_get_data(hReq, 6, CCI_A_TYPE_INT, &int_val, &ind);
	if (res < 0)
		return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));

	if (int_val == 1 )
		irData.m_bPrimaryKey = VARIANT_TRUE;
	else
		irData.m_bPrimaryKey = VARIANT_FALSE;

	res = cci_get_data(hReq, 7, CCI_A_TYPE_INT, &int_val, &ind);
	if (res < 0)
		return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));
	irData.m_uOrdinalPosition = int_val;

	return S_OK;
}

static HRESULT GetCardinality(int hConn, WCHAR* tableName, ULARGE_INTEGER* cardinality)
{
	CComBSTR query;
	T_CCI_ERROR error;
	int hReq, res, ind;

	query.Append("select count(*) from [");
	query.Append(tableName);
	query.Append("]");

	hReq = cci_prepare(hConn, CW2A(query.m_str), 0, &error);
	if (hReq < 0)
		return S_FALSE;

	res = cci_execute(hReq, CCI_EXEC_QUERY_ALL, 0, &error);
	if (res < 0)
		return S_FALSE;

	res = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &error);
	if(res<0) return S_FALSE;

	res = cci_fetch(hReq, &error);
	if(res<0) return S_FALSE;

	res = cci_get_data(hReq, 1, CCI_A_TYPE_INT, &cardinality->QuadPart, &ind);
	if(res<0) return S_FALSE;

	cci_close_req_handle(hReq);

	return S_OK;
}

// S_OK : 성공
HRESULT CSRIndexes::FillRowData(int hConn, int hReq, LONG* pcRowsAffected,
								const char *table_name, const char *index_name)
{
	int res;
	T_CCI_ERROR err_buf;
	HRESULT hr = S_OK;

	res = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &err_buf);
	if(res==CCI_ER_NO_MORE_DATA) return S_OK;
	if(res<0) return E_FAIL;

	while(1)
	{
		CIndexesRow irData;
		wcscpy(irData.m_szTableName, CA2W(table_name));
#if 0
		int hReq2, int_val, ind;
#endif

		hr = FetchData(hReq, irData, index_name);
		if(FAILED(hr))
			return E_FAIL;

		HRESULT hrCard = GetCardinality(hConn, irData.m_szTableName, &irData.m_ulCardinality);
		if(FAILED(hrCard))
			return E_FAIL;
#if 0
		//Column Ordinal을 얻어온다.
		if (wcslen(irData.m_szColumnName) > 0)
		{
			hReq2 = cci_schema_info(hConn, CCI_SCH_ATTRIBUTE, (char *)table_name, CW2A(irData.m_szColumnName),
									0, &err_buf);
			if(hReq2<0)
			{
				ATLTRACE2("CSRIndexes: cci_schema_info fail\n");
				return E_FAIL;
			}

			res = cci_cursor(hReq2, 1, CCI_CURSOR_FIRST, &err_buf);
			if (res < 0) return RaiseError(E_FAIL, 3, __uuidof(IDBSchemaRowset), err_buf.err_msg);

			res = cci_fetch(hReq2, &err_buf);
			if(res<0) return RaiseError(E_FAIL, 4, __uuidof(IDBSchemaRowset), err_buf.err_msg);

			res = cci_get_data(hReq2, 10, CCI_A_TYPE_INT, &int_val, &ind); //column ordinal
			irData.m_uOrdinalPosition = int_val;

			cci_close_req_handle(hReq2);
		}
#endif
		if(hr==S_OK) // S_FALSE면 추가하지 않음
		{
			// UNIQUE, INDEX_NAME, TABLE_NAME 순으로 정렬해야 한다.
			size_t nPos;
			for( nPos=0 ; nPos<m_rgRowData.GetCount() ; nPos++ )
			{
				if (m_rgRowData[nPos].m_bUnique < irData.m_bUnique) break;
				if (m_rgRowData[nPos].m_bUnique == irData.m_bUnique)
				{
					int res = CompareStringW(LOCALE_USER_DEFAULT,
							NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH | SORT_STRINGSORT,
							m_rgRowData[nPos].m_szIndexName, -1,
							irData.m_szIndexName, -1);
					if(res==CSTR_GREATER_THAN) break;
					if(res==CSTR_EQUAL)
					{
						res = CompareStringW(LOCALE_USER_DEFAULT,
							NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH | SORT_STRINGSORT,
							m_rgRowData[nPos].m_szTableName, -1,
							irData.m_szTableName, -1);
						if(res==CSTR_GREATER_THAN) break;
					}
				}
			}

			_ATLTRY
			{
				m_rgRowData.InsertAt(nPos, irData);
			}
			_ATLCATCHALL()
			{
				return E_OUTOFMEMORY;
			}

			(*pcRowsAffected)++;
		}

		res = cci_cursor(hReq, 1, CCI_CURSOR_CURRENT, &err_buf);
		if(res==CCI_ER_NO_MORE_DATA) return S_OK;
		if(res<0) return RaiseError(E_FAIL, 5, __uuidof(IDBSchemaRowset), err_buf.err_msg);
	}

	return S_OK;
}

HRESULT CSRIndexes::Execute(LONG* pcRowsAffected,
				ULONG cRestrictions, const VARIANT *rgRestrictions)
{
	ATLTRACE2(atlTraceDBProvider, 2, "CSRIndexes::Execute\n");

	ClearError();

	int hConn = -1;
	HRESULT hr = CCUBRIDSession::GetSessionPtr(this)->GetConnectionHandle(&hConn);
	if(FAILED(hr)) return hr;

	char table_name[256]; table_name[0] = 0;
	char index_name[256]; index_name[0] = 0;
	GetRestrictions(cRestrictions, rgRestrictions, table_name, index_name);

	if (pcRowsAffected)
		*pcRowsAffected = 0;

	CAtlArray<CStringA> rgTableNames;
	if(table_name[0])
		rgTableNames.Add(table_name);
	else
		Util::GetTableNames(hConn, rgTableNames);

	for(size_t i=0;i<rgTableNames.GetCount();i++)
	{
		T_CCI_ERROR err_buf;
		int hReq;
		hReq = cci_schema_info(hConn, CCI_SCH_CONSTRAINT, rgTableNames[i].GetBuffer(),
								NULL, 0, &err_buf);
		if(hReq<0)
		{
			ATLTRACE2("CSRIndexes: cci_schema_info fail\n");
			return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);
		}

		hr = FillRowData(hConn, hReq, pcRowsAffected, rgTableNames[i].GetBuffer(), index_name);
		if(FAILED(hr))
		{
			cci_close_req_handle(hReq);
			return E_FAIL;
		}

		cci_close_req_handle(hReq);
	}

	return S_OK;
}

DBSTATUS CSRIndexes::GetDBStatus(CSimpleRow *pRow, ATLCOLUMNINFO *pInfo)
{
	ATLTRACE2(atlTraceDBProvider, 3, "CSRIndexes::GetDBStatus\n");
	switch(pInfo->iOrdinal)
	{
	case 3: // TABLE_NAME
	case 6: // INDEX_NAME
	case 7: // PRIMARY_KEY
	case 8: // UNIQUE
	case 14:// SORT_BOOKMARKS
	case 15:// AUTO_UPDATE
	case 16:// NULL_COLLATION
	case 17:// ORDINAL_OPSITION
	case 18:// COLUMN_NAME
	case 21:// COLLATION
	case 22:// CARDINALITY
		return DBSTATUS_S_OK;
	default:
		return DBSTATUS_S_ISNULL;
	}
}
