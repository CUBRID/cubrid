#pragma once

class CViewColumnUsageRow
{
public:
	WCHAR m_szViewCatalog[1];
	WCHAR m_szViewSchema[1];
	WCHAR m_szViewName[256];
	WCHAR m_szTableCatalog[1];
	WCHAR m_szTableSchema[1];
	WCHAR m_szTableName[256];
	WCHAR m_szColumnName[256];
	GUID m_ColumnGUID;
	UINT m_ColumnPropID;
	
	CViewColumnUsageRow()
	{
		m_szViewCatalog[0] = NULL;
		m_szViewSchema[0] = NULL;
		m_szViewName[0] = NULL;
		m_szTableCatalog[0] = NULL;
		m_szTableSchema[0] = NULL;
		m_szTableName[0] = NULL;
		m_szColumnName[0] = NULL;
	}

BEGIN_PROVIDER_COLUMN_MAP(CViewColumnUsageRow)
	PROVIDER_COLUMN_ENTRY_WSTR("VIEW_CATALOG", 1, m_szViewCatalog)
	PROVIDER_COLUMN_ENTRY_WSTR("VIEW_SCHEMA", 2, m_szViewSchema)
	PROVIDER_COLUMN_ENTRY_WSTR("VIEW_NAME", 3, m_szViewName)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_CATALOG", 4, m_szTableCatalog)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_SCHEMA", 5, m_szTableSchema)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_NAME", 6, m_szTableName)
	PROVIDER_COLUMN_ENTRY_WSTR("COLUMN_NAME", 7, m_szColumnName)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("COLUMN_GUID", 8, DBTYPE_GUID, 0xFF, 0xFF, m_ColumnGUID)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("COLUMN_PROPID", 9, DBTYPE_UI4, 10, 0xFF, m_ColumnPropID)
END_PROVIDER_COLUMN_MAP()
};

class CSRViewColumnUsage :
	public CSchemaRowsetImpl<CSRViewColumnUsage, CViewColumnUsageRow, CCUBRIDSession>
{
public:
	~CSRViewColumnUsage();

	SR_PROPSET_MAP(CSRViewColumnUsage)

	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);
};