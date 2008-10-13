// Command.cpp : Implementation of CCUBRIDCommand

#include "stdafx.h"
#include "DataSource.h"
#include "Command.h"
#include "Rowset.h"
#include "MultipleResult.h"
#include "Session.h"
#include "util.h"
#include "type.h"
#include "row.h"
#include "ProviderInfo.h"
#include "error.h"

CCUBRIDDataSource *CCUBRIDCommand::GetDataSourcePtr()
{
	return GetSessionPtr()->GetDataSourcePtr();
}

CCUBRIDSession *CCUBRIDCommand::GetSessionPtr()
{
	return CCUBRIDSession::GetSessionPtr(this);
}

CCUBRIDCommand *CCUBRIDCommand::GetCommandPtr(IObjectWithSite *pSite)
{
	CComPtr<ICommand> spCom;
	HRESULT hr = pSite->GetSite(__uuidof(ICommand), (void **)&spCom);
	// 제대로 프로그래밍 됐을때, 실패하는 경우가 있을까?
	ATLASSERT(SUCCEEDED(hr));
	// 굳이 오버헤드를 감수해가며 dynamic_cast를 쓸 필요는 없을 듯
	return static_cast<CCUBRIDCommand *>((ICommand *)spCom);
}

CCUBRIDCommand::CCUBRIDCommand()
	: m_hReq(0), m_cExpectedRuns(0), m_prepareIndex(0), m_isPrepared(false),
	  m_cParams(0), m_cParamsInQuery(0), m_pParamInfo(NULL)
{
	ATLTRACE(atlTraceDBProvider, 3, "CCUBRIDCommand::CCUBRIDCommand\n");
	m_phAccessor = NULL;
	m_pBindings = NULL;
	m_cBindings = 0;
}

CCUBRIDCommand::~CCUBRIDCommand()
{
	ATLTRACE(atlTraceDBProvider, 3, "CCUBRIDCommand::~CCUBRIDCommand\n");

	if(m_hReq>0)
	{
		cci_close_req_handle(m_hReq);
		m_hReq = 0;
	}

	if(m_pParamInfo)
	{
		for(DBCOUNTITEM i=0;i<m_cParams;i++)
			SysFreeString(m_pParamInfo[i].pwszName);
		CoTaskMemFree(m_pParamInfo);
	}

	if(m_spUnkSite)
		GetSessionPtr()->RegisterTxnCallback(this, false);
}

STDMETHODIMP CCUBRIDCommand::SetSite(IUnknown *pUnkSite)
{
	HRESULT hr = IObjectWithSiteImpl<CCUBRIDCommand>::SetSite(pUnkSite);
	GetSessionPtr()->RegisterTxnCallback(this, true);
	return hr;
}

void CCUBRIDCommand::TxnCallback(const ITxnCallback *pOwner)
{
	if(pOwner!=this)
	{
		cci_close_req_handle(m_hReq);
		m_hReq = 0;
		m_isPrepared = false;
	}
}

HRESULT CCUBRIDCommand::GetConnectionHandle(int *phConn)
{
	return GetSessionPtr()->GetConnectionHandle(phConn);
}

HRESULT CCUBRIDCommand::OnPropertyChanged(ULONG iCurSet, DBPROP* pDBProp)
{
	HRESULT hr = CUBRIDOnPropertyChanged(this, iCurSet, pDBProp);
	if(hr==S_FALSE)
        return ICommandPropertiesImpl<CCUBRIDCommand>::OnPropertyChanged(iCurSet, pDBProp);
	else
		return hr;
}

HRESULT CCUBRIDCommand::IsValidValue(ULONG iCurSet, DBPROP* pDBProp)
{
	ATLASSERT(pDBProp);
	if(pDBProp->dwPropertyID==DBPROP_ROWSET_ASYNCH)
	{
		// TODO: PREPOPULATE와 POPULATEONDEMAND 둘 다 세팅하는 건 에러겠지?
		LONG val = V_I4(&pDBProp->vValue);
		if(val==0 || val==DBPROPVAL_ASYNCH_PREPOPULATE // --> synchronous
			|| val==DBPROPVAL_ASYNCH_POPULATEONDEMAND) // --> asynchronous
			return S_OK;
		return S_FALSE;
	}

	if (pDBProp->dwPropertyID == DBPROP_MAXROWS)
	{
		if ( V_I4(&pDBProp->vValue) < 0 )
			return S_FALSE;
	}

	return ICommandPropertiesImpl<CCUBRIDCommand>::IsValidValue(iCurSet, pDBProp);
}

//ICommandText::SetCommandText
STDMETHODIMP CCUBRIDCommand::SetCommandText(REFGUID rguidDialect,LPCOLESTR pwszCommand)
{
	ATLTRACE(atlTraceDBProvider, 0, "CCUBRIDCommand::SetCommandText with SQL '%s'\n",
										pwszCommand?CW2A(pwszCommand):"");

	HRESULT hr;

	ClearError();

	hr = ICommandTextImpl<CCUBRIDCommand>::SetCommandText(rguidDialect, pwszCommand);
	Util::ExtractTableName(m_strCommandText, m_strTableName);	

	//새로 Command를 세팅한 경우 unprepared 상태로 만든다.
	m_isPrepared = false;

	return hr;
}

