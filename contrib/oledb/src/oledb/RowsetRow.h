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

class CCUBRIDRowsetRowColumn;
class CCUBRIDRowset;

/*
 * Storage의 한 Row의 Local Copy 또는 Deferred Update 데이터를 저장해두는 클래스.
 *
 * m_rgColumns: BOOKMARK 컬럼 포함. Storage 상의 순서와 동일
 * m_iRowset: 0부터 시작. Storage 상의 위치.
 */
class CCUBRIDRowsetRow
{
public:
	typedef DBCOUNTITEM KeyType;
	DWORD m_dwRef;
	DBPENDINGSTATUS m_status;
	KeyType m_iRowset;
	KeyType m_iOriginalRowset; // not used
	char m_szOID[32]; // OID of Row
private:
	CCUBRIDRowsetRowColumn *m_rgColumns;
	DBORDINAL m_cColumns;
	ATLCOLUMNINFO *m_pInfo;
	CComPtr<IDataConvert> m_spConvert;
	CAtlArray<CStringA>* m_defaultVal;

private:
	// m_rgColumns 메모리를 해제
	void FreeData();
public:
	CCUBRIDRowsetRow(DBCOUNTITEM iRowset, DBORDINAL cCols, ATLCOLUMNINFO *pInfo, 
				CComPtr<IDataConvert> &spConvert, CAtlArray<CStringA>* defaultVal = NULL)
		: m_dwRef(0), m_rgColumns(0), m_status(0), m_iRowset(iRowset),
		  m_iOriginalRowset(iRowset), m_cColumns(cCols), m_pInfo(pInfo), m_defaultVal(defaultVal),
		  m_spConvert(spConvert)
	{
		ATLTRACE(atlTraceDBProvider, 3, "CCUBRIDRowsetRow::CCUBRIDRowsetRow\n");
		m_szOID[0] = NULL;
	}

	~CCUBRIDRowsetRow()
	{
		ATLTRACE(atlTraceDBProvider, 3, "CCUBRIDRowsetRow::~CCUBRIDRowsetRow\n");
		FreeData();
	}

	DWORD AddRefRow() { return CComObjectThreadModel::Increment((LPLONG)&m_dwRef); } 
	DWORD ReleaseRow() { return CComObjectThreadModel::Decrement((LPLONG)&m_dwRef); }

	HRESULT Compare(CCUBRIDRowsetRow *pRow)
	{
		ATLASSERT(pRow);
		return ( m_iRowset==pRow->m_iRowset ? S_OK : S_FALSE );
	}

	//===== ReadData: 다른 곳의 데이터를 이 클래스 안으로 읽어들인다.
public:
	// hReq로 부터 데이터를 읽어들임
	HRESULT ReadData(int hReq, bool bOIDOnly=false, bool bSensitive=false);
	// pBinding에 따라 이루어진 pData로 부터 데이터를 읽어들임
	HRESULT ReadData(ATLBINDINGS *pBinding, void *pData);
	HRESULT ReadData(int hReq, char* szOID);

	//===== WriteData: 이 클래스의 데이터를 다른 곳에 복사한다.
public:
	// m_rgColumns의 데이터를 storage에 저장
	HRESULT WriteData(int hConn, int hReq, CComBSTR &strTableName);
	// m_rgColumns의 데이터를 pBinding에 따라 pData에 저장
	HRESULT WriteData(ATLBINDINGS *pBinding, void *pData, DBROWCOUNT dwBookmark, CCUBRIDRowset* pRowset = NULL);
	// m_rgColumns의 데이터를 rgColumns에 저장
	HRESULT WriteData(DBORDINAL cColumns, DBCOLUMNACCESS rgColumns[]);

	//===== Compare: 이 클래스의 데이터가 조건에 부합하는 지 검사
public:
	// 현재 row가 rBinding의 조건에 맞는지 검사
	HRESULT Compare(void *pFindData, DBCOMPAREOP CompareOp, DBBINDING &rBinding);
};
