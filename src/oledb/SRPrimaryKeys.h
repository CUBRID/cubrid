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