STDMETHODIMP CCUBRIDCommand::PrepareCommand(int hConn, REFIID riid)
{
	// OID 추출
	{
		BSTR strNewCMD = ::SysAllocStringLen(NULL, m_strCommandText.Length());
		int idxNewCMD = 0;
		int idxOID = 1;
		char in_str = 0;
		char in_comment = 0;
		char line_comment = 0;
	
		//OID의 형태는 다음과 같은 패턴을 찾는다.
		// '으로 둘러쌓여 있고 '다음에는 @가 온다.
		// @다음은 |가 두 번 와야 하고 다음으로 닫는 '가 와야 한다.
		//by risensh1ne
		wchar_t *pos = m_strCommandText;
		while(*pos)
		{
			if (in_str) {
			  if (*pos == '\'')
			    in_str = 0;
			}
			else if (in_comment) {
			  if (line_comment && *pos == '\n') {
			    in_comment = 0;
			  }
			  else if (!line_comment && wcsncmp(pos, L"*/", 2) == 0) {
			    in_comment = 0;
			    strNewCMD[idxNewCMD++] = *pos++;
			  }
			}
			else {
			  if (*pos == '\'')
			    in_str = 1;
			    
			  if (wcsncmp(pos, L"//", 2) == 0 || wcsncmp(pos, L"--", 2) == 0) {
			    in_comment = 1;
			    line_comment = 1;
			    strNewCMD[idxNewCMD++] = *pos++;
			  }
			  else if (wcsncmp(pos, L"/*", 2) == 0) {
			    in_comment = 1;
			    line_comment = 0;
			    strNewCMD[idxNewCMD++] = *pos++;
			  }
			}

			if(!in_comment && *pos=='@')
			{
				if(*(pos-1)=='\'')
				{
					wchar_t *end = wcschr(pos, '\'');
					wchar_t *delimiter = wcschr(pos, '|');
					if (delimiter)
					{
						delimiter = wcschr(delimiter, '|');
						if (delimiter && delimiter < end)
						{
							if(end==NULL) break; //error가 날 것임
							*end = 0;

							strNewCMD[idxNewCMD-1] = '?';

							m_gOID.Add(CW2A(pos));
							m_gOIDIndex.Add(idxOID);
							idxOID++;
							pos = end+1;
							continue;
						}
					}
				}
			}

			if(!in_str && !in_comment && *pos=='?') idxOID++;
			strNewCMD[idxNewCMD] = *pos;
			idxNewCMD++;
			pos++;
		}

		strNewCMD[idxNewCMD] = 0;
		m_strCommandText.Attach(strNewCMD);

		m_cParamsInQuery = idxOID - (DBCOUNTITEM)m_gOID.GetCount() - 1;
	}

	int return_code;
	T_CCI_ERROR error;
	char prepareFlag = 0;
	CComVariant propVal;

	if (riid != GUID_NULL)
	{
		//if (Util::RequestedRIIDNeedsOID(riid))
		//	prepareFlag |= CCI_PREPARE_INCLUDE_OID;
		if (Util::RequestedRIIDNeedsUpdatability(riid))
			prepareFlag |= (CCI_PREPARE_INCLUDE_OID | CCI_PREPARE_UPDATABLE);
	}
#ifndef FIXME
	GetPropValue(&DBPROPSET_ROWSET, DBPROP_IRowsetInfo, &propVal);
	if (propVal.boolVal == ATL_VARIANT_TRUE)	
		prepareFlag = CCI_PREPARE_INCLUDE_OID | CCI_PREPARE_UPDATABLE;
#endif
	GetPropValue(&DBPROPSET_ROWSET, DBPROP_IRowsetChange, &propVal);
	if (propVal.boolVal == ATL_VARIANT_TRUE)	
		prepareFlag = CCI_PREPARE_INCLUDE_OID | CCI_PREPARE_UPDATABLE;
	GetPropValue(&DBPROPSET_ROWSET, DBPROP_IRowsetUpdate, &propVal);
	if (propVal.boolVal == ATL_VARIANT_TRUE)	
		prepareFlag = CCI_PREPARE_INCLUDE_OID | CCI_PREPARE_UPDATABLE;
	GetPropValue(&DBPROPSET_ROWSET, DBPROP_IRowsetRefresh, &propVal);
	if (propVal.boolVal == ATL_VARIANT_TRUE)	
		prepareFlag = CCI_PREPARE_INCLUDE_OID | CCI_PREPARE_UPDATABLE;
	GetPropValue(&DBPROPSET_ROWSET, DBPROP_OTHERUPDATEDELETE, &propVal);
	if (propVal.boolVal == ATL_VARIANT_TRUE)
		prepareFlag = CCI_PREPARE_INCLUDE_OID | CCI_PREPARE_UPDATABLE;
	GetPropValue(&DBPROPSET_ROWSET, DBPROP_IRow, &propVal);
	if (propVal.boolVal == ATL_VARIANT_TRUE)
		prepareFlag = CCI_PREPARE_INCLUDE_OID | CCI_PREPARE_UPDATABLE;


	if ((return_code = cci_prepare(hConn, CW2A(m_strCommandText.m_str), prepareFlag, &error)) < 0)
	{
		if (return_code == CCI_ER_DBMS)
		{
			int error_code = error.err_code;
			return RaiseError(DB_E_ERRORSINCOMMAND, 1, __uuidof(ICommandPrepare), error.err_msg);
		}

		show_error("cci_prepare failed", return_code, &error);
		return E_FAIL;
	}
	m_hReq = return_code;

	m_prepareIndex = 0;
	
	return S_OK;
}

