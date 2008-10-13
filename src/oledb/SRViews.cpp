#include "stdafx.h"
#include "Session.h"
#include "Error.h"

CSRViews::~CSRViews()
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
	{	// view_name restriction
		CW2A name(V_BSTR(&rgRestrictions[2]));
		ATLTRACE2("\tView Name = %s\n", (LPSTR)name);

		strncpy(view_name, name, 255);
		view_name[255] = 0; // ensure zero-terminated string
	}
}

// S_OK : 성공
// S_FALSE : 데이터는 가져왔지만 Consumer가 원하는 데이터가 아님
// E_FAIL : 실패
static HRESULT FetchData(int hReq, CViewsRow &vData)
{
	char *value;
	int ind, res;
	T_CCI_ERROR err_buf;
	
	res = cci_fetch(hReq, &err_buf);
	if(res<0) return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);

	res = cci_get_data(hReq, 1, CCI_A_TYPE_STR, &value, &ind);
	if(res<0) return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));
	wcscpy(vData.m_szTableName, CA2W(value));
	
	return S_OK;
}

static HRESULT GetViewDefinition(int hConn, CViewsRow &vData)
{
	char *value;
	int ind, res;
	T_CCI_ERROR err_buf;
	
	int hReq = cci_schema_info(hConn, CCI_SCH_QUERY_SPEC, CW2A(vData.m_szTableName),
			NULL, 0, &err_buf);

	res = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &err_buf);
    if(res==CCI_ER_NO_MORE_DATA)
		return S_OK;

	if(res<0) return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);

	res = cci_fetch(hReq, &err_buf);
	if(res<0) return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);

	res = cci_get_data(hReq, 1, CCI_A_TYPE_STR, &value, &ind);
	if(res<0) return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));
	wcscpy(vData.m_szViewDefinition, CA2W(value));

	// TODO: 맞는 값?
	vData.m_bCheckOption = ATL_VARIANT_FALSE;
	vData.m_bIsUpdatable = ATL_VARIANT_FALSE;

	cci_close_req_handle(hReq);

	return S_OK;
}

HRESULT CSRViews::Execute(LONG* pcRowsAffected,
				ULONG cRestrictions, const VARIANT *rgRestrictions)
{
	ATLTRACE2(atlTraceDBProvider, 2, "CSRViews::Execute\n");

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
		int hReq = cci_schema_info(hConn, CCI_SCH_VCLASS, (view_name[0]?view_name:NULL),
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
			CViewsRow vData;
			hr = FetchData(hReq, vData);
			if(FAILED(hr))
			{
				cci_close_req_handle(hReq);
				return hr;
			}

			hr = GetViewDefinition(hConn, vData);
			if(FAILED(hr))
			{
				cci_close_req_handle(hReq);
				return hr;
			}

			if(hr==S_OK) // S_FALSE면 추가하지 않음
			{
				_ATLTRY
				{
					// TABLE_NAME 순으로 정렬해야 한다.
					size_t nPos;
					for( nPos=0 ; nPos<m_rgRowData.GetCount() ; nPos++ )
					{
						int res = CompareStringW(LOCALE_USER_DEFAULT, 
								NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH | SORT_STRINGSORT,
								m_rgRowData[nPos].m_szTableName, -1,
								vData.m_szTableName, -1);
						if(res==CSTR_GREATER_THAN) break;
					}
					m_rgRowData.InsertAt(nPos, vData);
				}
				_ATLCATCHALL()
				{
					ATLTRACE2("out of memory\n");
					cci_close_req_handle(hReq);
					return E_OUTOFMEMORY;
				}
				if (pcRowsAffected) (*pcRowsAffected)++;
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

DBSTATUS CSRViews::GetDBStatus(CSimpleRow *pRow, ATLCOLUMNINFO *pInfo)
{
	ATLTRACE2(atlTraceDBProvider, 3, "CSRViews::GetDBStatus\n");
	switch(pInfo->iOrdinal)
	{
	case 3: // TABLE_NAME
	case 4: // VIEW_DEFINITION
	case 5: // CHECK_OPTION
	case 6: // IS_UPDATABLE
		return DBSTATUS_S_OK;
	default:
		return DBSTATUS_S_ISNULL;
	}
}
