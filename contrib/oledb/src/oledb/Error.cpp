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

#include "stdafx.h"
#include "Error.h"
#include "DataSource.h"

/*
 * ISQLErrorInfo는 custom error object
 * (IErrorRecords::AddErrorRecord의 4번째 인자)
 * 를 통해 지원한다.
 *
 * DISPPARAMS를 사용하면 좀 더 자세한 메시지를 만들 때 사용할 수 있다.
 * 예를 들어 dwMinor에 1을 주고, pszText에 'abcd'를 주었다고 하자.
 * IErrorLookup::GetErrorDescription 구현시 인자의 dwLookupID가 1이면
 * DISPPARAMS를 읽어서('abcd'가 들어있다) 'abcd를 열수 없음'과 같은
 * 메시지를 생성해 consumer로 전달하게 할 수 있다.
 *
 * IErrorLookup::GetErrorDescription의 lcid는 로케일을 가리킨다.
 * 예를 들어 미국 영어는 0x0409, 한국어는 0x0412이다.
 * 이를 통해 원하는 언어로 에러 메시지를 생성할 수 있다.
 */
/*
 * TODO: 현재 iid를 RaiseError()시 설정하고 있다.
 *  여러 인터페이스에서 공통으로 쓰는 함수가 있으면 제대로 된 값을 설정할 수 없다.
 *  ClearError()시 iid를 받아서 저장해 두었다가
 *  RaiseError()시 사용하는 방법을 생각할 수 있다.
 *  다만 Thread등의 문제가 없는지 생각해 봐야 한다.
 */

void ClearError()
{
	SetErrorInfo(0, NULL);
}

HRESULT RaiseError(ERRORINFO &info, CComVariant &var, BSTR bstrSQLState)
{
	CComPtr<IErrorInfo> spErrorInfo;
	GetErrorInfo(0, &spErrorInfo);

	// 에러 객체가 없으면 생성한다.
	// 에러 객체는 MDAC SDK가 제공한다.
	if(spErrorInfo==NULL)
		spErrorInfo.CoCreateInstance(CLSID_EXTENDEDERRORINFO,
									NULL, CLSCTX_INPROC_SERVER);

	// 추가할 에러
	DISPPARAMS dispparams = { NULL, NULL, 0, 0 };
	if(V_VT(&var)!=VT_EMPTY)
	{
		dispparams.rgvarg = &var;
		dispparams.cArgs = 1;
	}

	CComPtr<IUnknown> spSQLInfo;
	if(bstrSQLState)
	{
		CComPolyObject<CCUBRIDErrorInfo> *pObj;
		CComPolyObject<CCUBRIDErrorInfo>::CreateInstance(NULL, &pObj);

		CCUBRIDErrorInfo *pSQLInfo = &(pObj->m_contained);
		pSQLInfo->m_bstrSQLState = bstrSQLState;
		pSQLInfo->m_lNativeError = info.dwMinor;

		pSQLInfo->QueryInterface(__uuidof(IUnknown), (void **)&spSQLInfo);
	}

	// IErrorRecords의 포인터를 구해서 에러를 추가한다.
	CComPtr<IErrorRecords> spErrorRecords;
	spErrorInfo->QueryInterface(__uuidof(IErrorRecords), (void **)&spErrorRecords);
	spErrorRecords->AddErrorRecord(&info, info.dwMinor, &dispparams, spSQLInfo, 0);

	// 에러 객체 등록
	SetErrorInfo(0, spErrorInfo);

	return info.hrError;
}

HRESULT RaiseError(HRESULT hrError, DWORD dwMinor, IID iid, CComVariant &var, BSTR bstrSQLState)
{
	ERRORINFO info;
	info.hrError = hrError;
	info.dwMinor = dwMinor;
	info.clsid = CCUBRIDDataSource::GetObjectCLSID();
	info.iid = iid;
	info.dispid = 0;
	return RaiseError(info, var, bstrSQLState);
}

HRESULT RaiseError(HRESULT hrError, DWORD dwMinor, IID iid, LPCSTR pszText, BSTR bstrSQLState)
{
	CComVariant var;
	if(pszText) var = pszText;
	return RaiseError(hrError, dwMinor, iid, var, bstrSQLState);
}

HRESULT RaiseError(HRESULT hrError, DWORD dwMinor, IID iid, LPWSTR pwszText, BSTR bstrSQLState)
{
	CComVariant var;
	if(pwszText) var = pwszText;
	return RaiseError(hrError, dwMinor, iid, var, bstrSQLState);
}