//ICommand::Execute
STDMETHODIMP CCUBRIDCommand::Execute(IUnknown * pUnkOuter, REFIID riid, DBPARAMS * pParams, 
						  LONG * pcRowsAffected, IUnknown ** ppRowset)
{
	ATLTRACE2(atlTraceDBProvider, 2, _T("CCUBRIDCommand::Execute\n"));

	HRESULT hr = S_OK;
	T_CCI_ERROR error;
	int return_code;
	int hConn;
	int numQuery;
	CCUBRIDRowset* pRowset = NULL;
	T_CCI_QUERY_RESULT	*qr = NULL;
	int result_count;
	CComVariant var;
	bool bIsAsyncExec = false;
	bool bCreateRow = false;
	bool bMultiple = false;

	// 현재 오류객체를 제거한다.
	ClearError();

	m_bIsExecuting = TRUE;

	if (ppRowset)
		*ppRowset = NULL;

	if (riid != IID_NULL && !ppRowset)
	{
		hr = E_INVALIDARG;
		goto ExecuteError2;
	}
	
	if (pUnkOuter && riid != IID_IUnknown)
		return DB_E_NOAGGREGATION;
	
	if (m_strCommandText.Length()==0)
	{
		hr = DB_E_NOCOMMAND;
		goto ExecuteError2;
	}
	
	hr = GetConnectionHandle(&hConn);
	if (FAILED(hr))
	{
		ATLTRACE(atlTraceDBProvider, 2, _T("CCUBRIDCommand::Execute - GetConnectionHandle failed\n"));
		hr = E_FAIL;
		goto ExecuteError2;
	}
	
	//cci_prepare 하는 부분
	if (!m_isPrepared || m_cExpectedRuns==m_prepareIndex)
	{
		if (SUCCEEDED(hr = PrepareCommand(hConn, riid)))
		{
			m_isPrepared = true;
			m_cExpectedRuns = 1;
		} else
		{
			goto ExecuteError2;
		}
	}

	if (m_cParamsInQuery && pParams==NULL)
	{
		hr = DB_E_PARAMNOTOPTIONAL;
		goto ExecuteError2;
	}

	if (m_cParamsInQuery)
	{
		if (pParams->cParamSets==0 || pParams->pData==NULL)
		{
			hr = E_INVALIDARG;
			goto ExecuteError2;
		}

		ATLBINDINGS *pBinding = 0;
		{
			if(!m_rgBindings.Lookup((ULONG)pParams->hAccessor, pBinding) || pBinding==NULL)
			{
				hr = DB_E_BADACCESSORHANDLE;
				goto ExecuteError2;
			}
			if(!(pBinding->dwAccessorFlags & DBACCESSOR_PARAMETERDATA))
			{
				hr = DB_E_BADACCESSORTYPE;
				goto ExecuteError2;
			}
		}

		if (pBinding->cBindings < m_cParamsInQuery)
		{
			hr = DB_E_PARAMNOTOPTIONAL;
			goto ExecuteError2;
		}

		for(DBCOUNTITEM i=0;i<pBinding->cBindings;i++)
		{
			DBBINDING &rCurBind = pBinding->pBindings[i];
			DWORD dwPart = rCurBind.dwPart;
			DBTYPE wType = rCurBind.wType & ~DBTYPE_BYREF;
			
			T_CCI_A_TYPE aType = Type::GetCASTypeA(wType);
			T_CCI_U_TYPE uType = Type::GetCASTypeU(wType);

			DBSTATUS* dwStatus = (dwPart&DBPART_STATUS) ? (DBSTATUS *)((BYTE*)pParams->pData+rCurBind.obStatus) : NULL;

			DBORDINAL iOrdinal = rCurBind.iOrdinal;
			if (m_pParamInfo && m_pParamInfo[i].iOrdinal == iOrdinal)
			{
				HRESULT _hr = m_spConvert->CanConvert(m_pParamInfo[i].wType, wType);

				if (_hr == S_FALSE)
				{
					*dwStatus = DB_E_UNSUPPORTEDCONVERSION;
					hr = DB_E_ERRORSOCCURRED;
					goto ExecuteError2;
				}

				if (FAILED(_hr))
				{
					*dwStatus = DB_E_UNSUPPORTEDCONVERSION;
					hr = _hr;
					goto ExecuteError2;
				}
			}
			//Get Revised Ordinal
			for(size_t j=0;j<m_gOID.GetCount();j++)
			{
				if(m_gOIDIndex[j]<=(int)iOrdinal)
					iOrdinal++;
			}
			
			void *pSrc = (dwPart&DBPART_VALUE) ? ((BYTE*)pParams->pData+rCurBind.obValue) : NULL;
			if( (rCurBind.wType&DBTYPE_BYREF) && pSrc )
				pSrc = *(void **)pSrc;

			DBLENGTH ulLength = 0;
			if(dwPart&DBPART_LENGTH)
				ulLength = *(DBLENGTH *)((BYTE*)pParams->pData+rCurBind.obLength);
			else
			{
				if (pSrc)
				{
					switch(wType)
					{
					case DBTYPE_STR: ulLength = (DBLENGTH)strlen((const char *)pSrc); break;
					case DBTYPE_WSTR: ulLength = (DBLENGTH)wcslen((const wchar_t *)pSrc); break;
					case DBTYPE_BYTES: ulLength = rCurBind.cbMaxLen; break;
					case DBTYPE_VARNUMERIC: ulLength = rCurBind.cbMaxLen; break;
					case DBTYPE_VARIANT:
						VARIANT* var = (VARIANT *)pSrc;
						DBTYPE varType = var->vt;
						//Length가 필요한 경우가 또 있을까?
						switch (varType)
						{
						case VT_BSTR :	ulLength = (DBLENGTH)wcslen((const wchar_t *)var->bstrVal);
										break;
						}
					}
				} else
				{
					ulLength = 0;
				}
			}
			
			void *cci_value = NULL;
			if(dwStatus && *dwStatus==DBSTATUS_S_ISNULL)
			{
				aType = CCI_A_TYPE_FIRST;
				uType = CCI_U_TYPE_NULL;
			} else if (dwStatus && *dwStatus==DBSTATUS_S_IGNORE)
			{
				*dwStatus=DBSTATUS_E_BADSTATUS;
				hr = DB_E_ERRORSOCCURRED;
				goto ExecuteError2;
			} else
			{
				if (!pSrc)
				{
					hr = DB_E_ERRORSOCCURRED;
					goto ExecuteError2;
				}
				hr = Type::OLEDBValueToCCIValue(wType, &aType, &uType, pSrc, ulLength, &cci_value, dwStatus, m_spConvert);
				if (FAILED(hr))
					goto ExecuteError2;
			}

#ifndef LTM
			//NCHAR 형식일 경우 CHAR 형식으로 변환하여 바인딩한다.
			if (uType == CCI_U_TYPE_NCHAR)
				uType = CCI_U_TYPE_CHAR;
			else if (uType == CCI_U_TYPE_VARNCHAR)
				uType = CCI_U_TYPE_STRING;
#endif

			if (return_code = cci_bind_param(m_hReq, iOrdinal, aType, cci_value, uType, 0) < 0)
			{
				show_error("cci_bind_param_failed", return_code, &error);
				if (cci_value) CoTaskMemFree(cci_value);

				
				hr = DB_E_ERRORSOCCURRED;
				*dwStatus = DBSTATUS_E_BADACCESSOR;
				goto ExecuteError2;
			}
			if (cci_value) CoTaskMemFree(cci_value);
		}
	}

	//OID Binding
	if (m_gOID.GetCount() > 0)
	{
		for (size_t i = 0; i < m_gOID.GetCount(); i++)
		{

			if (return_code = cci_bind_param(m_hReq, m_gOIDIndex[i], CCI_A_TYPE_STR, m_gOID[i].GetBuffer(), CCI_U_TYPE_OBJECT, 0) < 0)
			{
				show_error("OID binding failed", return_code, &error);
				hr = E_FAIL;
				goto ExecuteError2;
			}
		}
	}
	
	if (pcRowsAffected)
		*pcRowsAffected = DB_COUNTUNAVAILABLE;
	
	//Asynch option 확인
	/*
	GetPropValue(&DBPROPSET_ROWSET, DBPROP_ROWSET_ASYNCH, &var);
	ATLASSERT(V_VT(&var)==VT_I4);
	if (V_I4(&var)&DBPROPVAL_ASYNCH_POPULATEONDEMAND)
		bIsAsyncExec = true;
	*/

	//DBPROP_IRow 처리
	GetPropValue(&DBPROPSET_ROWSET, DBPROP_IRow, &var);
	ATLASSERT(V_VT(&var)==VT_BOOL);
	if (V_BOOL(&var) == ATL_VARIANT_TRUE)
		bCreateRow = true;
	//DBPROP_IMultipleResults
	GetPropValue(&DBPROPSET_ROWSET, DBPROP_IMultipleResults, &var);
	ATLASSERT(V_VT(&var)==VT_BOOL);
	if (V_BOOL(&var) == ATL_VARIANT_TRUE)
		bMultiple = true;

	if (bIsAsyncExec)
	{
		if ((return_code = cci_execute(m_hReq, CCI_EXEC_QUERY_ALL|CCI_EXEC_ASYNC, 0, &error)) < 0)
		{
			show_error("cci_execute_failed", return_code, &error);
			hr = E_FAIL;
			goto ExecuteError;
		}

		if ((numQuery = cci_execute_result(m_hReq, &qr, &error)) < 0)
		{
			show_error("cci_execute_result_failed", return_code, &error);
			hr = E_FAIL;
			goto ExecuteError;
		}
		result_count = 0;
	} else
	{
		if ((return_code = cci_execute(m_hReq, CCI_EXEC_QUERY_ALL, 0, &error)) < 0)
		{
			/*
			if (error.err_code == -495)
			{
				hr = DB_E_CANTCONVERTVALUE;
				goto ExecuteError;
			}
			*/

			show_error("cci_execute_failed", return_code, &error);
			hr = E_FAIL;
			goto ExecuteError;
		}

		//쿼리 수행 결과를 가져온다
		if ((numQuery = cci_execute_result(m_hReq, &qr, &error)) < 0)
		{
			show_error("cci_execute_result_failed", return_code, &error);
			hr = E_FAIL;
			goto ExecuteError;
		}
		result_count = CCI_QUERY_RESULT_RESULT(qr, 1);
	}
	T_CCI_SQLX_CMD cmd_type = (T_CCI_SQLX_CMD)CCI_QUERY_RESULT_STMT_TYPE(qr, 1);


	if (riid == IID_IMultipleResults || bMultiple)
	{
		//처리할 질의수가 하나이고 업데이트 질의일 경우,
		//모든 질의가 업데이트 질의인 경우
		//Execute 타임에 커밋한다.
		bool isUpdateAll = true;
		if (numQuery == 1)
		{
			if (cmd_type!=SQLX_CMD_SELECT &&
				cmd_type!=SQLX_CMD_GET_STATS &&
				cmd_type!=SQLX_CMD_CALL &&
				cmd_type!=SQLX_CMD_EVALUATE)
				GetSessionPtr()->AutoCommit(0);
			
		} else {

			for (int i = 1; i <= numQuery; i++)
			{
				cmd_type = (T_CCI_SQLX_CMD)CCI_QUERY_RESULT_STMT_TYPE(qr, i);
				if (cmd_type==SQLX_CMD_SELECT ||
					cmd_type==SQLX_CMD_GET_STATS ||
					cmd_type==SQLX_CMD_CALL ||
					cmd_type==SQLX_CMD_EVALUATE)
				{
					isUpdateAll = false;
					break;
				}
			}
			if (isUpdateAll)
				GetSessionPtr()->AutoCommit(0);
		}

		// MultipleResult 객체 생성
		{
			if (pUnkOuter != NULL && !InlineIsEqualUnknown(riid))
			{
				hr = DB_E_NOAGGREGATION;
				goto ExecuteError2;
			}

			CComPolyObject<CMultipleResult>* pPolyObj;
			hr = CComPolyObject<CMultipleResult>::CreateInstance(pUnkOuter, &pPolyObj);
			if (FAILED(hr))
				goto ExecuteError2;

			// Ref the created COM object and Auto release it on failure
			CComPtr<IUnknown> spUnk;
			hr = pPolyObj->QueryInterface(&spUnk);
			if (FAILED(hr))
			{
				delete pPolyObj; // must hand delete as it is not ref'd
				goto ExecuteError2;
			}

			pPolyObj->m_contained.Initialize(this, qr, pParams, numQuery, isUpdateAll);

			// Command object의 IUnknown을 MultipleResult의 Site로 설정한다.
			{
				CComPtr<IUnknown> spOuterUnk;
				QueryInterface(__uuidof(IUnknown), (void **)&spOuterUnk);
				pPolyObj->m_contained.SetSite(spOuterUnk);
			}

			hr = pPolyObj->QueryInterface(riid, (void **)ppRowset);
			if(FAILED(hr))
				goto ExecuteError2;
		}

		if (pcRowsAffected)
			*pcRowsAffected = result_count;
	}
	else if (riid == IID_IRow || bCreateRow)
	{
		// TODO: return 대신 goto ExecuteError?
		// 조건에 맞는 Row가 하나도 없을 때
		if(result_count==0)
			return DB_E_NOTFOUND;

		if(ppRowset && (
			cmd_type==SQLX_CMD_SELECT ||
			cmd_type==SQLX_CMD_GET_STATS ||
			cmd_type==SQLX_CMD_CALL ||
			cmd_type==SQLX_CMD_EVALUATE))
		{
			//Row object를 생성한다.
			CComPolyObject<CCUBRIDRow> *pRow;
			hr = CComPolyObject<CCUBRIDRow>::CreateInstance(pUnkOuter, &pRow);
			if(FAILED(hr))
				goto ExecuteError2;

			// 생성된 COM 객체를 참조해서, 실패시 자동 해제하도록 한다.
			CComPtr<IUnknown> spUnk;
			hr = pRow->QueryInterface(&spUnk);
			if(FAILED(hr))
			{
				delete pRow; // 참조되지 않았기 때문에 수동으로 지운다.
				goto ExecuteError2;
			}

			// Command object의 IUnknown을 Row의 Site로 설정한다.
			CComPtr<IUnknown> spOuterUnk;
			QueryInterface(__uuidof(IUnknown), (void **)&spOuterUnk);
			pRow->m_contained.SetSite(spOuterUnk, CCUBRIDRow::Type::FromCommand);

			hr = pRow->m_contained.Initialize(m_hReq);
			if(FAILED(hr))
			{
				delete pRow;
				goto ExecuteError2;
			}
			
			//생성된 Row 객체의 IRow 인터페이스 반환
			hr = pRow->QueryInterface(riid, (void **)ppRowset);
			if(FAILED(hr))
				goto ExecuteError2;

			// LTM에서 이에 대한 고려를 하지 않고 있다.
			// 일단 주석 처리한다.
			//if (result_count > 1)
			//	hr = DB_S_NOTSINGLETON;
		} else
		{
			GetSessionPtr()->AutoCommit(0);
			if (pcRowsAffected)
				*pcRowsAffected = result_count;
		}
	} else
	{	
		if(ppRowset && (
			cmd_type==SQLX_CMD_SELECT ||
			cmd_type==SQLX_CMD_GET_STATS ||
			cmd_type==SQLX_CMD_CALL ||
			cmd_type==SQLX_CMD_EVALUATE))
		{
			hr = CreateRowset<CCUBRIDRowset>(pUnkOuter, riid, pParams, pcRowsAffected, ppRowset, pRowset);	
			
			if (*ppRowset)
				pRowset->InitFromCommand(m_hReq, result_count, bIsAsyncExec);
		} else
		{
			GetSessionPtr()->AutoCommit(0);
			if (pcRowsAffected)
				*pcRowsAffected = result_count;
		}
	}

	m_prepareIndex++;
	m_bIsExecuting = FALSE;

	return hr;

ExecuteError:
	m_prepareIndex = 0;
	m_bIsExecuting = FALSE;
	return RaiseError(hr, 1, __uuidof(ICommand), error.err_msg);

//에러 스트링이 따로 없는 경우
ExecuteError2:
	m_prepareIndex = 0;
	m_bIsExecuting = FALSE;
	return RaiseError(hr, 0, __uuidof(ICommand));
}

