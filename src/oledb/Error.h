#pragma once

// 에러 객체를 지운다
void ClearError();

// 에러 객체에 에러를 추가한다
HRESULT RaiseError(ERRORINFO &info, CComVariant &var, BSTR bstrSQLState=0);
HRESULT RaiseError(HRESULT hrError, DWORD dwMinor, IID iid, CComVariant &var, BSTR bstrSQLState=0);
HRESULT RaiseError(HRESULT hrError, DWORD dwMinor, IID iid, LPCSTR pszText, BSTR bstrSQLState=0);
HRESULT RaiseError(HRESULT hrError, DWORD dwMinor, IID iid, LPWSTR pwszText=0, BSTR bstrSQLState=0);

/*
 * Error Lookup Service
 *    DataSource의 CLSID에 ExtendedErrors 항목에
 *    다음 객체의 UUID를 등록하므로써 동작시킨다.
 */
[
	coclass,
	threading("apartment"),
	version(1.0),
	uuid("3165D76D-CB91-482f-9378-00C216FD5F32"),	
	helpstring("CUBRIDProvider Error Lookup Class"),
	registration_script("..\\..\\src\\oledb\\CUBRIDErrorLookup.rgs")
]
class ATL_NO_VTABLE CCUBRIDErrorLookup :
	public IErrorLookup
{
public:
	STDMETHOD(GetErrorDescription)(HRESULT hrError, DWORD dwLookupID,
						DISPPARAMS *pdispparams, LCID lcid,
						BSTR *pbstrSource, BSTR *pbstrDescription);
	STDMETHOD(GetHelpInfo)(HRESULT hrError, DWORD dwLookupID, LCID lcid,
						BSTR *pbstrHelpFile, DWORD *pdwHelpContext);
	STDMETHOD(ReleaseErrors)(const DWORD dwDynamicErrorID);
};

[
	coclass,
	noncreatable,
	uuid("ED0E5A7D-89F5-4862-BEF3-20E551E1D07B"),
	threading("apartment"),
	registration_script("none")
]
class ATL_NO_VTABLE CCUBRIDErrorInfo :
	public ISQLErrorInfo
{
public:
	CComBSTR m_bstrSQLState;
	LONG m_lNativeError;
	STDMETHOD(GetSQLInfo)(BSTR *pbstrSQLState, LONG *plNativeError);
};
