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

CSRTablesInfo::~CSRTablesInfo()
{
	if(m_spUnkSite)
	{
		CCUBRIDSession* pSession = CCUBRIDSession::GetSessionPtr(this);
		if (pSession->m_cSessionsOpen == 1)	pSession->RowsetCommit();
	}
}

static void GetRestrictions(ULONG cRestrictions, const VARIANT *rgRestrictions, char *table_name, int* table_type)
{
	// restriction이 없다고 항상 cRestrictions==0은 아니다.
	// 따라서 vt!=VT_EMPTY인지 등도 검사해줘야 한다.

	if(cRestrictions>=3 && V_VT(&rgRestrictions[2])==VT_BSTR && V_BSTR(&rgRestrictions[2])!=NULL)
	{	// TABLE_NAME restriction
		CW2A name(V_BSTR(&rgRestrictions[2]));
		ATLTRACE2("\tTable Name = %s\n", (LPSTR)name);

		strncpy(table_name, name, 255);
		table_name[255] = 0; // ensure zero-terminated string
	}

	if(cRestrictions>=4 && V_VT(&rgRestrictions[3])==VT_BSTR && V_BSTR(&rgRestrictions[3])!=NULL)
	{	// TABLE_NAME restriction
		CW2A name(V_BSTR(&rgRestrictions[3]));
		ATLTRACE2("\tTable Type = %s\n", (LPSTR)name);

		*table_type = 3; // wrong restriction
		if (!strcmp(name, "TABLE"))
			*table_type = 2;
		else if (!strcmp(name, "VIEW"))
			*table_type = 1;
		else if (!strcmp(name, "SYSTEM TABLE"))
			*table_type = 0;
	}
}

// S_OK : 성공
// S_FALSE : 데이터는 가져왔지만 Consumer가 원하는 데이터가 아님
// E_FAIL : 실패
static HRESULT FetchData(int hReq, CTablesInfoRow &tirData, int table_type)
{
	char *value;
	int int_value;
	int ind, res;
	T_CCI_ERROR err_buf;
	
	res = cci_fetch(hReq, &err_buf);
	if(res<0) return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);

	res = cci_get_data(hReq, 1, CCI_A_TYPE_STR, &value, &ind);
	if(res<0) return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));
	wcscpy(tirData.m_szTableName, CA2W(value));
	
	res = cci_get_data(hReq, 2, CCI_A_TYPE_INT, &int_value, &ind);
	if(res<0) return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));

	if (table_type >= 0 && table_type != int_value)
		return S_FALSE;

	if (int_value == 2)
		wcscpy(tirData.m_szTableType, L"TABLE");
	else if (int_value == 1)
		wcscpy(tirData.m_szTableType, L"VIEW");
	else if (int_value == 0)
		wcscpy(tirData.m_szTableType, L"SYSTEM TABLE");

	tirData.m_bBookmarks = ATL_VARIANT_TRUE;
	tirData.m_bBookmarkType = DBPROPVAL_BMK_NUMERIC;
	tirData.m_bBookmarkDatatype = DBTYPE_BYTES;
	tirData.m_bBookmarkMaximumLength = sizeof(DBROWCOUNT);

	return S_OK;
}

//수행도중 에러가 나도 S_OK  리턴
//에러시 cardinality는 0이 된다.
static HRESULT GetCardinality(int hConn, WCHAR* tableName, ULARGE_INTEGER* cardinality)
{
	CComBSTR query;
	T_CCI_ERROR error;
	int hReq, res, ind;

	query.Append("select count(*) from ");
	query.Append(tableName);

	hReq = cci_prepare(hConn, CW2A(query.m_str), 0, &error);
	if (hReq < 0)
		return S_OK;

	res = cci_execute(hReq, CCI_EXEC_QUERY_ALL, 0, &error);
	if (res < 0)
		return S_OK;

	res = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &error);
	if(res<0) return S_OK;

	res = cci_fetch(hReq, &error);
	if(res<0) return S_OK;

	res = cci_get_data(hReq, 1, CCI_A_TYPE_INT, &cardinality->QuadPart, &ind);
	if(res<0) return S_OK;

	cci_close_req_handle(hReq);

	return S_OK;
}

