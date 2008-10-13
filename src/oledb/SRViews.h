#pragma once

class CViewsRow
{
public:
	WCHAR m_szTableCatalog[1];
	WCHAR m_szTableSchema[1];
	WCHAR m_szTableName[256];
	WCHAR m_szViewDefinition[5000];
	VARIANT_BOOL m_bCheckOption;
	VARIANT_BOOL m_bIsUpdatable;
	WCHAR m_szDescription[1];
	DATE m_szDateCreated;
	DATE m_szDateModified;
	
	CViewsRow()
	{
		m_szTableCatalog[0] = NULL;
		m_szTableSchema[0] = NULL;
		m_szTableName[0] = NULL;
		m_szViewDefinition[0] = NULL;
		m_bIsUpdatable = ATL_VARIANT_FALSE;
		m_szDescription[0] = NULL;
	}

BEGIN_PROVIDER_COLUMN_MAP(CViewsRow)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_CATALOG", 1, m_szTableCatalog)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_SCHEMA", 2, m_szTableSchema)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_NAME", 3, m_szTableName)
	PROVIDER_COLUMN_ENTRY_WSTR("VIEW_DEFINITION", 4, m_szViewDefinition)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("CHECK_OPTION", 5, DBTYPE_BOOL, 0xFF, 0xFF, m_bCheckOption)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("IS_UPDATABLE", 6, DBTYPE_BOOL, 0xFF, 0xFF, m_bIsUpdatable)
	PROVIDER_COLUMN_ENTRY_WSTR("DESCRIPTION", 7, m_szDescription)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("DATE_CREATED", 8, DBTYPE_DATE, 0xFF, 0xFF, m_szDateCreated)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("DATE_MODIFIED", 9, DBTYPE_DATE, 0xFF, 0xFF, m_szDateModified)
END_PROVIDER_COLUMN_MAP()
};

class CSRViews :
	public CSchemaRowsetImpl<CSRViews, CViewsRow, CCUBRIDSession>
{
public:
	~CSRViews();

	SR_PROPSET_MAP(CSRViews)

	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);
};
