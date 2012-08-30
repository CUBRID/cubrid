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

CSRViewColumnUsage::~CSRViewColumnUsage()
{
	if(m_spUnkSite)
	{
		CCUBRIDSession* pSession = CCUBRIDSession::GetSessionPtr(this);
		if (pSession->m_cSessionsOpen == 1)	pSession->RowsetCommit();
	}
}

static void GetRestrictions(ULONG cRestrictions, const VARIANT *rgRestrictions, char *view_name)
{
	// restriction이 없다고 항상 cRestrictions==0은 아니다.
	// 따라서 vt!=VT_EMPTY인지 등도 검사해줘야 한다.

	if(cRestrictions>=3 && V_VT(&rgRestrictions[2])==VT_BSTR && V_BSTR(&rgRestrictions[2])!=NULL)
	{	// TABLE_NAME restriction
		CW2A name(V_BSTR(&rgRestrictions[2]));
		ATLTRACE2("\tView Name = %s\n", (LPSTR)name);

		strncpy(view_name, name, 255);
		view_name[255] = 0; // ensure zero-terminated string
	}
}

HRESULT CSRViewColumnUsage::Execute(LONG* pcRowsAffected,
				ULONG cRestrictions, const VARIANT *rgRestrictions)
{
	ATLTRACE2(atlTraceDBProvider, 2, "CSRViewColumnUsage::Execute\n");

	ClearError();

	if (pcRowsAffected)
			*pcRowsAffected = 0;
	
	int hConn = -1;
	HRESULT hr = CCUBRIDSession::GetSessionPtr(this)->GetConnectionHandle(&hConn);
	if(FAILED(hr)) return hr;
	
	char view_name[256]; view_name[0] = 0;
	
	GetRestrictions(cRestrictions, rgRestrictions, view_name);
	{
		T_CCI_ERROR err_buf;
		char current_view_name[256];
		char* buffer;
		int res, ind;
		int hReq, hReq2;

		hReq = cci_schema_info(hConn, CCI_SCH_VCLASS, (view_name[0]?view_name:NULL),
			NULL, CCI_CLASS_NAME_PATTERN_MATCH, &err_buf);
		if(hReq<0)
		{
			ATLTRACE2("cci_schema_info fail\n");
			return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);
		}

		res = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &err_buf);
        if(res==CCI_ER_NO_MORE_DATA) goto done;
		if(res<0) goto error;

		while(1)
		{
			res = cci_fetch(hReq, &err_buf);
			if(res<0) goto error;

			res = cci_get_data(hReq, 1, CCI_A_TYPE_STR, &buffer, &ind);
			if(res<0) goto error;
			strcpy(current_view_name, buffer);
		
			hReq2 = cci_schema_info(hConn, CCI_SCH_ATTRIBUTE, current_view_name,
					NULL, CCI_ATTR_NAME_PATTERN_MATCH, &err_buf);
			if(hReq2<0)
			{
				ATLTRACE2("cci_schema_info fail\n");
				goto error;
			}

			res = cci_cursor(hReq2, 1, CCI_CURSOR_FIRST, &err_buf);
			if(res==CCI_ER_NO_MORE_DATA)
				goto next_view;

			if(res<0) goto error;

			while(1)
			{
				CViewColumnUsageRow tprData;
				wcscpy(tprData.m_szViewName, CA2W(current_view_name));

				res = cci_fetch(hReq2, &err_buf);
				if(res<0) goto error;

				res = cci_get_data(hReq2, 1, CCI_A_TYPE_STR, &buffer, &ind);
				if(res<0) goto error;
				wcscpy(tprData.m_szColumnName, CA2W(buffer));
				
				res = cci_get_data(hReq2, 11, CCI_A_TYPE_STR, &buffer, &ind);
				if(res<0) goto error;
				wcscpy(tprData.m_szTableName, CA2W(buffer));
				
				_ATLTRY
				{
					// VIEW_NAME, TABLE_NAME, COLUMN_NAME 순으로 정렬해야 한다.
					size_t nPos;
					for( nPos=0 ; nPos<m_rgRowData.GetCount() ; nPos++ )
					{
						int res = wcscmp(m_rgRowData[nPos].m_szViewName, tprData.m_szViewName);
						if(res>0) break;
						if (res == 0)
						{
							res = wcscmp(m_rgRowData[nPos].m_szTableName, tprData.m_szTableName);
							if (res>0) break;
							if (res == 0)
							{
								res = wcscmp(m_rgRowData[nPos].m_szColumnName, tprData.m_szColumnName);
								if (res>0) break;
							}
						}
					}
					m_rgRowData.InsertAt(nPos, tprData);
				}
				_ATLCATCHALL()
				{
					ATLTRACE2("out of memory\n");
					cci_close_req_handle(hReq);
					return E_OUTOFMEMORY;
				}
				if (pcRowsAffected) (*pcRowsAffected)++;

				res = cci_cursor(hReq2, 1, CCI_CURSOR_CURRENT, &err_buf);
				if(res==CCI_ER_NO_MORE_DATA) break;
				if(res<0) goto error;
			}
next_view:
			cci_close_req_handle(hReq2);

			res = cci_cursor(hReq, 1, CCI_CURSOR_CURRENT, &err_buf);
			if(res==CCI_ER_NO_MORE_DATA) goto done;
			if(res<0) goto error;
		}

error:
		ATLTRACE2("fail to fetch data\n");
		cci_close_req_handle(hReq);
		cci_close_req_handle(hReq2);
		return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);
done:
		cci_close_req_handle(hReq);
	}

	return S_OK;
}

DBSTATUS CSRViewColumnUsage::GetDBStatus(CSimpleRow *pRow, ATLCOLUMNINFO *pInfo)
{
	ATLTRACE2(atlTraceDBProvider, 3, "CSRViewColumnUsage::GetDBStatus\n");
	switch(pInfo->iOrdinal)
	{
	case 3: // VIEW_NAME
	case 6: // TABLE_NAME
	case 7: // COLUMN_NAME 
		return DBSTATUS_S_OK;
	default:
		return DBSTATUS_S_ISNULL;
	}
}
