#pragma once

#include "row.h"

class CCUBRIDClassRows
{
public:
	wchar_t* m_strData;

	CCUBRIDClassRows()
	{
		m_strData = new WCHAR[100];
	}

BEGIN_PROVIDER_COLUMN_MAP(CCUBRIDClassRows)
	PROVIDER_COLUMN_ENTRY_WSTR("SET", 1, m_strData)
END_PROVIDER_COLUMN_MAP()
};

class CCUBRIDRowRowset :
	public CSchemaRowsetImpl< CCUBRIDRowRowset, CCUBRIDClassRows, CCUBRIDRow>
{
public:
	int		m_hReq;
	char	m_szOID[15];
	char	m_colName[256];
	int		m_colIndex;

	CCUBRIDRowRowset(void) : m_hReq(0), m_colIndex(0)
	{
		m_szOID[0] = NULL;
		m_colName[0] = NULL;
	}

	~CCUBRIDRowRowset(void)
	{
		cci_close_req_handle(m_hReq);
	}

BEGIN_PROPSET_MAP(CCUBRIDRowRowset)
	BEGIN_PROPERTY_SET(DBPROPSET_ROWSET)
		PROPERTY_INFO_ENTRY(IAccessor)
		PROPERTY_INFO_ENTRY(IColumnsInfo)
		PROPERTY_INFO_ENTRY(IConvertType)
		PROPERTY_INFO_ENTRY(IRowset)
		PROPERTY_INFO_ENTRY(IRowsetIdentity)
		PROPERTY_INFO_ENTRY(IRowsetInfo)
		PROPERTY_INFO_ENTRY(CANFETCHBACKWARDS)
		PROPERTY_INFO_ENTRY(CANHOLDROWS)
		PROPERTY_INFO_ENTRY(CANSCROLLBACKWARDS)
		PROPERTY_INFO_ENTRY_VALUE(MAXOPENROWS, 0)
		PROPERTY_INFO_ENTRY_VALUE(MAXROWS, 0)
	END_PROPERTY_SET(DBPROPSET_ROWSET)
END_PROPSET_MAP()

	HRESULT GetConnectionHandle(int *hConn);
	HRESULT FetchData(T_CCI_SET** set);
	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);
};
