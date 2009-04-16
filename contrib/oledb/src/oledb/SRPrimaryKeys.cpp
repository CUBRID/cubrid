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

static void GetRestrictions(ULONG cRestrictions, const VARIANT *rgRestrictions, char *table_name)
{
	if ( cRestrictions >= 3 && V_VT(&rgRestrictions[2]) == VT_BSTR && V_BSTR(&rgRestrictions[2]) != NULL)
	{
		CW2A name(V_BSTR(&rgRestrictions[2]));

		ATLTRACE2("\tTable Name = %s\n", (LPSTR)name);

		strncpy(table_name, name, 127);
		table_name[127] = '\0'; // ensure zero-terminated string
	}
}

CPrimaryKeysRow::CPrimaryKeysRow()
{
	columnOrdinal = 0;
	columnPropID = 0;
	columnName[0] = '\0';
	tableName[0] = '\0';
	tableCatalog[0] = '\0';
	tableSchema[0] = '\0';
	this->primaryKeyName[0] = '\0';
}

const wchar_t *CPrimaryKeysRow::GetTableNameColumn(void)
{
	return const_cast<wchar_t *>(this->tableName);
}

void CPrimaryKeysRow::SetTableNameColumn(const wchar_t *newTableName)
{
	wcscpy(this->tableName, newTableName);
	_wcsupr(this->tableName);
}

void CPrimaryKeysRow::SetColumnNameColumn(const wchar_t *columnName)
{
	wcscpy(this->columnName, columnName);
	_wcsupr(this->columnName);
}

void CPrimaryKeysRow::SetColumnOrdinalColumn(const int &ordinal)
{
	this->columnOrdinal = ordinal;
}

void CPrimaryKeysRow::SetPrimaryKeyNameColumn(const wchar_t *primaryKeyName)
{
	wcscpy(this->primaryKeyName, primaryKeyName);
}

CSRPrimaryKeys::~CSRPrimaryKeys()
{
	if (m_spUnkSite)
	{
		CCUBRIDSession* pSession = CCUBRIDSession::GetSessionPtr(this);
		if (pSession->m_cSessionsOpen == 1)	
			pSession->RowsetCommit();
	}
}

HRESULT CSRPrimaryKeys::Execute(LONG *pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions)
{
	ATLTRACE2(atlTraceDBProvider, 2, "CSRPrimaryKeys::Execute()\n");
	
	////
	int connectionHandle = -999;
	int cci_return_code = 0;
	int cci_request_handle = 0;

	char tableName[128] = { '\0',};

	HRESULT hr = E_FAIL;

	T_CCI_ERROR cci_error_buffer;
	////

	ClearCurrentErrorObject();
	
	hr = GetConnectionHandleFromSessionObject(connectionHandle);
	//hr = CCUBRIDSession::GetSessionPtr(this)->GetConnectionHandle(&connectionHandle);
	if (FAILED(hr))
		return hr;
	GetRestrictions(cRestrictions, rgRestrictions, tableName);
#if 1
	cci_return_code = cci_schema_info
		(
		connectionHandle, 
		CCI_SCH_PRIMARY_KEY, 
		(tableName[0]? tableName:NULL), 
		NULL,							// no attribute name is specified.
		CCI_CLASS_NAME_PATTERN_MATCH, 
		&cci_error_buffer
		);
#endif
	if (cci_return_code < 0)
	{
		ATLTRACE2(atlTraceDBProvider, 2, "CSRPrimaryKeys::cci_schema_info() FAILED! \n");
		return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), cci_error_buffer.err_msg);
	}

	cci_request_handle = cci_return_code;

	cci_return_code = cci_cursor(cci_request_handle, 1, CCI_CURSOR_FIRST, &cci_error_buffer);
	if (cci_return_code < 0)
	{
		if (cci_return_code == CCI_ER_NO_MORE_DATA)
		{
			// it means that there is no primary key. 
			cci_close_req_handle(cci_request_handle);
			return S_OK;
		}
		else
		{
			cci_close_req_handle(cci_request_handle);
			return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), cci_error_buffer.err_msg);
		}
	}
	
	while (1)
	{
		//CPrimaryKeysRow *newRow = static_cast<CPrimaryKeysRow *>(CoTaskMemAlloc(sizeof(CPrimaryKeysRow)));
		//if ( newRow == NULL )
		//	return E_OUTOFMEMORY;
		CPrimaryKeysRow newRow;

		hr = FetchData(cci_request_handle, newRow);
		if (FAILED(hr))
			return hr;

		if (hr == S_OK)
		{
			_ATLTRY
			{
				size_t position = 0;
				for (position = 0; position < m_rgRowData.GetCount() ; position++)
				{
					int result = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH | SORT_STRINGSORT, m_rgRowData[position].GetTableNameColumn(), -1, newRow.GetTableNameColumn(), -1);
					if ( result == CSTR_GREATER_THAN ) 
						break;
				}
				m_rgRowData.InsertAt(position, newRow);
			}
			_ATLCATCHALL()
			{
				cci_close_req_handle(cci_request_handle);
				return E_OUTOFMEMORY;
			}
		}

		cci_return_code = cci_cursor(cci_request_handle, 1, CCI_CURSOR_CURRENT, &cci_error_buffer);
		if (cci_return_code < 0)
		{
			if (cci_return_code == CCI_ER_NO_MORE_DATA)
			{
				//CoTaskMemFree(newRow);
				cci_close_req_handle(cci_request_handle);
				return S_OK;
			}
			else
			{
				//CoTaskMemFree(newRow);
				cci_close_req_handle(cci_request_handle);
				return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), cci_error_buffer.err_msg);
			}
		}
	}
	return S_OK;
}

