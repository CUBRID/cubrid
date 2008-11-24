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
#include "Session.h"

/*
 * Commit 정책
 *   (implicit commit: IRowsetChange등에서 AutoCommit() 호출)
 *   (explicit commit: ITransaction::Commit 또는 Rowset이 닫힘)
 *
 * 1. implicit commit in manual-commit mode
 *
 *		아무 일도 하지 않는다.
 *
 * 2. implicit commit in auto-commit mode
 *
 *		TxnCallback을 호출해서 다른 Rowset을 좀비로 만든다.
 *
 * 3. explicit commit in manual-commit mode
 *
 *		실제로 Commit 하고 TxnCallback을 호출해서 모든 Rowset을 좀비로 만든다.
 *
 * 4. explicit commit in auto-commit mode
 *
 *		실제로 Commit 한다(다른 Rowset은 이미 좀비이고, 현재 Rowset은 닫는 중)
 *
 */

HRESULT CCUBRIDSession::DoCASCCICommit(bool bCommit)
{
	int hConn;
	HRESULT hr = GetConnectionHandle(&hConn);
	if(FAILED(hr)) return E_FAIL;

	T_CCI_ERROR err_buf;
	int rc = cci_end_tran(hConn, bCommit?CCI_TRAN_COMMIT:CCI_TRAN_ROLLBACK, &err_buf);
	if(rc<0) return bCommit?XACT_E_COMMITFAILED:E_FAIL;

	// TxnCallback을 호출해 좀비로 만든다.
	POSITION pos = m_grpTxnCallbacks.GetHeadPosition();
	while(pos)
		m_grpTxnCallbacks.GetNext(pos)->TxnCallback(0);

	return S_OK;
}

HRESULT CCUBRIDSession::AutoCommit(const Util::ITxnCallback *pOwner)
{
	ATLTRACE(atlTraceDBProvider, 2, "CCUBRIDSession::AutoCommit\n");

	if(m_bAutoCommit)
	{
		if(pOwner)
		{
			// TxnCallback을 호출해 pOwner를 제외하고는 좀비로 만든다.
			POSITION pos = m_grpTxnCallbacks.GetHeadPosition();
			while(pos)
				m_grpTxnCallbacks.GetNext(pos)->TxnCallback(pOwner);

			//HRESULT hr = DoCASCCICommit(true);
			//if(FAILED(hr)) return hr;
			//return S_OK;
		}
		else
		{
			// 모두 좀비가 되니까 차라리 실제 Commit을 한다.
			DoCASCCICommit(true);
		}
	}

	return S_OK;
}

HRESULT CCUBRIDSession::RowsetCommit()
{
	return DoCASCCICommit(true);
}

// bCheckOnly : isoLevel이 적당한 값인지만 체크한다.
HRESULT CCUBRIDSession::SetCASCCIIsoLevel(ISOLEVEL isoLevel, bool bCheckOnly)
{
	int hConn;
	if(!bCheckOnly)
	{
		HRESULT hr = GetConnectionHandle(&hConn);
		if(FAILED(hr)) return E_FAIL;
	}

	int cci_isolevel;
	switch(isoLevel)
	{
	case ISOLATIONLEVEL_READUNCOMMITTED:
		cci_isolevel = TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE; break;
	case ISOLATIONLEVEL_READCOMMITTED:
		cci_isolevel = TRAN_COMMIT_CLASS_COMMIT_INSTANCE; break;
	case ISOLATIONLEVEL_REPEATABLEREAD:
		cci_isolevel = TRAN_REP_CLASS_REP_INSTANCE; break;
	case ISOLATIONLEVEL_SERIALIZABLE:
		cci_isolevel = TRAN_SERIALIZABLE; break;
	default:
		return XACT_E_ISOLATIONLEVEL;
	}

	if(!bCheckOnly)
	{
		T_CCI_ERROR err_buf;
		int rc = cci_set_db_parameter(hConn, CCI_PARAM_ISOLATION_LEVEL, &cci_isolevel, &err_buf);
		if(rc<0) return E_FAIL;
		m_isoLevel = isoLevel;
	}

	ATLTRACE(atlTraceDBProvider, 2, "Current Isolation Level:[%ld]\n", isoLevel);

	return S_OK;
}

