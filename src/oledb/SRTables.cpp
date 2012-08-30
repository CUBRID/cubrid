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

static const int TABLE_TYPE_TABLE = 0x1;
static const int TABLE_TYPE_VIEW = 0x2;
static const int TABLE_TYPE_SYSTEM = 0x4;

CSRTables::~CSRTables()
{
	if(m_spUnkSite)
	{
		CCUBRIDSession* pSession = CCUBRIDSession::GetSessionPtr(this);
		if (pSession->m_cSessionsOpen == 1)	pSession->RowsetCommit();
	}
}

static void GetRestrictions(ULONG cRestrictions, const VARIANT *rgRestrictions,
							char *table_name, int *table_type)
{
	// restriction이 없다고 항상 cRestrictions==0은 아니다.
	// 따라서 vt!=VT_EMPTY인지 등도 검사해줘야 한다.

	// TABLE_NAME restriction
	if(cRestrictions>=3 && rgRestrictions[2].vt==VT_BSTR
		&& rgRestrictions[2].bstrVal!=NULL)
	{
		CW2A name(rgRestrictions[2].bstrVal);
		ATLTRACE2("\tName = %s\n", (LPSTR)name);

		strncpy(table_name, name, 1023);
		table_name[1023] = 0; // ensure zero-terminated string
	}

	// TABLE_TYPE restriction
	if(cRestrictions>=4 && rgRestrictions[3].vt==VT_BSTR
		&& rgRestrictions[3].bstrVal!=NULL)
	{
		CW2A type(rgRestrictions[3].bstrVal);
		ATLTRACE2("\tType = %s\n", (LPSTR)type);

		*table_type = 0;
		if(strcmp(type, "TABLE")==0) *table_type |= TABLE_TYPE_TABLE;
		if(strcmp(type, "VIEW")==0) *table_type |= TABLE_TYPE_VIEW;
		if(strcmp(type, "SYSTEM TABLE")==0) *table_type |= TABLE_TYPE_SYSTEM;
	}
}

// S_OK : 성공
// S_FALSE : 데이터는 가져왔지만 Consumer가 원하는 데이터가 아님
// E_FAIL : 실패
static HRESULT FetchData(int hReq, CTABLESRow &trData, int table_type)
{
	char *name;
	int type, ind, res;
	T_CCI_ERROR err_buf;

	res = cci_fetch(hReq, &err_buf);
	if(res<0) return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);

	res = cci_get_data(hReq, 1, CCI_A_TYPE_STR, &name, &ind);
	if(res<0) return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));
	CA2W nametmp(name);
	wcsncpy(trData.m_szTable, nametmp, 128);
	trData.m_szTable[128] = 0;
	_wcsupr(trData.m_szTable);

	res = cci_get_data(hReq, 2, CCI_A_TYPE_INT, &type, &ind);
	if(res<0) return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));

	if((type==0 && (table_type & TABLE_TYPE_SYSTEM))
		|| (type==1 && (table_type & TABLE_TYPE_VIEW))
		|| (type==2 && (table_type & TABLE_TYPE_TABLE)))
	{
		switch(type)
		{
		case 0:
			wcscpy(trData.m_szType, L"SYSTEM TABLE");
			wcscpy(trData.m_szDesc, L"System Class");
			break;
		case 1:
			wcscpy(trData.m_szType, L"VIEW");
			wcscpy(trData.m_szDesc, L"Virtual Class");
			break;
		case 2:
			wcscpy(trData.m_szType, L"TABLE");
			wcscpy(trData.m_szDesc, L"Class");
			break;
		}
	}
	else
		return S_FALSE;

	return S_OK;
}

HRESULT CSRTables::Execute(LONG * /*pcRowsAffected*/,
				ULONG cRestrictions, const VARIANT *rgRestrictions)
{
	ATLTRACE2(atlTraceDBProvider, 2, "CSRTables::Execute\n");

	ClearError();

	int hConn = -1;
	HRESULT hr = CCUBRIDSession::GetSessionPtr(this)->GetConnectionHandle(&hConn);
	if(FAILED(hr)) return hr;

	char table_name[1024]; table_name[0] = 0;
	int table_type = TABLE_TYPE_TABLE | TABLE_TYPE_VIEW | TABLE_TYPE_SYSTEM;
	GetRestrictions(cRestrictions, rgRestrictions, table_name, &table_type);

	{
		T_CCI_ERROR err_buf;
		int hReq = cci_schema_info(hConn, CCI_SCH_CLASS, (table_name[0]?table_name:NULL),
							NULL, CCI_CLASS_NAME_PATTERN_MATCH, &err_buf);
		if(hReq<0)
		{
			ATLTRACE2("cci_schema_info fail\n");
			return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);
		}

		int res = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &err_buf);
		if(res==CCI_ER_NO_MORE_DATA) goto done;
		if(res<0) goto error;

		while(1)
		{
			CTABLESRow trData;
			hr = FetchData(hReq, trData, table_type);
			if(FAILED(hr))
			{
				cci_close_req_handle(hReq);
				return hr;
			}

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
								m_rgRowData[nPos].m_szType, -1,
								trData.m_szType, -1);
						if(res==CSTR_GREATER_THAN) break;
						if(res==CSTR_EQUAL)
						{
							res = CompareStringW(LOCALE_USER_DEFAULT, 
									NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH | SORT_STRINGSORT,
									m_rgRowData[nPos].m_szTable, -1,
									trData.m_szTable, -1);
							if(res==CSTR_GREATER_THAN) break;
						}
					}
					m_rgRowData.InsertAt(nPos, trData);
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

DBSTATUS CSRTables::GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo)
{
	ATLTRACE2(atlTraceDBProvider, 2, _T("CSRTables::GetDBStatus[%d]\n"), pInfo->iOrdinal);
	switch(pInfo->iOrdinal)
	{
	case 3: // TABLE_NAME
	case 4: // TABLE_TYPE
	case 6: // DESCRIPTION
		return DBSTATUS_S_OK;
	default:
		return DBSTATUS_S_ISNULL;
	}
}