DBSTATUS CSRPrimaryKeys::GetDBStatus(CSimpleRow *pRow, ATLCOLUMNINFO *pInfo)
{
	ATLTRACE2(atlTraceDBProvider, 3, "CSRPrimaryKeys::GetDBStatus()\n");

	switch(pInfo->iOrdinal) 
	{
	case 1:
	case 2:
	case 5:
	case 6:
		return DBSTATUS_S_ISNULL;
	case 3:
	case 4:
	case 7:
	case 8:
		return DBSTATUS_S_OK;
	default:
		return DBSTATUS_E_UNAVAILABLE;
	}
}

void CSRPrimaryKeys::ClearCurrentErrorObject(void)
{
	ClearError();
}

HRESULT CSRPrimaryKeys::GetConnectionHandleFromSessionObject(int &connectionHandle)
{
	return CCUBRIDSession::GetSessionPtr(this)->GetConnectionHandle(&connectionHandle);
}

HRESULT CSRPrimaryKeys::FetchData(int cci_request_handle, CPrimaryKeysRow &newRow)
{
	////
	int ordinal = 0;

	int cci_return_code = -999;
	int cci_indicator = -999;
	char *value = NULL;

	T_CCI_ERROR cci_error_buffer;
	////

	cci_return_code = cci_fetch(cci_request_handle, &cci_error_buffer);
	if ( cci_return_code < 0 )
	{
		cci_close_req_handle(cci_request_handle);
		return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), cci_error_buffer.err_msg);
	}

	cci_return_code = cci_get_data(cci_request_handle, 1, CCI_A_TYPE_STR, &value, &cci_indicator);
	if (cci_return_code < 0)
	{
		cci_close_req_handle(cci_request_handle);
		return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), cci_error_buffer.err_msg);
	}

	newRow.SetTableNameColumn(CA2W(value));

	cci_return_code = cci_get_data(cci_request_handle, 2, CCI_A_TYPE_STR, &value, &cci_indicator);
	if (cci_return_code < 0)
	{
		cci_close_req_handle(cci_request_handle);
		return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), cci_error_buffer.err_msg);
	}

	newRow.SetColumnNameColumn(CA2W(value));	

	cci_return_code = cci_get_data(cci_request_handle, 3, CCI_A_TYPE_INT, &ordinal, &cci_indicator);
	if (cci_return_code < 0)
	{
		cci_close_req_handle(cci_request_handle);
		return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), cci_error_buffer.err_msg);
	}
	newRow.SetColumnOrdinalColumn(ordinal);

	cci_return_code = cci_get_data(cci_request_handle, 4, CCI_A_TYPE_STR, &value, &cci_indicator);
	if (cci_return_code < 0)
	{
		cci_close_req_handle(cci_request_handle);
		return RaiseError(E_FAIL, 1, __uuidof(IDBSchemaRowset), cci_error_buffer.err_msg);
	}
	newRow.SetPrimaryKeyNameColumn(CA2W(value));
	return S_OK;
}