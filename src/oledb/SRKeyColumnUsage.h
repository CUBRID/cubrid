#pragma	   once

class CKeyColumnUsageRow
{
public:
	wchar_t constraintCatalog[127];
	wchar_t constraintSchema[127];
	wchar_t constraintName[127];
	wchar_t tableCatalog[127];
	wchar_t tableSchema[127];
	wchar_t tableName[127];
	wchar_t columnName[127];
	GUID columnGUID;
	ULONG columnPropID;
	ULONG ordinalPosition;

public:
	CKeyColumnUsageRow();

//	static void GetRestrictions(ULONG cRestrictions, const VARIANT *rgRestrictions, char *constraintName, char *tableName, char *columnName);

	void SetTableName(const wchar_t *tableName);
	void SetColumnName(const wchar_t *columnName);
	void SetOrdinalPosition(const int &ordinal);
	void SetConstraintName(const wchar_t *constraintName);

	const wchar_t *GetTableName(void);
	const wchar_t *GetColumnName(void);
	const int GetOrdinalPosition(void);
	const wchar_t *GetConstraintName(void);

	BEGIN_PROVIDER_COLUMN_MAP(CKeyColumnUsageRow)
		PROVIDER_COLUMN_ENTRY_WSTR("CONSTRAINT_CATALOG", 1, constraintCatalog)
		PROVIDER_COLUMN_ENTRY_WSTR("CONSTRAINT_SCHEMA", 2, constraintSchema)
		PROVIDER_COLUMN_ENTRY_WSTR("CONSTRAINT_NAME", 3, constraintName)
		PROVIDER_COLUMN_ENTRY_WSTR("TABLE_CATALOG", 4, tableCatalog)
		PROVIDER_COLUMN_ENTRY_WSTR("TABLE_SCHEMA", 5, tableSchema)
		PROVIDER_COLUMN_ENTRY_WSTR("TABLE_NAME", 6, tableName)
		PROVIDER_COLUMN_ENTRY_WSTR("COLUMN_NAME", 7, columnName)
		PROVIDER_COLUMN_ENTRY_TYPE_PS("COLUMN_GUID", 8, DBTYPE_GUID, 0xFF, 0xFF, columnGUID)
		PROVIDER_COLUMN_ENTRY_TYPE_PS("COLUMN_PROPID", 9, DBTYPE_UI4, 10, ~0, columnPropID)
		PROVIDER_COLUMN_ENTRY_TYPE_PS("ORDINAL_POSITION", 10, DBTYPE_UI4, 10, ~0, ordinalPosition)
	END_PROVIDER_COLUMN_MAP()
};

class CSRKeyColumnUsage : public CSchemaRowsetImpl<CSRKeyColumnUsage, CKeyColumnUsageRow, CCUBRIDSession>
{
public:
	~CSRKeyColumnUsage();

	SR_PROPSET_MAP(CSRKeyColumnUsage)

	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);

private:
	void ClearCurrentErrorObject(void);
	HRESULT GetConnectionHandleFromSessionObject(int &connectionHandle);

	HRESULT FetchData(int cci_request_handle, CKeyColumnUsageRow &newRow);
};