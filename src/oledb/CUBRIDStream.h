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
