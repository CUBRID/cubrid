/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

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
