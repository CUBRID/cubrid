#include "stdafx.h"
#include "DataSource.h"
#include "Session.h"
#include "Error.h"

CSRColumnPrivileges::~CSRColumnPrivileges()
{
	if(m_spUnkSite)
	{
		CCUBRIDSession* pSession = CCUBRIDSession::GetSessionPtr(this);
		if (pSession->m_cSessionsOpen == 1)	pSession->RowsetCommit();
	}
}

static HRESULT GetCurrentUser(CSRColumnPrivileges *pSR, CComVariant &var)
{
	CCUBRIDDataSource *pDS = CCUBRIDSession::GetSessionPtr(pSR)->GetDataSourcePtr();
	pDS->GetPropValue(&DBPROPSET_DBINIT, DBPROP_AUTH_USERID, &var);
	return S_OK;
}

static void GetRestrictions(ULONG cRestrictions, const VARIANT *rgRestrictions, char *table_name, char* column_name)
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
	{	// COLUMN_NAME restriction
		CW2A name(V_BSTR(&rgRestrictions[3]));
		ATLTRACE2("\tColumn Name = %s\n", (LPSTR)name);

		strncpy(column_name, name, 255);
		table_name[255] = 0; // ensure zero-terminated string
	}
}

// S_OK : 성공
// S_FALSE : 데이터는 가져왔지만 Consumer가 원하는 데이터가 아님
// E_FAIL : 실패
static HRESULT FetchData(int hReq, CColumnPrivilegesRow &cprData)
{
	char *value;
	int ind, res;
	T_CCI_ERROR err_buf;

	res = cci_fetch(hReq, &err_buf);
	if (res==CCI_ER_NO_MORE_DATA) return S_FALSE;
	if(res<0) return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);

	res = cci_get_data(hReq, 1, CCI_A_TYPE_STR, &value, &ind);
	if(res<0) return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));
	wcscpy(cprData.m_szColumnName, CA2W(value));

	res = cci_get_data(hReq, 2, CCI_A_TYPE_STR, &value, &ind);
	if(res<0) return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));
	// CCI는 "ALTER", "EXECUTE"등도 반환하는데 어떻게 처리해야 할지 모르겠다.
	if(strcmp(value, "SELECT")!=0 && strcmp(value, "DELETE")!=0 && strcmp(value, "INSERT")!=0
	   && strcmp(value, "UPDATE")!=0 && strcmp(value, "REFERENCES")!=0)
		return S_FALSE;
	wcscpy(cprData.m_szPrivilegeType, CA2W(value));

	res = cci_get_data(hReq, 3, CCI_A_TYPE_STR, &value, &ind);
	if(res<0) return RaiseError(E_FAIL, 0, __uuidof(IDBSchemaRowset));
	if(strcmp(value, "NO")==0)
		cprData.m_bIsGrantable = VARIANT_FALSE;
	else
		cprData.m_bIsGrantable = VARIANT_TRUE;

	return S_OK;
}

