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

#include "row.h"

// CCUBRIDStream
[
	coclass,
	noncreatable,
	uuid("857539EA-0140-40be-A8E5-1F347991CC0D"),
	threading("apartment"),
	registration_script("none")
]
class ATL_NO_VTABLE CCUBRIDStream :
	public ISequentialStream,
	public CConvertHelper
{
public:

private:
	int				m_hConn;
	int				m_hReq;
	char			m_OID[32];
	int				m_colIndex;
	LPOLESTR		m_colName;
	T_CCI_U_TYPE	m_colType;
	int				m_colPrecision;
	int				m_colScale;
	ULONG			m_curPos;
	char*			m_writeBuf;

public:
	CCUBRIDStream(void) : m_curPos(0), m_hConn(-1), m_hReq(0), m_colIndex(0), m_colPrecision(0), m_colScale(0)
	{
		ATLTRACE(atlTraceDBProvider, 3, "CCUBRIDStream::CCUBRIDStream\n");
		m_writeBuf = NULL;
		m_OID[0] = NULL;
	}

	~CCUBRIDStream(void)
	{
		ATLTRACE(atlTraceDBProvider, 3, "CCUBRIDStream::~CCUBRIDStream\n");
		if (m_hReq)
			cci_close_req_handle(m_hReq);
	}

	HRESULT FinalConstruct()
	{
		HRESULT hr = CConvertHelper::FinalConstruct();
		if (FAILED (hr))
			return hr;
		return S_OK;
	}

	void Initialize(int hConn, char* cur_oid, T_CCI_COL_INFO info);
	void Initialize(int hConn, char* cur_oid, const ATLCOLUMNINFO* info);

	//ISequentialStream
	STDMETHOD(Read)(void *pv, ULONG cb, ULONG *pcbRead);
    STDMETHOD(Write)(const void *pv, ULONG cb, ULONG *pcbWritten);
};
