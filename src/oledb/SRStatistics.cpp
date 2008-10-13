#include "stdafx.h"
#include "Session.h"
#include "Error.h"

CSRStatistics::~CSRStatistics()
{
	if(m_spUnkSite)
	{
		CCUBRIDSession* pSession = CCUBRIDSession::GetSessionPtr(this);
		if (pSession->m_cSessionsOpen == 1)	pSession->RowsetCommit();
	}
}

static void GetRestrictions(ULONG cRestrictions, const VARIANT *rgRestrictions, char *table_name)
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
}

// S_OK : 성공
// S_FALSE : 데이터는 가져왔지만 Consumer가 원하는 데이터가 아님
// E_FAIL : 실패
static HRESULT FetchData(int hReq, CStatisticsRow &tprData)
{
	char *value;
	int ind, res;
	T_CCI_ERROR err_buf;
	
	res = cci_fetch(hReq, &err_buf);
	if(res<0) return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);

	res = cci_get_data(hReq, 1, CCI_A_TYPE_STR, &value, &ind);
	if(res<0) return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));
	wcscpy(tprData.m_szTableName, CA2W(value));
	
	return S_OK;
}

static HRESULT GetCardinality(int hConn, WCHAR* tableName, ULARGE_INTEGER* cardinality)
{
	CComBSTR query;
	T_CCI_ERROR error;
	int hReq, res, ind;

	query.Append("select count(*) from ");
	query.Append(tableName);

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

HRESULT CSRStatistics::Execute(LONG* pcRowsAffected,
				ULONG cRestrictions, const VARIANT *rgRestrictions)
{
	ATLTRACE2(atlTraceDBProvider, 2, "CSRStatistics::Execute\n");

	ClearError();

	int hConn = -1;
	HRESULT hr = CCUBRIDSession::GetSessionPtr(this)->GetConnectionHandle(&hConn);
	if(FAILED(hr)) return hr;
	
	char table_name[256]; table_name[0] = 0;
	
	GetRestrictions(cRestrictions, rgRestrictions, table_name);
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

		if (pcRowsAffected)
			*pcRowsAffected = 0;

		while(1)
		{
			CStatisticsRow tprData;
			hr = FetchData(hReq, tprData);
			hr = GetCardinality(hConn, tprData.m_szTableName, &tprData.m_ulCardinality);

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
								tprData.m_szTableName, -1);
						if(res==CSTR_GREATER_THAN) break;
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

DBSTATUS CSRStatistics::GetDBStatus(CSimpleRow *pRow, ATLCOLUMNINFO *pInfo)
{
	ATLTRACE2(atlTraceDBProvider, 3, "CSRStatistics::GetDBStatus\n");
	switch(pInfo->iOrdinal)
	{
	case 3: // TABLE_NAME
	case 4: // CARDINALITY
		return DBSTATUS_S_OK;
	default:
		return DBSTATUS_S_ISNULL;
	}
}
