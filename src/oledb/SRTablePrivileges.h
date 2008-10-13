#pragma once

class CTablePrivilegesRow
{
public:
	WCHAR m_szGrantor[129];
	WCHAR m_szGrantee[129];
	WCHAR m_szTableCatalog[129];
	WCHAR m_szTableSchema[129];
	WCHAR m_szTableName[129];
	WCHAR m_szPrivilegeType[129];
	VARIANT_BOOL m_bIsGrantable;

	CTablePrivilegesRow()
	{
		m_szGrantor[0] = NULL;
		m_szGrantee[0] = NULL;
		m_szTableCatalog[0] = NULL;
		m_szTableSchema[0] = NULL;
		m_szTableName[0] = NULL;
		m_szPrivilegeType[0] = NULL;
	}

BEGIN_PROVIDER_COLUMN_MAP(CTablePrivilegesRow)
	PROVIDER_COLUMN_ENTRY_WSTR("GRANTOR", 1, m_szGrantor)
	PROVIDER_COLUMN_ENTRY_WSTR("GRANTEE", 2, m_szGrantee)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_CATALOG", 3, m_szTableCatalog)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_SCHEMA", 4, m_szTableSchema)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_NAME", 5, m_szTableName)
	PROVIDER_COLUMN_ENTRY_WSTR("PRIVILEGE_TYPE", 6, m_szPrivilegeType)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("IS_GRANTABLE", 7, DBTYPE_BOOL, 0xFF, 0xFF, m_bIsGrantable)
END_PROVIDER_COLUMN_MAP()
};

class CSRTablePrivileges :
	public CSchemaRowsetImpl<CSRTablePrivileges, CTablePrivilegesRow, CCUBRIDSession>
{
public:
	SR_PROPSET_MAP(CSRTablePrivileges)

	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);
};