void CCUBRIDSession::EnterAutoCommitMode()
{
	m_bAutoCommit = true;

	CComVariant var;
	GetPropValue(&DBPROPSET_SESSION, DBPROP_SESS_AUTOCOMMITISOLEVELS, &var);
	SetCASCCIIsoLevel(V_I4(&var));
}

STDMETHODIMP CCUBRIDSession::GetOptionsObject(ITransactionOptions **ppOptions)
{
	ATLTRACE(atlTraceDBProvider, 2, "CCUBRIDSession::GetOptionsObject\n");
	return DB_E_NOTSUPPORTED;
}

STDMETHODIMP CCUBRIDSession::StartTransaction(ISOLEVEL isoLevel, ULONG isoFlags,
			ITransactionOptions *pOtherOptions, ULONG *pulTransactionLevel)
{
	ATLTRACE(atlTraceDBProvider, 2, "CCUBRIDSession::StartTransaction\n");

	// we do not support nested transactions
	if(!m_bAutoCommit) return XACT_E_XTIONEXISTS;
	if(isoFlags!=0) return XACT_E_NOISORETAIN;

	HRESULT hr = SetCASCCIIsoLevel(isoLevel);
	if(FAILED(hr)) return hr;

	// flat transaction이므로 항상 1
	if(pulTransactionLevel) *pulTransactionLevel = 1;
	m_bAutoCommit = false;
	return S_OK;
}

STDMETHODIMP CCUBRIDSession::Commit(BOOL fRetaining, DWORD grfTC, DWORD grfRM)
{
	ATLTRACE(atlTraceDBProvider, 2, "CCUBRIDSession::Commit\n");

	if(grfTC==XACTTC_ASYNC_PHASEONE || grfTC==XACTTC_SYNC_PHASEONE || grfRM!=0) return XACT_E_NOTSUPPORTED;
	if(m_bAutoCommit) return XACT_E_NOTRANSACTION;

	HRESULT hr = DoCASCCICommit(true);
	if(FAILED(hr)) return hr;

	// fRetaining==true 면 transaction을 유지한다.
	if(!fRetaining) m_bAutoCommit = true; //EnterAutoCommitMode();

	return S_OK;
}
    
STDMETHODIMP CCUBRIDSession::Abort(BOID *pboidReason, BOOL fRetaining, BOOL fAsync)
{
	ATLTRACE(atlTraceDBProvider, 2, "CCUBRIDSession::Abort\n");

	if(fAsync) return XACT_E_NOTSUPPORTED;
	if(m_bAutoCommit) return XACT_E_NOTRANSACTION;

	HRESULT hr = DoCASCCICommit(false);
	if(FAILED(hr)) return hr;

	// fRetaining==true 면 transaction을 유지한다.
	if(!fRetaining) m_bAutoCommit = true; //EnterAutoCommitMode();

	return S_OK;
}

STDMETHODIMP CCUBRIDSession::GetTransactionInfo(XACTTRANSINFO *pinfo)
{
	ATLTRACE(atlTraceDBProvider, 2, "CCUBRIDSession::GetTransactionInfo\n");

	if(!pinfo) return E_INVALIDARG;
	if(m_bAutoCommit) return XACT_E_NOTRANSACTION;

	int hConn;
	HRESULT hr = GetConnectionHandle(&hConn);
	if(FAILED(hr)) return E_FAIL;

	memset(pinfo, 0, sizeof(*pinfo));
	memcpy(&pinfo->uow, &hConn, sizeof(int));
	pinfo->isoLevel = m_isoLevel;
	pinfo->grfTCSupported = XACTTC_NONE;

	return S_OK;
}