HRESULT CSRTablesInfo::Execute(LONG* pcRowsAffected,
				ULONG cRestrictions, const VARIANT *rgRestrictions)
{
	ATLTRACE2(atlTraceDBProvider, 2, "CSRTablesInfo::Execute\n");

	ClearError();

	int hConn = -1;
	HRESULT hr = CCUBRIDSession::GetSessionPtr(this)->GetConnectionHandle(&hConn);
	if(FAILED(hr)) return hr;
	
	char table_name[256]; table_name[0] = 0;
	int table_type = -1;

	GetRestrictions(cRestrictions, rgRestrictions, table_name, &table_type);
	{
		T_CCI_ERROR err_buf;
		int hReq = cci_schema_info(hConn, CCI_SCH_CLASS, (table_name[0]?table_name:NULL),
			NULL, CCI_CLASS_NAME_PATTERN_MATCH, &err_buf);
		if(hReq<0)
		{
			ATLTRACE2("cci_schema_info fail\n");
			return E_FAIL;
		}

		int res = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &err_buf);
        if(res==CCI_ER_NO_MORE_DATA) goto done;
		if(res<0) goto error;

		while(1)
		{
			CTablesInfoRow tirData;
			hr = FetchData(hReq, tirData, table_type);
			if(FAILED(hr))
			{
				cci_close_req_handle(hReq);
				return hr;
			}
			HRESULT hrCard = GetCardinality(hConn, tirData.m_szTableName, &tirData.m_ulCardinality);
			if(FAILED(hrCard))
			{
				cci_close_req_handle(hReq);
				return hrCard;
			};

			if(hr==S_OK) // S_FALSE면 추가하지 않음
			{
				_ATLTRY
				{
					// TABLE_TYPE, TABLE_NAME 순으로 정렬해야 한다.
					size_t nPos;
					for( nPos=0 ; nPos<m_rgRowData.GetCount() ; nPos++ )
					{
						int res = CompareStringW(LOCALE_USER_DEFAULT, 
								NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH | SORT_STRINGSORT,
								m_rgRowData[nPos].m_szTableType, -1,
								tirData.m_szTableType, -1);
						if(res==CSTR_GREATER_THAN) break;
						if(res==CSTR_EQUAL)
						{
							res = CompareStringW(LOCALE_USER_DEFAULT, 
									NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH | SORT_STRINGSORT,
									m_rgRowData[nPos].m_szTableName, -1,
									tirData.m_szTableName, -1);
							if(res==CSTR_GREATER_THAN) break;
						}
					}
					m_rgRowData.InsertAt(nPos, tirData);
				}
				_ATLCATCHALL()
				{
					ATLTRACE2("out of memory\n");
					cci_close_req_handle(hReq);
					return E_OUTOFMEMORY;
				}
			}

			res = cci_cursor(hReq, 1, CCI_CURSOR_CURRENT, &err_buf);
			if(res==CCI_ER_NO_MORE_DATA) goto done;
			if(res<0) goto error;
		}

error:
		ATLTRACE2("fail to fetch data\n");
		cci_close_req_handle(hReq);
		return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);
done:
		cci_close_req_handle(hReq);
	}

	return S_OK;
}

DBSTATUS CSRTablesInfo::GetDBStatus(CSimpleRow *pRow, ATLCOLUMNINFO *pInfo)
{
	ATLTRACE2(atlTraceDBProvider, 3, "CSRTablesInfo::GetDBStatus\n");
	switch(pInfo->iOrdinal)
	{
	case 3: // TABLE_NAME
	case 4: // TABLE_TYPE
	case 6: // BOOKMARKS
	case 7: // BOOKMARK_TYPE
	case 8: // BOOKMARK_DATATYPE
	case 9: // BOOKMARK_MAXIMUM_LENGTH
	case 12: // CARDINALITY
		return DBSTATUS_S_OK;
	default:
		return DBSTATUS_S_ISNULL;
	}
}
