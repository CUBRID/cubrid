#pragma once

class CSRProviderTypes : 
	public CSchemaRowsetImpl< CSRProviderTypes, CPROVIDER_TYPERow, CCUBRIDSession>
{
public:
	SR_PROPSET_MAP(CSRProviderTypes)

	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);
};