STDMETHODIMP CCUBRIDCommand::Prepare(ULONG cExpectedRuns)
{
	int	hConn;
	HRESULT hr = S_OK;

	// 현재 오류객체를 제거한다.
	ClearError();

	//Command가 세팅되어 있지 않을 때
	if (m_strCommandText.Length()==0)
	{
			hr = DB_E_NOCOMMAND;
			goto PrepareError;
	}

	//Rowset이 하나라도 열려있는 상태면 DB_E_OBJECTOPEN을 리턴
	if (m_cRowsetsOpen > 0)
	{
			hr = DB_E_OBJECTOPEN;
			goto PrepareError;
	}
		
	// 현재 Command에 대해 세팅되어 있는 프로퍼티들의 상태를 검사한다
	bool bOptFailed = false;
	bool bReqFailed = false;
	{
		ULONG pcPropertySets;
		DBPROPSET *prgPropertySets;
		hr = ICommandPropertiesImpl<CCUBRIDCommand>::GetProperties(0, NULL, 
			&pcPropertySets, &prgPropertySets);
		if(FAILED(hr))
			goto PrepareError;

		for (ULONG i = 0; i < prgPropertySets->cProperties; i++)
		{
			if (prgPropertySets->rgProperties[i].dwOptions == DBPROPOPTIONS_REQUIRED &&
				prgPropertySets->rgProperties[i].dwStatus == DBPROPSTATUS_NOTSET)
				bReqFailed = true;
			if (prgPropertySets->rgProperties[i].dwOptions == DBPROPOPTIONS_OPTIONAL &&
				prgPropertySets->rgProperties[i].dwStatus == DBPROPSTATUS_NOTSET)
				bOptFailed = true;

			VariantClear(&(prgPropertySets->rgProperties[i].vValue));
		}
		CoTaskMemFree(prgPropertySets->rgProperties);
		CoTaskMemFree(prgPropertySets);
	}

	if (bReqFailed)
	{
		hr = DB_E_ERRORSOCCURRED;
		goto PrepareError;
	}

	//Connection handle을 가져온다
	hr = GetConnectionHandle(&hConn);
	if (FAILED(hr))
		goto PrepareError;

	hr = PrepareCommand(hConn);
	if (FAILED(hr))
		goto PrepareError;

	//몇 번 주기로 prepare를 다시 할 것인지를 저장
	m_cExpectedRuns = cExpectedRuns;

	//현재 Command에 대해 열려있는 Rowset 수를 0으로
	m_cRowsetsOpen = 0;
	m_isPrepared = true;

	return bOptFailed ? DB_S_ERRORSOCCURRED : S_OK;

PrepareError:
	return RaiseError(hr, 0, __uuidof(ICommandPrepare));
}

