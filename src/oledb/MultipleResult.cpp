// MultipleResult.cpp : Implementation of CMultipleResult

#include "stdafx.h"
#include "MultipleResult.h"
#include "Rowset.h"
#include "Session.h"
#include "Row.h"
#include "Error.h"

CCUBRIDSession *CMultipleResult::GetSessionPtr()
{
	return GetCommandPtr()->GetSessionPtr();
}

CCUBRIDCommand *CMultipleResult::GetCommandPtr()
{
	return m_command;
}

CMultipleResult::~CMultipleResult()
{
	ATLTRACE2(atlTraceDBProvider, 2, _T("CMultipleResult::~CMultipleResult()\n"));
	this->GetSessionPtr()->AutoCommit(NULL);
	if(m_spUnkSite)
		GetSessionPtr()->RegisterTxnCallback(this, false);
}

HRESULT CMultipleResult::SetSite(IUnknown *pUnkSite)
{
	HRESULT hr = IObjectWithSiteImpl<CMultipleResult>::SetSite(pUnkSite);
	GetSessionPtr()->RegisterTxnCallback(this, true);
	return hr;
}

void CMultipleResult::TxnCallback(const ITxnCallback *pOwner)
{
	if(pOwner!=this)
	{
		cci_close_req_handle(m_hReq);
		m_hReq = 0;
		m_bInvalidated = true;
	}
}

