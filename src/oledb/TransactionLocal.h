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

template <class T>
class ATL_NO_VTABLE ITransactionLocalImpl : public ITransactionLocal
{
private:
	ISOLEVEL m_isoLevel;
	bool m_bAutoCommit;

	HRESULT DoCASCCICommit(bool bCommit)
	{
		int hConn;
		HRESULT hr = ((T *)this)->GetConnectionHandle(&hConn);
		if(FAILED(hr)) return E_FAIL;

		T_CCI_ERROR err_buf;
		int rc = cci_end_tran(hConn, bCommit?CCI_TRAN_COMMIT:CCI_TRAN_ROLLBACK, &err_buf);
		if(rc<0) return E_FAIL;

		return S_OK;
	}

	HRESULT SetCASCCIIsoLevel(int hConn, ISOLEVEL isoLevel)
	{
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
			// cci_isolevel = TRAN_SERIALIZABLE; break;
			cci_isolevel = TRAN_REP_CLASS_REP_INSTANCE; break;
		default:
			return XACT_E_ISOLATIONLEVEL;
		}

		T_CCI_ERROR err_buf;
		int rc = cci_set_db_parameter(hConn, CCI_PARAM_ISOLATION_LEVEL, &cci_isolevel, &err_buf);
		if(rc<0) return E_FAIL;

		m_isoLevel = isoLevel;
		return S_OK;
	}
public:
	void EnterAutoCommitMode()
	{
		m_bAutoCommit = true;

		int hConn;
		HRESULT hr = ((T *)this)->GetConnectionHandle(&hConn);
		if(FAILED(hr)) return;

		CComVariant var;
		((T *)this)->GetPropValue(&DBPROPSET_SESSION, DBPROP_SESS_AUTOCOMMITISOLEVELS, &var);

		SetCASCCIIsoLevel(hConn, V_I4(&var));
	}

	HRESULT ChangeAutoCommitIsoLevels(ISOLEVEL isoLevel)
	{
		if(!m_bAutoCommit) return S_OK;

		int hConn;
		HRESULT hr = ((T *)this)->GetConnectionHandle(&hConn);
		if(FAILED(hr)) return E_FAIL;

		return SetCASCCIIsoLevel(hConn, isoLevel);
	}

	// AutoCommit을 쉽게 부를 수 있도록 도와주는 함수
	static HRESULT AutoCommit(IObjectWithSite *object)
	{
		ATLASSERT(object);

		CComPtr<ITransactionLocal> spTransactionLocal;
		HRESULT hr = object->GetSite(__uuidof(ITransactionLocal), (void **)&spTransactionLocal);
		if(FAILED(hr)) return hr;

		ITransactionLocalImpl *pTxn =
			static_cast<ITransactionLocalImpl *>((ITransactionLocal *)spTransactionLocal);
		return pTxn->AutoCommit();
	}

	// statement 수행이 끝나면 이 함수를 호출한다.
	// Transaction을 시작하지 않은 상태면 Commit 한다.
	// Transaction을 시작했으면 Commit 또는 Rollback은
	// ITransaction::Commit과 ITransaction::Abort에서 한다.
	HRESULT AutoCommit()
	{
		ATLTRACE(atlTraceDBProvider, 2, "ITransactionLocalImpl::AutoCommit\n");

		if(m_bAutoCommit)
		{
			HRESULT hr = DoCASCCICommit(true);
			if(FAILED(hr)) return hr;
		}

		return S_OK;
	}

	STDMETHOD(GetOptionsObject)(ITransactionOptions **ppOptions)
	{
		ATLTRACE(atlTraceDBProvider, 2, "ITransactionLocalImpl::GetOptionsObject\n");
		return DB_E_NOTSUPPORTED;
	}

	STDMETHOD(StartTransaction)(ISOLEVEL isoLevel, ULONG isoFlags,
				ITransactionOptions *pOtherOptions, ULONG *pulTransactionLevel)
	{
		ATLTRACE(atlTraceDBProvider, 2, "ITransactionLocalImpl::StartTransaction\n");

		// we do not support nested transactions
		if(!m_bAutoCommit) return XACT_E_XTIONEXISTS;
		if(isoFlags!=0) return XACT_E_NOISORETAIN;

		int hConn;
		HRESULT hr = ((T *)this)->GetConnectionHandle(&hConn);
		if(FAILED(hr)) return hr;

		hr = SetCASCCIIsoLevel(hConn, isoLevel);
		if(FAILED(hr)) return hr;

		// flat transaction이므로 항상 1
		if(pulTransactionLevel) *pulTransactionLevel = 1;
		m_bAutoCommit = false;
		return S_OK;
	}

    STDMETHOD(Commit)(BOOL fRetaining, DWORD grfTC, DWORD grfRM)
	{
		ATLTRACE(atlTraceDBProvider, 2, "ITransactionLocalImpl::Commit\n");

		if(grfTC!=XACTTC_NONE || grfRM!=0) return XACT_E_NOTSUPPORTED;
		if(m_bAutoCommit) return XACT_E_NOTRANSACTION;

		HRESULT hr = DoCASCCICommit(true);
		if(FAILED(hr)) return hr;

		// fRetaining==true 면 transaction을 유지한다.
		if(!fRetaining) EnterAutoCommitMode();

		return S_OK;
	}
        
	STDMETHOD(Abort)(BOID *pboidReason, BOOL fRetaining, BOOL fAsync)
	{
		ATLTRACE(atlTraceDBProvider, 2, "ITransactionLocalImpl::Abort\n");

		if(fAsync) return XACT_E_NOTSUPPORTED;
		if(m_bAutoCommit) return XACT_E_NOTRANSACTION;

		HRESULT hr = DoCASCCICommit(false);
		if(FAILED(hr)) return hr;

		// fRetaining==true 면 transaction을 유지한다.
		if(!fRetaining) EnterAutoCommitMode();

		return S_OK;
	}
        
	STDMETHOD(GetTransactionInfo)(XACTTRANSINFO *pinfo)
	{
		ATLTRACE(atlTraceDBProvider, 2, "ITransactionLocalImpl::GetTransactionInfo\n");

		if(!pinfo) return E_INVALIDARG;
		if(m_bAutoCommit) return XACT_E_NOTRANSACTION;

		memset(pinfo, 0, sizeof(pinfo));
		// TODO: pinfo->uow?
		pinfo->isoLevel = m_isoLevel;
		pinfo->grfTCSupported = XACTTC_NONE;

		return S_OK;
	}
};