STDMETHODIMP CCUBRIDCommand::Unprepare(void)
{
	// 현재 오류객체를 제거한다.
	ClearError();

	HRESULT hr = S_OK;

	if (m_cRowsetsOpen > 0)
	{
		hr = DB_E_OBJECTOPEN;
		goto UnprepareError;
	}
		
	//Prepare된 상태가 아닐때
	if (!m_hReq)
		return S_OK;

	//if (!m_isPrepared)
	//	return DB_E_NOTPREPARED;

	//prepare 주기를 0으로, request handle을 해제
	m_cExpectedRuns = 0;

	if (cci_close_req_handle(m_hReq) < 0)
	{
		hr = E_FAIL;
		goto UnprepareError;
	}

	m_hReq = 0;
	m_isPrepared = false;

	return hr;
UnprepareError:
	return RaiseError(hr, 0, __uuidof(ICommandPrepare));
}

STDMETHODIMP CCUBRIDCommand::MapColumnIDs(DBORDINAL cColumnIDs, const DBID rgColumnIDs[],
							DBORDINAL rgColumns[])
{
	ClearError();

	if ((cColumnIDs != 0 && rgColumnIDs == NULL) || rgColumns == NULL)
			return E_INVALIDARG;

	if (m_strCommandText.Length()==0)
		return DB_E_NOCOMMAND;

	if (!m_isPrepared)
		return DB_E_NOTPREPARED;

	return IColumnsInfoImpl<CCUBRIDCommand>::MapColumnIDs(cColumnIDs, rgColumnIDs, rgColumns);
}