HRESULT CMultipleResult::GetResult(IUnknown *pUnkOuter, DBRESULTFLAG lResultFlag,
            REFIID riid, DBROWCOUNT *pcRowsAffected, IUnknown **ppRowset)
{
	ATLTRACE2(atlTraceDBProvider, 2, _T("CMultipleResult::GetResult\n"));

	HRESULT hr = S_OK;
	CCUBRIDCommand* cmd = NULL;
	CCUBRIDRowset* pRowset = NULL;
	int result_count = 0, rc;
	T_CCI_SQLX_CMD cmd_type;
	T_CCI_ERROR error;

	ClearError();
	error.err_msg[0] = 0;

	//E_INVALIDARG 처리
	if (!(lResultFlag == DBRESULTFLAG_DEFAULT || lResultFlag == DBRESULTFLAG_ROWSET
		|| lResultFlag == DBRESULTFLAG_ROW))
	{
		hr = E_INVALIDARG;
		goto error;
	}
	
	//E_NOINTERFACE 처리
	if (lResultFlag == DBRESULTFLAG_ROWSET)
	{
		if (riid == IID_IRow)
			hr = E_NOINTERFACE;
	} else if (lResultFlag == DBRESULTFLAG_ROW)
	{
		if (riid == IID_IRowset)
			hr = E_NOINTERFACE;
	}

	if ( riid == IID_IRowsetUpdate )
		hr = E_NOINTERFACE;
	else if ( riid == IID_IMultipleResults)
		hr = E_NOINTERFACE;

	if (hr == E_NOINTERFACE)
		goto error;
	
	if (pUnkOuter && riid != IID_IUnknown)
	{
		hr = DB_E_NOAGGREGATION;
		goto error;
	}

	if (pcRowsAffected)
		*pcRowsAffected = DB_COUNTUNAVAILABLE;

	//모든 쿼리가 다 수행되었으면 m_qr 해제 후 DB_S_NORESULT 리턴 
	if (m_resultIndex > m_numQuery)
	{
		ATLTRACE2(atlTraceDBProvider, 2, _T("DB_S_NORESULT\n"));

		cci_query_result_free(m_qr, m_numQuery);
		m_qr = NULL;
		if (ppRowset)
			*ppRowset = NULL;
		if (pcRowsAffected)
			*pcRowsAffected = DB_COUNTUNAVAILABLE;

		return DB_S_NORESULT;
	}

	if (m_bInvalidated)
	{
		hr = E_UNEXPECTED;
		goto error;
	}

	//Rowset이 열려있는 경우 DB_E_OBJECTOPEN을 리턴
	if (m_command->m_cRowsetsOpen > 0)
	{
		hr = DB_E_OBJECTOPEN;
		goto error;
	}

	//다음번 결과셋을 가져온다
	if (m_resultIndex != 1 && !m_bCommitted)
	{

		rc = cci_next_result(m_hReq, &error);
		if (rc < 0)
		{
			hr = E_FAIL;
			goto error;
		}
	}

	result_count = CCI_QUERY_RESULT_RESULT(m_qr, m_resultIndex);
	cmd_type = (T_CCI_SQLX_CMD)CCI_QUERY_RESULT_STMT_TYPE(m_qr, m_resultIndex);
	//쿼리 인덱스 증가
	m_resultIndex++;

	//CCUBRIDCommand 객체에 대한 레퍼런스를 받아옴
	cmd = m_command;

	if (riid == IID_IRow)
	{	
		if (ppRowset && (cmd_type==SQLX_CMD_SELECT ||
		   cmd_type==SQLX_CMD_GET_STATS ||
		   cmd_type==SQLX_CMD_CALL ||
		   cmd_type==SQLX_CMD_EVALUATE))
		{
			CComPolyObject<CCUBRIDRow> *pRow;
			HRESULT hr = CComPolyObject<CCUBRIDRow>::CreateInstance(pUnkOuter, &pRow);
			if(FAILED(hr)) goto error;

			// 생성된 COM 객체를 참조해서, 실패시 자동 해제하도록 한다.
			CComPtr<IUnknown> spUnk;
			hr = pRow->QueryInterface(&spUnk);
			if(FAILED(hr))
			{
				delete pRow; // 참조되지 않았기 때문에 수동으로 지운다.
				goto error;
			}

			//Command object의 IUnknown을 Row의 Site로 설정한다.
			CComPtr<IUnknown> spOuterUnk;
			GetCommandPtr()->QueryInterface(__uuidof(IUnknown), (void **)&spOuterUnk);
			hr = pRow->m_contained.SetSite(spOuterUnk,  CCUBRIDRow::Type::FromCommand);
			if(FAILED(hr)) goto error;
			hr = pRow->m_contained.Initialize(m_hReq);

			if (FAILED(hr))
			{
				if (ppRowset)
					*ppRowset = NULL;
				if (pcRowsAffected)
					*pcRowsAffected = DB_COUNTUNAVAILABLE;

				return RaiseError(hr, 0, __uuidof(IMultipleResults));
			}
			//생성된 Row 객체의 IRow 인터페이스 반환
			hr = pRow->QueryInterface(riid, (void **)ppRowset);
			if(FAILED(hr)) goto error;
			
			//if (result_count > 1)
			//	return DB_S_NOTSINGLETON;
		} else
		{
			if (cmd_type == SQLX_CMD_SELECT)
			{
				if (pcRowsAffected)
					*pcRowsAffected = -1;
			}
			else
			{
				if (pcRowsAffected)
					*pcRowsAffected = result_count;
			}

			if (m_resultIndex > m_numQuery) //마지막 쿼리인 경우
			{
				GetSessionPtr()->AutoCommit(this);
				m_hReq = 0;
				m_bCommitted = true;
			}

			if (ppRowset != NULL)
				*ppRowset = NULL;
		}
	} 
	//IID_IRowset이 아닌 경우 Rowset을 생성
	//IID_IRow 처럼 다르게 처리해 주어야 하는 경우가 또 있을까?
	else
	{	
		if(cmd_type==SQLX_CMD_SELECT ||
		   cmd_type==SQLX_CMD_GET_STATS ||
		   cmd_type==SQLX_CMD_CALL ||
		   cmd_type==SQLX_CMD_EVALUATE)
		{
			if (ppRowset)
			{
				if (riid != IID_NULL)
				{
					ATLTRACE2(atlTraceDBProvider, 2, _T("CMultipleResult::CreateRowset\n"));

					//Rowset object 생성
					hr = cmd->CreateRowset<CCUBRIDRowset>(pUnkOuter, riid, m_pParams, pcRowsAffected, ppRowset, pRowset);
					if (FAILED(hr))	goto error;

					pRowset->InitFromCommand(m_hReq, result_count);
				} else
					*ppRowset = NULL;
			}
		} else
		{
			if (pcRowsAffected)
				*pcRowsAffected = result_count;

			//커밋을 하지 않으므로 다음번 쿼리에서 현재 쿼리의 업데이트 내용을 볼 수 없다!!
			//마지막 쿼리인 경우는 커밋

			if (m_resultIndex > m_numQuery && m_numQuery > 1) //질의수가 2개 이상이면서 마지막 질의인 경우 커밋
			{
				GetSessionPtr()->AutoCommit(this);
				m_hReq = 0;
				m_bCommitted = true;
			}

			*ppRowset = NULL;
		}
	} 
	

	return S_OK;

error:
	if (ppRowset)
		*ppRowset = NULL;
	if (pcRowsAffected)
		*pcRowsAffected = DB_COUNTUNAVAILABLE;
	m_hReq = 0;

	if (strlen(error.err_msg) > 0)
		return RaiseError(hr, 1, __uuidof(IMultipleResults), error.err_msg);
	else
		return RaiseError(hr, 0, __uuidof(IMultipleResults));
}