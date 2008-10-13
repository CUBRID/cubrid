#pragma once

class CSRColumns : 
	public CSchemaRowsetImpl< CSRColumns, CCOLUMNSRow, CCUBRIDSession>
{
public:
	~CSRColumns();

	SR_PROPSET_MAP(CSRColumns)

	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);
};