STDMETHODIMP CCUBRIDCommand::GetColumnInfo(DBORDINAL *pcColumns, DBCOLUMNINFO **prgInfo,
							 OLECHAR **ppStringsBuffer)
{
	ATLTRACE(atlTraceDBProvider, 2, _T("UniCommand:GetColumnInfo\n"));

	HRESULT hr = S_OK;

	ClearError();

	if (pcColumns)
		*pcColumns = 0;
	if (prgInfo)
		*prgInfo = NULL;
	if (ppStringsBuffer)
		*ppStringsBuffer = NULL;

	if (!pcColumns || !prgInfo || !ppStringsBuffer)
	{
		return E_INVALIDARG;
	}

	if(m_strCommandText.Length()==0)
		return DB_E_NOCOMMAND;

	if(!m_isPrepared)
		return DB_E_NOTPREPARED;
	
	hr = IColumnsInfoImpl<CCUBRIDCommand>::GetColumnInfo(pcColumns, prgInfo, ppStringsBuffer);
	
	if (*pcColumns == 0)
	{
		*prgInfo = NULL;
		*ppStringsBuffer = NULL;
	}

	return hr;
}

ATLCOLUMNINFO* CCUBRIDCommand::GetColumnInfo(CCUBRIDCommand* pv, ULONG* pcInfo)
{
	if(!pv->m_Columns.m_pInfo)
	{
		CComVariant var;
		pv->GetPropValue(&DBPROPSET_ROWSET, DBPROP_BOOKMARKS, &var);

		//MAX_STRING_LENGTH를 얻기 위해
		CCUBRIDDataSource* pDS = pv->GetDataSourcePtr();

		// TODO : check error?
		pv->m_Columns.GetColumnInfo(pv->m_hReq, V_BOOL(&var)==ATL_VARIANT_TRUE, pDS->PARAM_MAX_STRING_LENGTH);
	}

	if(pcInfo)
		*pcInfo = pv->m_Columns.m_cColumns;
	return pv->m_Columns.m_pInfo;
}