// S_OK : 성공
HRESULT CSRColumnPrivileges::FillRowData(int hReq, LONG *pcRowsAffected,
			const CComVariant &grantee, const char *table_name, const char *column_name)
{
	int res;
	T_CCI_ERROR err_buf;
	HRESULT hr = S_OK;

	res = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &err_buf);
    if(res==CCI_ER_NO_MORE_DATA) return S_OK;
	else if(res<0) return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);

	while(1)
	{
		CColumnPrivilegesRow cprData;
		wcscpy(cprData.m_szGrantor, L"DBA");
		wcscpy(cprData.m_szGrantee, V_BSTR(&grantee));
		wcscpy(cprData.m_szTableName, CA2W(table_name));

		hr = FetchData(hReq, cprData);
		if(FAILED(hr)) return E_FAIL;

		if(hr==S_OK) // S_FALSE면 추가하지 않음
		{
			// TABLE_NAME, COLUMN_NAME, PRIVILEGE_TYPE 순으로 정렬해야 한다.
			size_t nPos;
			for( nPos=0 ; nPos<m_rgRowData.GetCount() ; nPos++ )
			{
				int res = CompareStringW(LOCALE_USER_DEFAULT, 
						NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH | SORT_STRINGSORT,
						m_rgRowData[nPos].m_szTableName, -1,
						cprData.m_szTableName, -1);
				if(res==CSTR_GREATER_THAN) break;
				if(res==CSTR_EQUAL)
				{
					res = CompareStringW(LOCALE_USER_DEFAULT, 
							NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH | SORT_STRINGSORT,
							m_rgRowData[nPos].m_szColumnName, -1,
							cprData.m_szColumnName, -1);
					if(res==CSTR_GREATER_THAN) break;
					if(res==CSTR_EQUAL)
					{
						res = CompareStringW(LOCALE_USER_DEFAULT, 
								NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH | SORT_STRINGSORT,
								m_rgRowData[nPos].m_szPrivilegeType, -1,
								cprData.m_szPrivilegeType, -1);
						if(res==CSTR_GREATER_THAN) break;
					}
				}
			}
		
			_ATLTRY
			{
				m_rgRowData.InsertAt(nPos, cprData);
			}
			_ATLCATCHALL()
			{
				return E_OUTOFMEMORY;
			}
			(*pcRowsAffected)++;
		}

		res = cci_cursor(hReq, 1, CCI_CURSOR_CURRENT, &err_buf);
		if(res==CCI_ER_NO_MORE_DATA) return S_OK;
		else if(res<0) return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);
	}

	return S_OK;
}

HRESULT CSRColumnPrivileges::Execute(LONG* pcRowsAffected,
				ULONG cRestrictions, const VARIANT *rgRestrictions)
{
	ATLTRACE2(atlTraceDBProvider, 2, "CSRColumnPrivileges::Execute\n");

	ClearError();

	int hConn;
	HRESULT hr = CCUBRIDSession::GetSessionPtr(this)->GetConnectionHandle(&hConn);
	if(FAILED(hr)) return hr;

	if (pcRowsAffected)
		*pcRowsAffected = 0;

	char table_name[256]; table_name[0] = 0;
	char column_name[256]; column_name[0] = 0;

	GetRestrictions(cRestrictions, rgRestrictions, table_name, column_name);

	CComVariant grantee;
	hr = GetCurrentUser(this, grantee);
	if(FAILED(hr)) return hr;

	CAtlArray<CStringA> rgTableNames;
	if(table_name[0])
		rgTableNames.Add(table_name);
	else
		Util::GetTableNames(hConn, rgTableNames);

	for(size_t i=0;i<rgTableNames.GetCount();i++)
	{
		T_CCI_ERROR err_buf;
		int hReq;
		hReq = cci_schema_info(hConn, CCI_SCH_ATTR_PRIVILEGE, rgTableNames[i].GetBuffer(),
								(column_name[0]?column_name:NULL),
								CCI_ATTR_NAME_PATTERN_MATCH, &err_buf);
		
		if(hReq<0)
		{
			ATLTRACE2("CSRColumnPrivileges: cci_schema_info fail\n");
			return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), err_buf.err_msg);
		}

		hr = FillRowData(hReq, pcRowsAffected, grantee, rgTableNames[i].GetBuffer(), column_name);
		if(FAILED(hr))
		{
			cci_close_req_handle(hReq);
			return E_FAIL;
		}

		cci_close_req_handle(hReq);
	}

	return S_OK;
}

DBSTATUS CSRColumnPrivileges::GetDBStatus(CSimpleRow *pRow, ATLCOLUMNINFO *pInfo)
{
	ATLTRACE2(atlTraceDBProvider, 3, "CSRColumnPrivileges::GetDBStatus\n");
	switch(pInfo->iOrdinal)
	{
	case 1: // GRANTOR
	case 2: // GRANTEE
	case 5: // TABLE_NAME
	case 6: // COLUMN_NAME
	case 9: // PRIVILEGE_TYPE
	case 10: // IS_GRANTABLE
		return DBSTATUS_S_OK;
	default:
		return DBSTATUS_S_ISNULL;
	}
}