static LPOLESTR GetErrorDescriptionString(HRESULT hr)
{
	// 다음은 conformance tests의 cexterr.cpp에서 가져왔다.
	static struct
	{
		HRESULT hr;
		LPOLESTR str;
	} desc[] = {
		{ E_UNEXPECTED, OLESTR("Catastrophic failure") },
		{ E_FAIL, OLESTR("A provider-specific error occurred") },
		{ E_NOINTERFACE, OLESTR("No such interface supported") },
		{ E_INVALIDARG, OLESTR("The parameter is incorrect.") },
		{ DB_E_BADACCESSORHANDLE, OLESTR("Accessor is invalid.") },
		{ DB_E_ROWLIMITEXCEEDED, OLESTR("Row could not be inserted into the rowset without exceeding provider's maximum number of active rows.") },
		{ DB_E_READONLYACCESSOR, OLESTR("Accessor is read-only. Operation failed.") },
		{ DB_E_SCHEMAVIOLATION, OLESTR("Given values violate the database schema.") },
		{ DB_E_BADROWHANDLE, OLESTR("Row handle is invalid.") },
		{ DB_E_OBJECTOPEN, OLESTR("Object was open.") },
		{ DB_E_BADCHAPTER, OLESTR("Chapter is invalid.") },
		{ DB_E_CANTCONVERTVALUE, OLESTR("Data or literal value could not be converted to the type of the column in the data source, and the provider was unable to determine which columns could not be converted.  Data overflow or sign mismatch was not the cause.") },
		{ DB_E_BADBINDINFO, OLESTR("Binding information is invalid.") },
		{ DB_SEC_E_PERMISSIONDENIED, OLESTR("Permission denied.") },
		{ DB_E_NOTAREFERENCECOLUMN, OLESTR("Specified column does not contain bookmarks or chapters.") },
		{ DB_E_LIMITREJECTED, OLESTR("Cost limits were rejected.") },
		{ DB_E_NOCOMMAND, OLESTR("Command text was not set for the command object.") },
		{ DB_E_COSTLIMIT, OLESTR("Query plan within the cost limit cannot be found.") },
		{ DB_E_BADBOOKMARK, OLESTR("Bookmark is invalid.") },
		{ DB_E_BADLOCKMODE, OLESTR("Lock mode is invalid.") },
		{ DB_E_PARAMNOTOPTIONAL, OLESTR("No value given for one or more required parameters.") },
		{ DB_E_BADCOLUMNID, OLESTR("Column ID is invalid.") },
		{ DB_E_BADRATIO, OLESTR("Numerator was greater than denominator. Values must express ratio between zero and 1.") },
		{ DB_E_BADVALUES, OLESTR("Value is invalid.") },
		{ DB_E_ERRORSINCOMMAND, OLESTR("One or more errors occurred during processing of command.") },
		{ DB_E_CANTCANCEL, OLESTR("Command cannot be canceled.") },
		{ DB_E_DIALECTNOTSUPPORTED, OLESTR("Command dialect is not supported by this provider.") },
		{ DB_E_DUPLICATEDATASOURCE, OLESTR("Data source object could not be created because the named data source already exists.") },
		{ DB_E_CANNOTRESTART, OLESTR("The rowset was built over a live data feed and cannot be restarted.") },
		{ DB_E_NOTFOUND, OLESTR("No key matching the described characteristics could be found within the current range.") },
		{ DB_E_NEWLYINSERTED, OLESTR("Identity cannot be determined for newly inserted rows.") },
		{ DB_E_CANNOTFREE, OLESTR("Provider has ownership of this tree.") },
		{ DB_E_GOALREJECTED, OLESTR("Goal was rejected because no nonzero weights were specified for any goals supported. Current goal was not changed.") },
		{ DB_E_UNSUPPORTEDCONVERSION, OLESTR("Requested conversion is not supported.") },
		{ DB_E_BADSTARTPOSITION, OLESTR("Goal was rejected because no nonzero weights were specified for any goals supported. Current goal was not changed.") },
		{ DB_E_NOQUERY, OLESTR("Information was requested for a query, and the query was not set.") },
		{ DB_E_NOTREENTRANT, OLESTR("Consumer's event handler called a non-reentrant method in the provider.") },
		{ DB_E_ERRORSOCCURRED, OLESTR("Multiple-step OLE DB operation generated errors. Check each OLE DB status value, if available. No work was done.") },
		{ DB_E_NOAGGREGATION, OLESTR("Non-NULL controlling IUnknown was specified, and either the requested interface was not IUnknown, or the provider does not support COM aggregation.") },
		{ DB_E_DELETEDROW, OLESTR("Row handle referred to a deleted row or a row marked for deletion.") },
		{ DB_E_CANTFETCHBACKWARDS, OLESTR("The rowset does not support fetching backwards.") },
		{ DB_E_ROWSNOTRELEASED, OLESTR("Row handles must all be released before new ones can be obtained.") },
		{ DB_E_BADSTORAGEFLAG, OLESTR("One or more storage flags are not supported.") },
		{ DB_E_BADCOMPAREOP, OLESTR("Comparison operator is invalid.") },
		{ DB_E_BADSTATUSVALUE, OLESTR("The specified status flag was neither DBCOLUMNSTATUS_OK nor DBCOLUMNSTATUS_ISNULL.") },
		{ DB_E_CANTSCROLLBACKWARDS, OLESTR("Rowset does not support scrolling backward.") },
		{ DB_E_BADREGIONHANDLE, OLESTR("Region handle is invalid.") },
		{ DB_E_NONCONTIGUOUSRANGE, OLESTR("Set of rows is not contiguous to, or does not overlap, the rows in the watch region.") },
		{ DB_E_INVALIDTRANSITION, OLESTR("A transition from ALL* to MOVE* or EXTEND* was specified.") },
		{ DB_E_NOTASUBREGION, OLESTR("The specified region is not a proper subregion of the region identified by the given watch region handle.") },
		{ DB_E_MULTIPLESTATEMENTS, OLESTR("Multiple-statement commands are not supported by this provider.") },
		{ DB_E_INTEGRITYVIOLATION, OLESTR("A specified value violated the integrity constraints for a column or table.") },
		{ DB_E_BADTYPENAME, OLESTR("Type name is invalid.") },
		{ DB_E_ABORTLIMITREACHED, OLESTR("Execution stopped because a resource limit was reached. No results were returned.") },
		{ DB_E_ROWSETINCOMMAND, OLESTR("Command object whose command tree contains a rowset or rowsets cannot be cloned.") },
		{ DB_E_CANTTRANSLATE, OLESTR("Current tree cannot be represented as text.") },
		{ DB_E_DUPLICATEINDEXID, OLESTR("The specified index already exists.") },
		{ DB_E_NOINDEX, OLESTR("The specified index does not exist.") },
		{ DB_E_INDEXINUSE, OLESTR("Index is in use.") },
		{ DB_E_NOTABLE, OLESTR("The specified table does not exist.") },
		{ DB_E_CONCURRENCYVIOLATION, OLESTR("The rowset was using optimistic concurrency and the value of a column has been changed since it was last read.") },
		{ DB_E_BADCOPY, OLESTR("Errors were detected during the copy.") },
		{ DB_E_BADPRECISION, OLESTR("Precision is invalid.") },
		{ DB_E_BADSCALE, OLESTR("Scale is invalid.") },
		{ DB_E_BADTABLEID, OLESTR("Table ID is invalid.") },
		{ DB_E_BADTYPE, OLESTR("Type is invalid.") },
		{ DB_E_DUPLICATECOLUMNID, OLESTR("Column ID already exists or occurred more than once in the array of columns.") },
		{ DB_E_DUPLICATETABLEID, OLESTR("The specified table already exists.") },
		{ DB_E_TABLEINUSE, OLESTR("Table is in use.") },
		{ DB_E_NOLOCALE, OLESTR("The specified locale ID was not supported.") },
		{ DB_E_BADRECORDNUM, OLESTR("Record number is invalid.") },
		{ DB_E_BOOKMARKSKIPPED, OLESTR("Although the bookmark was validly formed, no row could be found to match it.") },
		{ DB_E_BADPROPERTYVALUE, OLESTR("Property value is invalid.") },
		{ DB_E_INVALID, OLESTR("The rowset was not chaptered.") },
		{ DB_E_BADACCESSORFLAGS, OLESTR("One or more accessor flags were invalid.") },
		{ DB_E_BADSTORAGEFLAGS, OLESTR("One or more storage flags are invalid.") },
		{ DB_E_BYREFACCESSORNOTSUPPORTED, OLESTR("Reference accessors are not supported by this provider.") },
		{ DB_E_NULLACCESSORNOTSUPPORTED, OLESTR("Null accessors are not supported by this provider.") },
		{ DB_E_NOTPREPARED, OLESTR("The command was not prepared.") },
		{ DB_E_BADACCESSORTYPE, OLESTR("The specified accessor was not a parameter accessor.") },
		{ DB_E_WRITEONLYACCESSOR, OLESTR("The given accessor was write-only.") },
		{ DB_SEC_E_AUTH_FAILED, OLESTR("Authentication failed.") },
		{ DB_E_CANCELED, OLESTR("Operation was canceled.") },
		{ DB_E_CHAPTERNOTRELEASED, OLESTR("The rowset was single-chaptered and the chapter was not released.") },
		{ DB_E_BADSOURCEHANDLE, OLESTR("Source handle is invalid.") },
		{ DB_E_PARAMUNAVAILABLE, OLESTR("Provider cannot derive parameter information and SetParameterInfo has not been called.") },
		{ DB_E_ALREADYINITIALIZED, OLESTR("The data source object is already initialized.") },
		{ DB_E_NOTSUPPORTED, OLESTR("Method is not supported by this provider.") },
		{ DB_E_MAXPENDCHANGESEXCEEDED, OLESTR("Number of rows with pending changes exceeded the limit.") },
		{ DB_E_BADORDINAL, OLESTR("The specified column did not exist.") },
		{ DB_E_PENDINGCHANGES, OLESTR("There are pending changes on a row with a reference count of zero.") },
		{ DB_E_DATAOVERFLOW, OLESTR("Literal value in the command exceeded the range of the type of the associated column.") },
		{ DB_E_BADHRESULT, OLESTR("HRESULT is invalid.") },
		{ DB_E_BADLOOKUPID, OLESTR("Lookup ID is invalid.") },
		{ DB_E_BADDYNAMICERRORID, OLESTR("DynamicError ID is invalid.") },
		{ DB_E_PENDINGINSERT, OLESTR("Most recent data for a newly inserted row could not be retrieved because the insert is pending.") },
		{ DB_E_BADCONVERTFLAG, OLESTR("Conversion flag is invalid.") },
		{ DB_E_OBJECTMISMATCH, OLESTR("Operation is not allowed to this column type") },
	};
	for(int i=0;i<sizeof(desc)/sizeof(*desc);i++)
	{
		if(desc[i].hr==hr)
			return desc[i].str;
	}
	return OLESTR("The Error Description String");
}