STDMETHODIMP CCUBRIDCommand::GetParameterInfo( 
            DB_UPARAMS *pcParams,
            DBPARAMINFO **prgParamInfo,
            OLECHAR **ppNamesBuffer)
{
	ATLTRACE(atlTraceDBProvider, 2, _T("CCUBRIDCommand::GetParameterInfo\n"));

	ClearError();

	if(pcParams)
		*pcParams = 0;
	if(prgParamInfo)
		*prgParamInfo = 0;
	if(ppNamesBuffer)
		*ppNamesBuffer = 0;
		
	if(pcParams==NULL || prgParamInfo==NULL)
		return E_INVALIDARG;

	if(m_cParams==0)
		return DB_E_PARAMUNAVAILABLE;

	// SQL에서 파라미터 정보를 얻을 수 없으므로
	// DB_E_NOCOMMAND, DB_E_NOTPREPARED를 반환하는 경우는 없다.

	*prgParamInfo = (DBPARAMINFO *)CoTaskMemAlloc(m_cParams*sizeof(DBPARAMINFO));
	if(*prgParamInfo==NULL)
		return E_OUTOFMEMORY;

	if(ppNamesBuffer)
	{
		size_t cCount = 0;
		for(DBCOUNTITEM i=0;i<m_cParams;i++)
		{
			//끝의 NULL 문자까지 고려하여 cCount를 계산한다
			if(m_pParamInfo[i].pwszName)
				cCount += wcslen(m_pParamInfo[i].pwszName) + 1;
		}

		*ppNamesBuffer = (OLECHAR *)CoTaskMemAlloc(cCount*sizeof(OLECHAR));
		if(*ppNamesBuffer==NULL)
		{
			CoTaskMemFree(*prgParamInfo);
			*prgParamInfo = 0;
			return E_OUTOFMEMORY;
		}
	}

	//파라메터 갯수 카피
	*pcParams = m_cParams;
	//파라메터 정보 구조체 배열 카피
	memcpy(*prgParamInfo, m_pParamInfo, m_cParams*sizeof(DBPARAMINFO));

	//파라메터 이름을 이어 붙인다.
	size_t cCount = 0;
	for(DBCOUNTITEM i=0;i<m_cParams;i++)
	{
		if(m_pParamInfo[i].pwszName)
		{
			wcscpy((*ppNamesBuffer)+cCount, m_pParamInfo[i].pwszName);
			(*prgParamInfo)[i].pwszName = (*ppNamesBuffer)+cCount;
			cCount += wcslen(m_pParamInfo[i].pwszName) + 1;
		}
	}

	return S_OK;
}

STDMETHODIMP CCUBRIDCommand::MapParameterNames( 
			DB_UPARAMS cParamNames,
			const OLECHAR *rgParamNames[],
			DB_LPARAMS rgParamOrdinals[])
{
	ULONG j;

	ATLTRACE(atlTraceDBProvider, 2, _T("CCUBRIDCommand::MapParameterNames\n"));

	ClearError();

	//cParamNames가 0이면 그냥 S_OK를 리턴
	if (cParamNames == 0)
		return S_OK;

	//cParamNames가 0이 아니고 rgParamNames 혹은 rgParamOrdinals가 NULL이면
	//E_INVALIDARG를 리턴
	if (rgParamNames == NULL || rgParamOrdinals == NULL)
		return E_INVALIDARG;

	if (m_cParams == 0 && (!m_strCommandText || wcslen(m_strCommandText) == 0))
	{
		return DB_E_NOCOMMAND;
	}

	if (m_cParams == 0 && !m_isPrepared)
		return DB_E_NOTPREPARED;

	bool bSucceeded = false;
	bool bFailed = false;
	
	for (ULONG i = 0; i < cParamNames; i++)
	{
		if (!rgParamNames[i])
		{
			bFailed = true;
			rgParamOrdinals[i] = 0;
			continue;
		}

		bool bFound = false;
		for (j = 0; j < m_cParams; j++)
		{
			if (m_pParamInfo[j].pwszName &&
				_wcsicmp(rgParamNames[i], m_pParamInfo[j].pwszName) == 0)
			{
				bFound = true;
				break;
			}
		}

		//파라메터 이름이 같은 것이 발견되면 bSucceeded를 true로 세팅하고
		//하나라도 발견되지 못하면 bFailed를 true로 세팅
		if (bFound)
		{
			rgParamOrdinals[i] = j + 1;
			bSucceeded = true;
		}
		else
		{
			rgParamOrdinals[i] = 0;
			bFailed = true;
		}

	}
	
	if(!bFailed)
		return S_OK;
	else
		return bSucceeded ? DB_S_ERRORSOCCURRED : DB_E_ERRORSOCCURRED;
}

