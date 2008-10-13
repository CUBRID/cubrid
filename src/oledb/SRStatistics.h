#pragma once

class CStatisticsRow
{
public:
	WCHAR m_szTableCatalog[1];
	WCHAR m_szTableSchema[1];
	WCHAR m_szTableName[256];
	ULARGE_INTEGER m_ulCardinality;
	
	CStatisticsRow()
	{
		m_szTableCatalog[0] = NULL;
		m_szTableSchema[0] = NULL;
		m_szTableName[0] = NULL;
		m_ulCardinality.QuadPart = 0;
	}

BEGIN_PROVIDER_COLUMN_MAP(CStatisticsRow)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_CATALOG", 1, m_szTableCatalog)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_SCHEMA", 2, m_szTableSchema)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_NAME", 3, m_szTableName)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("CARDINALITY", 4, DBTYPE_UI8, 20, ~0, m_ulCardinality)
END_PROVIDER_COLUMN_MAP()
};

class CSRStatistics :
	public CSchemaRowsetImpl<CSRStatistics, CStatisticsRow, CCUBRIDSession>
{
public:
	~CSRStatistics();

	SR_PROPSET_MAP(CSRStatistics)

	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);
};