STDMETHODIMP CCUBRIDErrorLookup::GetErrorDescription(HRESULT hrError,
					DWORD dwLookupID, DISPPARAMS *pdispparams, LCID lcid,
					BSTR *pbstrSource, BSTR *pbstrDescription)
{
	if(pbstrSource==NULL || pbstrDescription==NULL)
		return E_INVALIDARG;

	CComBSTR strSource = "CUBRIDProvider";
	*pbstrSource = strSource.Detach();

	CComBSTR strDesc;

	if(dwLookupID==0)
	{
		strDesc = GetErrorDescriptionString(hrError);
	}
	else if(dwLookupID==1) // cci error
	{
		strDesc = V_BSTR(&pdispparams[0].rgvarg[0]);
	}

	*pbstrDescription = strDesc.Detach();

	return S_OK;
}

STDMETHODIMP CCUBRIDErrorLookup::GetHelpInfo(HRESULT hrError, DWORD dwLookupID,
					LCID lcid, BSTR *pbstrHelpFile, DWORD *pdwHelpContext)
{
	if(pbstrHelpFile==NULL || pdwHelpContext==NULL)
		return E_INVALIDARG;

	*pbstrHelpFile = 0;
	*pdwHelpContext = 0;
	return S_OK;
}

STDMETHODIMP CCUBRIDErrorLookup::ReleaseErrors(const DWORD dwDynamicErrorID)
{
	return S_OK;
}

STDMETHODIMP CCUBRIDErrorInfo::GetSQLInfo(BSTR *pbstrSQLState, LONG *plNativeError)
{
	if(pbstrSQLState) *pbstrSQLState = 0;
	if(plNativeError) *plNativeError = 0;
	if(pbstrSQLState==NULL || plNativeError==NULL)
		return E_INVALIDARG;

	*pbstrSQLState = SysAllocString(m_bstrSQLState);
	if(*pbstrSQLState==NULL)
		return E_OUTOFMEMORY;
	*plNativeError = m_lNativeError;

	return S_OK;
}