STDMETHODIMP CCUBRIDCommand::SetParameterInfo( 
	DB_UPARAMS cParams,
	const DB_UPARAMS rgParamOrdinals[],
	const DBPARAMBINDINFO rgParamBindInfo[])
{
	ATLTRACE(atlTraceDBProvider, 2, _T("CCUBRIDCommand::SetParameterInfo\n"));

	HRESULT hr = S_OK;
	DBCOUNTITEM cNewParams = 0;
	DBPARAMINFO *pNewParamInfo = 0;

	ClearError();

	//cParam이 0이면 기존에 세팅된 파라메터 정보를 모두 삭제한다
	if (cParams == 0)
		goto FreeAndSet;
	
	//함수 argument 체크
	if (rgParamOrdinals == NULL)
		return E_INVALIDARG;

	//현재 Command에 대해 Rowset이 열려 있으면 DB_E_OBJECTOPEN 리턴
	if(m_cRowsetsOpen>0)
		return DB_E_OBJECTOPEN;

	//Ordinal이 하나라도 0이면 E_INVALIDARG를 리턴
	if (rgParamOrdinals)
	{
		for (DBCOUNTITEM i = 0; i < cParams; i++)
			if (rgParamOrdinals[i] == 0)
				return E_INVALIDARG;
	}

	//pwszDataSourceType 항목이 NULL이면 E_INVALIDARG를 리턴
	//현재 DefaultTypeConversion은 지원하지 않는 것으로 함
	//dwFlags 항목이 valid하지 않은 경우 E_INVALIDARG를 리턴
	if (rgParamBindInfo)
	{
		for (DBCOUNTITEM i = 0; i < cParams; i++)
		{
			if (!rgParamBindInfo[i].pwszDataSourceType)
				return E_INVALIDARG;

			if ((rgParamBindInfo[i].dwFlags != 0) &&
				!(rgParamBindInfo[i].dwFlags & DBPARAMFLAGS_ISINPUT) &&
				!(rgParamBindInfo[i].dwFlags & DBPARAMFLAGS_ISOUTPUT) &&
				!(rgParamBindInfo[i].dwFlags & DBPARAMFLAGS_ISSIGNED) &&
				!(rgParamBindInfo[i].dwFlags & DBPARAMFLAGS_ISNULLABLE) &&
				!(rgParamBindInfo[i].dwFlags & DBPARAMFLAGS_ISLONG) &&
				!(rgParamBindInfo[i].dwFlags & DBPARAMFLAGS_SCALEISNEGATIVE))
				return E_INVALIDARG;
		}
	}

	if (rgParamBindInfo)
	{
		cNewParams = m_cParams;
		pNewParamInfo = (DBPARAMINFO *)CoTaskMemAlloc((m_cParams+cParams)*sizeof(DBPARAMINFO));

		// 기존 파라미터 정보를 복사한다.
		if(m_cParams)
		{
			memcpy(pNewParamInfo, m_pParamInfo, m_cParams*sizeof(DBPARAMINFO));
			for(DBCOUNTITEM i=0;i<m_cParams;i++)
				pNewParamInfo[i].pwszName = SysAllocString(m_pParamInfo[i].pwszName);
		}

		for(DBCOUNTITEM i=0;i<cParams;i++)
		{
			// 새로운 파라미터의 ordinal이 기존 정보의 ordinal과 겹치는지의 여부
			DBCOUNTITEM j;
			for(j=0;j<cNewParams;j++)
			{
				if(pNewParamInfo[j].iOrdinal==rgParamOrdinals[i])
					break;
			}
			if(j==cNewParams)
			{	// 못 찾았음
				cNewParams++;
			}
			else
			{	// 찾았음. 기존 정보를 override
				SysFreeString(pNewParamInfo[j].pwszName);
				hr = DB_S_TYPEINFOOVERRIDDEN;
			}

			pNewParamInfo[j].wType = Type::GetOledbTypeFromName(rgParamBindInfo[i].pwszDataSourceType);
			pNewParamInfo[j].pwszName = SysAllocString(rgParamBindInfo[i].pwszName);
			pNewParamInfo[j].dwFlags = rgParamBindInfo[i].dwFlags;
			pNewParamInfo[j].iOrdinal = rgParamOrdinals[i];
			pNewParamInfo[j].pTypeInfo = NULL;
			pNewParamInfo[j].ulParamSize = rgParamBindInfo[i].ulParamSize;
			pNewParamInfo[j].bPrecision = rgParamBindInfo[i].bPrecision;
			pNewParamInfo[j].bScale = rgParamBindInfo[i].bScale;
		}

		if(cNewParams==0)
		{
			CoTaskMemFree(pNewParamInfo);
			pNewParamInfo = 0;
		}
		else
		{
			//rgParamBindInfo구조체 배열의 pwszName 필드는 모두 NULL이던지 모두 값을 가지던지 해야 한다
			//그렇지 못하면 DB_E_BADPARAMETERNAME을 리턴
			bool bIsNull = (pNewParamInfo[0].pwszName==NULL);
			for(DBCOUNTITEM i=1;i<cNewParams;i++)
			{
				if( (pNewParamInfo[i].pwszName==NULL) != bIsNull )
				{
					for(DBCOUNTITEM i=0;i<cNewParams;i++)
						SysFreeString(pNewParamInfo[i].pwszName);
					CoTaskMemFree(pNewParamInfo);
					return DB_E_BADPARAMETERNAME;
				}
			}
		}
	}
	else // rgParamBindInfo==NULL
	{	// rgParamOrdinals 배열의 Ordinal에 해당하는 파라메터들을 Discard한다.
		if (m_cParams)
		{
			cNewParams = 0;
			pNewParamInfo = (DBPARAMINFO *)CoTaskMemAlloc(m_cParams*sizeof(DBPARAMINFO));

			for(DBCOUNTITEM i=0;i<m_cParams;i++)
			{
				bool bFound = false;
				for(DBCOUNTITEM j=0;j<cParams;j++)
				{
					if(m_pParamInfo[i].iOrdinal==rgParamOrdinals[j])
					{
						bFound = true;
						break;
					}
				}
				if(!bFound)
				{
					memcpy(&pNewParamInfo[cNewParams], &m_pParamInfo[i], sizeof(DBPARAMINFO));
					pNewParamInfo[cNewParams].pwszName = SysAllocString(m_pParamInfo[i].pwszName);
					cNewParams++;
				}
			}

			if(cNewParams==0)
			{
				CoTaskMemFree(pNewParamInfo);
				pNewParamInfo = 0;
			}
		}
	}

FreeAndSet:
	//기존의 정보 해제
	if(m_pParamInfo)
	{
		for(DBCOUNTITEM i=0;i<m_cParams;i++)
			SysFreeString(m_pParamInfo[i].pwszName);
		CoTaskMemFree(m_pParamInfo);
	}

	m_pParamInfo = pNewParamInfo;
	m_cParams = cNewParams;

	// sort
	if(m_cParams)
	{
		for(DBCOUNTITEM i=0;i<m_cParams;i++)
		{
			DBORDINAL iMin = ~0;
			DBCOUNTITEM iMinIndex = i;
			for(DBCOUNTITEM j=i;j<m_cParams;j++)
			{
				if(m_pParamInfo[j].iOrdinal<iMin)
				{
					iMin = m_pParamInfo[j].iOrdinal;
					iMinIndex = j;
				}
			}

			if(i!=iMinIndex)
			{
				DBPARAMINFO tmp;
				memcpy(&tmp, &m_pParamInfo[i], sizeof(DBPARAMINFO));
				memcpy(&m_pParamInfo[i], &m_pParamInfo[iMinIndex], sizeof(DBPARAMINFO));
				memcpy(&m_pParamInfo[iMinIndex], &tmp, sizeof(DBPARAMINFO));
			}

			// TODO: CUBRIDCas 에서 넘겨준 컬럼 이름은 소문자, LTM은 대문자라 문제가 생긴다.
			// 일단 소문자로 만들면 LTM은 통과한다.
			if(m_pParamInfo[i].pwszName)
				_wcslwr(m_pParamInfo[i].pwszName);
		}
	}

	return hr;
}
