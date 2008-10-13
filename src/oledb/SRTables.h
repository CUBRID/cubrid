#pragma once

class CSRTables : 
	public CSchemaRowsetImpl< CSRTables, CTABLESRow, CCUBRIDSession>
{
public:
	~CSRTables();

	SR_PROPSET_MAP(CSRTables)

	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);
};
