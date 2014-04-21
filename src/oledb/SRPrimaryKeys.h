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

class CPrimaryKeysRow
{
private:
	wchar_t tableCatalog[1];
	wchar_t tableSchema[1];

	wchar_t tableName[128];
	wchar_t columnName[128];

	GUID columnGUID;
	ULONG columnPropID;
	ULONG  columnOrdinal;

	wchar_t primaryKeyName[128];

public:
	CPrimaryKeysRow();

	const wchar_t *GetTableNameColumn(void);
	void SetTableNameColumn(const wchar_t *newTableName);
	
	void SetColumnNameColumn(const wchar_t *columnName);
	
	void SetColumnOrdinalColumn(const int &ordinal);

	void SetPrimaryKeyNameColumn(const wchar_t *primaryKeyName);

	BEGIN_PROVIDER_COLUMN_MAP(CPrimaryKeysRow)
		PROVIDER_COLUMN_ENTRY_WSTR("TABLE_CATALOG", 1, tableCatalog)
		PROVIDER_COLUMN_ENTRY_WSTR("TABLE_SCHEMA", 2, tableSchema)
		PROVIDER_COLUMN_ENTRY_WSTR("TABLE_NAME", 3, tableName)
		PROVIDER_COLUMN_ENTRY_WSTR("COLUMN_NAME", 4, columnName)
		PROVIDER_COLUMN_ENTRY_TYPE_PS("COLUMN_GUID", 5, DBTYPE_GUID, 0xFF, 0xFF, columnGUID)
		PROVIDER_COLUMN_ENTRY_TYPE_PS("COLUMN_PROPID", 6, DBTYPE_UI4, 10, ~0, columnPropID)
		PROVIDER_COLUMN_ENTRY_TYPE_PS("ORDINAL", 7, DBTYPE_UI4, 10, ~0, columnOrdinal)
		PROVIDER_COLUMN_ENTRY_WSTR("PK_NAME", 8, primaryKeyName)
	END_PROVIDER_COLUMN_MAP()
};

class CSRPrimaryKeys : public CSchemaRowsetImpl<CSRPrimaryKeys, CPrimaryKeysRow, CCUBRIDSession>
{
public:
	~CSRPrimaryKeys();

	SR_PROPSET_MAP(CSRPrimaryKeys)

	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);

private:
	void ClearCurrentErrorObject(void);
	HRESULT GetConnectionHandleFromSessionObject(int &connectionHandle);
	HRESULT FetchData(int cci_request_handle, CPrimaryKeysRow &newRow);
};
