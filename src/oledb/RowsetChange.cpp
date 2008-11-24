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
#include "Rowset.h"
#include "DataSource.h"

static HRESULT DoCommit(CCUBRIDRowset *pRowset)
{
	// CHECK_UPDATABILITY에서 걸러질 것이다.
	ATLASSERT(pRowset->m_eType==CCUBRIDRowset::FromCommand
			  || pRowset->m_eType==CCUBRIDRowset::FromSession);

	Util::ITxnCallback *pOwner = 0;
	if(pRowset->m_eType==CCUBRIDRowset::FromCommand)
		pOwner = pRowset->GetCommandPtr();
	else if(pRowset->m_eType==CCUBRIDRowset::FromSession)
		pOwner = pRowset;

	CCUBRIDSession *pSession = pRowset->GetSessionPtr();
	ATLASSERT(pSession);
	return pSession->AutoCommit(pOwner);
}

// 각 업데이트 방식의 허용 여부를 판단한다.
// Rowset 자체의 허용 여부도 판단한다.
#define CHECK_UPDATABILITY(prop)													\
	do																				\
	{																				\
		if(cci_is_updatable(GetRequestHandle())==0)									\
			return RaiseError(DB_SEC_E_PERMISSIONDENIED, 0, __uuidof(IRowsetChange));\
																					\
		{																			\
			CComVariant var;														\
			HRESULT hr = GetPropValue(&DBPROPSET_ROWSET, DBPROP_UPDATABILITY, &var);\
			if(!(V_I4(&var) & (prop)))												\
				return RaiseError(DB_E_NOTSUPPORTED, 0, __uuidof(IRowsetChange));	\
		}																			\
	} while(0)

static void MakeRowInvalid(CCUBRIDRowset *pRowset, CCUBRIDRowsetRow *pRow)
{
	// invalidate bookmark
	DBROWCOUNT dwBookmark = pRow->m_iRowset+3;//Util::FindBookmark(pRowset->m_rgBookmarks, (LONG)pRow->m_iRowset+1);
	pRowset->m_rgBookmarks[dwBookmark] = -1;

	// next fetch position 조정
	//if(pRow->m_status!=DBPENDINGSTATUS_NEW && pRowset->m_iRowset > (LONG)pRow->m_iRowset)
	//	pRowset->m_iRowset--;

	pRow->m_status = DBPENDINGSTATUS_INVALIDROW;
}

static bool IsDeferred(CCUBRIDRowset *pRowset)
{
	CComVariant var;
	HRESULT hr = pRowset->GetPropValue(&DBPROPSET_ROWSET, DBPROP_IRowsetUpdate, &var);
	ATLASSERT(SUCCEEDED(hr));
	return V_BOOL(&var)==ATL_VARIANT_TRUE;
}

STDMETHODIMP CCUBRIDRowset::DeleteRows(HCHAPTER hReserved, DBCOUNTITEM cRows,
				const HROW rghRows[], DBROWSTATUS rgRowStatus[])
{
	ATLTRACE(atlTraceDBProvider, 2, "CCUBRIDRowset::DeleteRows\n");

	ClearError();
	if(m_nStatus==1) return RaiseError(E_UNEXPECTED, 1, __uuidof(IRowsetChange), "This object is in a zombie state");

	CHECK_UPDATABILITY(DBPROPVAL_UP_DELETE);

	if(cRows==0) return S_OK;
	if(rghRows==NULL && cRows>=1) return RaiseError(E_INVALIDARG, 0, __uuidof(IRowsetChange));

	// Determine if we're in immediate or deferred mode
	bool bDeferred = IsDeferred(this);

	int hConn;
	{
		HRESULT hr = GetConnectionHandle(&hConn);
		if(FAILED(hr)) return RaiseError(E_FAIL, 0, __uuidof(IRowsetChange));
	}

	BOOL bSuccess = false;
	BOOL bFailed = false;
	for(DBCOUNTITEM i=0;i<cRows;i++)
	{
		HROW hRow = rghRows[i];

		// Attempt to locate the row in our map
		CCUBRIDRowsetRow *pRow;
		{
			bool bFound = m_rgRowHandles.Lookup((ULONG)hRow, pRow);
			if(!bFound || pRow==NULL)
			{	// invalid handle
				bFailed = true;
				if(rgRowStatus) rgRowStatus[i] = DBROWSTATUS_E_INVALID;
				continue;
			}
		}

		if(pRow->m_status==DBPENDINGSTATUS_DELETED)
		{	// already deleted
			if(rgRowStatus) rgRowStatus[i] = DBROWSTATUS_E_DELETED;
			bFailed  = true;
			continue;
		}

		ATLASSERT( pRow->m_iRowset==(ULONG)-1 || pRow->m_iRowset<m_rgRowData.GetCount() );

		DBROWSTATUS rowStat = DBROWSTATUS_S_OK;

		// mark the row as deleted
		if(pRow->m_status==DBPENDINGSTATUS_INVALIDROW)
		{
			bFailed = true;
			// unsigned high bit signified neg. number
			if(pRow->m_dwRef & 0x80000000)		
				rowStat = DBROWSTATUS_E_INVALID;
			else
				rowStat = DBROWSTATUS_E_DELETED;
		}
		else if(pRow->m_iRowset==(ULONG)-1 && pRow->m_status!=DBPENDINGSTATUS_NEW)
		{	// 새로 삽입되었고 Storage로 전송됐다.
			bFailed = true;
			rowStat = DBROWSTATUS_E_NEWLYINSERTED;
		}
		else
		{
			bSuccess = true;
			rowStat = DBROWSTATUS_S_OK;
			if(pRow->m_status==DBPENDINGSTATUS_NEW)
				MakeRowInvalid(this, pRow);
			else
				pRow->m_status = DBPENDINGSTATUS_DELETED;
		}

		if(!bDeferred && pRow->m_status==DBPENDINGSTATUS_DELETED)
		{	// 변화를 지금 적용
			// CCUBRIDRowsetRow의 delete는 ReleaseRows에서 이루어진다.
			HRESULT hr = pRow->WriteData(hConn, GetRequestHandle(), m_strTableName);
			if(FAILED(hr)) return hr;

			MakeRowInvalid(this, pRow);
		}

		if(rgRowStatus) rgRowStatus[i] = rowStat;
	}

	if(!bDeferred)
		DoCommit(this); // commit

	if(bFailed)
	{
		if(bSuccess)
			return DB_S_ERRORSOCCURRED;
		else
			return RaiseError(DB_E_ERRORSOCCURRED, 0, __uuidof(IRowsetChange));
	}
	else
		return S_OK;
}

STDMETHODIMP CCUBRIDRowset::SetData(HROW hRow, HACCESSOR hAccessor, void *pData)
{
	ATLTRACE(atlTraceDBProvider, 2, "CCUBRIDRowset::SetData\n");

	ClearError();
	if(m_nStatus==1) return RaiseError(E_UNEXPECTED, 1, __uuidof(IRowsetChange), "This object is in a zombie state");

	// cci_cursor_update를 이용하면 질의를 재실행할 때까지 SetData를 실행할 수 없다.
	// SQL and cci_execute를 이용하면 가능하다.
	CHECK_RESTART(__uuidof(IRowsetChange));

	CHECK_UPDATABILITY(DBPROPVAL_UP_CHANGE);

	// Determine if we're in immediate or deferred mode
	bool bDeferred = IsDeferred(this);

	// Attempt to locate the row in our map
	CCUBRIDRowsetRow *pRow;
	{
		bool bFound = m_rgRowHandles.Lookup((ULONG)hRow, pRow);
		if(!bFound || pRow==NULL)
			return RaiseError(DB_E_BADROWHANDLE, 0, __uuidof(IRowsetChange));
	}

	ATLASSERT( pRow->m_iRowset==(ULONG)-1 || pRow->m_iRowset<m_rgRowData.GetCount() );

	// 이미 지워진 row
	if(pRow->m_status==DBPENDINGSTATUS_DELETED || pRow->m_status==DBPENDINGSTATUS_INVALIDROW)
		return RaiseError(DB_E_DELETEDROW, 0, __uuidof(IRowsetChange));

	// 새로 삽입되었고 Storage로 전송됐다.
	if(pRow->m_iRowset==(ULONG)-1 && pRow->m_status!=DBPENDINGSTATUS_NEW)
		return DB_E_NEWLYINSERTED;

	// 바인딩 정보를 구함
	ATLBINDINGS *pBinding;
	{
		bool bFound = m_rgBindings.Lookup((ULONG)hAccessor, pBinding);
		if(!bFound || pBinding==NULL)
			return RaiseError(DB_E_BADACCESSORHANDLE, 0, __uuidof(IRowsetChange));
		if(!(pBinding->dwAccessorFlags & DBACCESSOR_ROWDATA))
			return RaiseError(DB_E_BADACCESSORTYPE, 0, __uuidof(IRowsetChange)); // row accessor 가 아니다.
		if(pData==NULL && pBinding->cBindings!=0)
			return RaiseError(E_INVALIDARG, 0, __uuidof(IRowsetChange));
	}

	HRESULT hr = pRow->ReadData(pBinding, pData);
	if(FAILED(hr)) return hr;
	// 새로 삽입된 row는 변경이 있어도 그냥 새로 삽입된 것으로 표시
	if(pRow->m_status!=DBPENDINGSTATUS_NEW)
		pRow->m_status = DBPENDINGSTATUS_CHANGED;

	if(!bDeferred)
	{	// 변화를 지금 적용
		int hConn;
		HRESULT hr = GetConnectionHandle(&hConn);
		if(FAILED(hr)) return RaiseError(hr, 0, __uuidof(IRowsetChange));

		hr = pRow->WriteData(hConn, GetRequestHandle(), m_strTableName);

		if (hr == DB_E_INTEGRITYVIOLATION)
			return RaiseError(DB_E_INTEGRITYVIOLATION, 0, __uuidof(IRowsetChange));

		if(FAILED(hr)) return RaiseError(DB_E_ERRORSOCCURRED, 0, __uuidof(IRowsetChange));
		
		pRow->m_status = 0; // or UNCHANGED?
		DoCommit(this); // commit
	}

	return hr; // S_OK or DB_S_ERRORSOCCURRED
}

STDMETHODIMP CCUBRIDRowset::InsertRow(HCHAPTER hReserved, HACCESSOR hAccessor, void *pData, HROW *phRow)
{
	ATLTRACE(atlTraceDBProvider, 2, "CCUBRIDRowset::InsertRow\n");

	if(phRow) *phRow = NULL;

	if(phRow)
		CHECK_CANHOLDROWS(__uuidof(IRowsetChange));

	ClearError();
	if(m_nStatus==1) return RaiseError(E_UNEXPECTED, 1, __uuidof(IRowsetChange), "This object is in a zombie state");

	CHECK_UPDATABILITY(DBPROPVAL_UP_INSERT);

	// Determine if we're in immediate or deferred mode
	bool bDeferred = IsDeferred(this);

	// 바인딩 정보를 구함
	ATLBINDINGS *pBinding;
	{
		bool bFound = m_rgBindings.Lookup((ULONG)hAccessor, pBinding);
		if(!bFound || pBinding==NULL)
			return RaiseError(DB_E_BADACCESSORHANDLE, 0, __uuidof(IRowsetChange));
		if(!(pBinding->dwAccessorFlags & DBACCESSOR_ROWDATA))
			return RaiseError(DB_E_BADACCESSORTYPE, 0, __uuidof(IRowsetChange)); // row accessor 가 아니다.
		if(pData==NULL && pBinding->cBindings!=0)
			return RaiseError(E_INVALIDARG, 0, __uuidof(IRowsetChange));
	}

	CCUBRIDRowsetRow *pRow;
	CCUBRIDRowsetRow::KeyType key;
	{	// 새 row를 위한 Row class를 생성한다.
		DBORDINAL cCols;
		ATLCOLUMNINFO *pColInfo = GetColumnInfo(this, &cCols);
		ATLTRY(pRow = new CCUBRIDRowsetRow(-1, cCols, pColInfo, m_spConvert, m_Columns.m_defaultVal));
		if(pRow==NULL) return RaiseError(E_OUTOFMEMORY, 0, __uuidof(IRowsetChange));

		// row handle에 추가
		key = (CCUBRIDRowsetRow::KeyType)m_rgRowData.GetCount();
		if (key == 0) key++;
		
		while(m_rgRowHandles.Lookup(key)) key++;
		m_rgRowHandles.SetAt(key, pRow);

		// rowset에 데이터가 하나 늘었다.
//		m_rgRowData.SetCount(m_rgRowData.GetCount()+1);
	}

	HRESULT hrRead = pRow->ReadData(pBinding, pData);
	if(FAILED(hrRead))
	{
//		m_rgRowData.SetCount(m_rgRowData.GetCount()-1);
		m_rgRowHandles.RemoveKey(key);
		delete pRow;
		return hrRead;
	}
	pRow->m_status = DBPENDINGSTATUS_NEW;

	if(phRow)
	{	// handle을 반환할 때는 참조 카운트를 하나 증가시킨다.
		pRow->AddRefRow();
		*phRow = (HROW)key;
	}

//	m_rgBookmarks.Add(pRow->m_iRowset+1);

	if(!bDeferred)
	{	// 변화를 지금 적용
		int hConn;
		HRESULT hr = GetConnectionHandle(&hConn);
		if(FAILED(hr)) return E_FAIL;

		hr = pRow->WriteData(hConn, GetRequestHandle(), m_strTableName);
		if(FAILED(hr))
		{
			pRow->ReleaseRow();
			if (phRow != NULL)
				*phRow = NULL;
			return hr;
		}

		pRow->m_status = 0; // or UNCHANGED?
		if(pRow->m_dwRef==0)
		{
			m_rgRowHandles.RemoveKey(key);
			delete pRow;
		}

		DoCommit(this); // commit

		//// deferred update 모드였다가 immediate update 모드로 바뀌지는 않으므로
		//// 다른 NEW 상태의 row는 없다. 즉 m_iRowset을 변경해야 할 row는 없다.

		//// 새로 추가된 Row의 OID를 읽어들인다.
		//pRow->ReadData(GetRequestHandle(), true);
	}

	return hrRead;
}

STDMETHODIMP CCUBRIDRowset::GetOriginalData(HROW hRow, HACCESSOR hAccessor, void *pData)
{
	ATLTRACE(atlTraceDBProvider, 2, _T("CCUBRIDRowset::GetOriginalData\n"));

	ClearError();
	if(m_nStatus==1) return RaiseError(E_UNEXPECTED, 1, __uuidof(IRowsetUpdate), "This object is in a zombie state");
	CHECK_RESTART(__uuidof(IRowsetUpdate));

	// 바인딩 정보를 구함
	ATLBINDINGS *pBinding;
	{
		bool bFound = m_rgBindings.Lookup((ULONG)hAccessor, pBinding);
		if(!bFound || pBinding==NULL)
			return DB_E_BADACCESSORHANDLE;
		if(!(pBinding->dwAccessorFlags & DBACCESSOR_ROWDATA))
			return DB_E_BADACCESSORTYPE; // row accessor 가 아니다.
		if(pData==NULL && pBinding->cBindings!=0)
			return E_INVALIDARG;
	}

	// Attempt to locate the row in our map
	CCUBRIDRowsetRow *pRow;
	{
		bool bFound = m_rgRowHandles.Lookup((ULONG)hRow, pRow);
		if(!bFound || pRow==NULL)
			return DB_E_BADROWHANDLE;
	}

	// If the status is DBPENDINGSTATUS_INVALIDROW, the row has been
	// deleted and the change transmitted to the data source.  In 
	// this case, we can't get the original data so return
	// DB_E_DELETEDROW.
	if (pRow->m_status == DBPENDINGSTATUS_INVALIDROW)
		return DB_E_DELETEDROW;

	// Determine if we have a pending insert. In this case, the
	// spec says revert to default values, and if defaults,
	// are not available, then NULLs.

	if (pRow->m_status == DBPENDINGSTATUS_NEW)
	{
		for (ULONG lBind=0; lBind<pBinding->cBindings; lBind++)
		{
			DBBINDING* pBindCur = &(pBinding->pBindings[lBind]);

			// default value가 없으므로 NULL로 set
			if (pBindCur->dwPart & DBPART_VALUE)
				*((BYTE *)pData+pBindCur->obValue) = NULL;
			if (pBindCur->dwPart & DBPART_STATUS)
				*((DBSTATUS*)((BYTE*)(pData) + pBindCur->obStatus)) = DBSTATUS_S_ISNULL;
		}

		return S_OK;
	}

	// 새로 삽입된 Row
	if(pRow->m_iRowset==(ULONG)-1)
	{
		return pRow->WriteData(pBinding, pData, 0);
	}

	DBORDINAL cCols;
	ATLCOLUMNINFO *pInfo = GetColumnInfo(this, &cCols);
	DBROWCOUNT dwBookmark = pRow->m_iRowset+3;//Util::FindBookmark(m_rgBookmarks, (LONG)pRow->m_iRowset+1);

	// 임시 RowClass를 통해 storage에서 데이터를 읽어온 후, pData로 전송한다.
	CCUBRIDRowsetRow OrigRow(pRow->m_iRowset, cCols, pInfo, m_spConvert, m_Columns.m_defaultVal);
	OrigRow.ReadData(GetRequestHandle());
	return OrigRow.WriteData(pBinding, pData, dwBookmark);
}

STDMETHODIMP CCUBRIDRowset::GetPendingRows(HCHAPTER hReserved, DBPENDINGSTATUS dwRowStatus,
				DBCOUNTITEM *pcPendingRows, HROW **prgPendingRows,
				DBPENDINGSTATUS **prgPendingStatus)
{
	ATLTRACE(atlTraceDBProvider, 2, "CCUBRIDRowset::GetPendingRows\n");

	ClearError();
	if(m_nStatus==1) return RaiseError(E_UNEXPECTED, 1, __uuidof(IRowsetUpdate), "This object is in a zombie state");

	bool bPending = false;
	CCUBRIDRowsetRow *pRow = NULL;

	if(pcPendingRows)
	{
		*pcPendingRows = 0;
		if(prgPendingRows) *prgPendingRows = NULL;
		if(prgPendingStatus) *prgPendingStatus = NULL;
	}

	// Validate input parameters
	if ((dwRowStatus & 
		~(DBPENDINGSTATUS_NEW | DBPENDINGSTATUS_CHANGED | DBPENDINGSTATUS_DELETED)) != 0)
		return E_INVALIDARG;

	// Determine how many rows we'll need to return

	POSITION pos = m_rgRowHandles.GetStartPosition();
	while( pos != NULL )
	{
		MapClass::CPair* pPair = m_rgRowHandles.GetNext( pos );
		ATLASSERT( pPair != NULL );

		// Check to see if a row has a pending status
		pRow = pPair->m_value;

		if (pRow->m_status & dwRowStatus)
		{
			if (pcPendingRows != NULL)
				(*pcPendingRows)++;
			bPending = true;
		}
	}

	// In this case, there are no pending rows that match, just exit out
	if (!bPending)
	{
		// There are no pending rows so exit immediately
		return S_FALSE;
	}
	else
	{
		// Here' the consumer just wants to see if there are pending rows
		// we know that so we can exit
		if (pcPendingRows == NULL)
			return S_OK;
	}

	// Allocate arrays for pending rows
	{
		if (prgPendingRows != NULL)
		{
			*prgPendingRows = (HROW*)CoTaskMemAlloc(*pcPendingRows * sizeof(HROW));
			if (*prgPendingRows == NULL)
			{
				*pcPendingRows = 0;
				return E_OUTOFMEMORY;
			}
		}

		if (prgPendingStatus != NULL)
		{
			*prgPendingStatus = (DBPENDINGSTATUS*)CoTaskMemAlloc(*pcPendingRows * sizeof(DBPENDINGSTATUS));
			if (*prgPendingStatus == NULL)
			{
				*pcPendingRows = 0;
				CoTaskMemFree(*prgPendingRows);
				*prgPendingRows = NULL;
				return E_OUTOFMEMORY;
			}
			memset(*prgPendingStatus, 0, *pcPendingRows * sizeof(DBPENDINGSTATUS));
		}
	}

	if (prgPendingRows || prgPendingStatus)
	{
		ULONG ulRows = 0;
		pos = m_rgRowHandles.GetStartPosition();
		while( pos != NULL )
		{
			MapClass::CPair* pPair = m_rgRowHandles.GetNext( pos );
			ATLASSERT( pPair != NULL );

			pRow = pPair->m_value;
			if (pRow->m_status & dwRowStatus)
			{
				// Add the output row
				pRow->AddRefRow();
				if (prgPendingRows)
					((*prgPendingRows)[ulRows]) = /*(HROW)*/pPair->m_key;
				if (prgPendingStatus)
					((*prgPendingStatus)[ulRows]) = (DBPENDINGSTATUS)pRow->m_status;
				ulRows++;
			}
		}
		if (pcPendingRows != NULL)
			*pcPendingRows = ulRows;
	}

	// Return code depending on
	return S_OK;
}

STDMETHODIMP CCUBRIDRowset::GetRowStatus(HCHAPTER hReserved, DBCOUNTITEM cRows,
				const HROW rghRows[], DBPENDINGSTATUS rgPendingStatus[])
{
	ATLTRACE(atlTraceDBProvider, 2, _T("CCUBRIDRowset::GetRowStatus\n"));

	ClearError();
	if(m_nStatus==1) return RaiseError(E_UNEXPECTED, 1, __uuidof(IRowsetUpdate), "This object is in a zombie state");

	bool bSucceeded = true;
	ULONG ulFetched = 0;

	if(cRows)
	{
		// check for correct pointers
		if(rghRows==NULL || rgPendingStatus==NULL) return E_INVALIDARG;

		for (ULONG ulRows=0; ulRows < cRows; ulRows++)
		{
			CCUBRIDRowsetRow* pRow;
			bool bFound = m_rgRowHandles.Lookup((ULONG)rghRows[ulRows], pRow);
			if ((! bFound || pRow == NULL) || (pRow->m_status == DBPENDINGSTATUS_INVALIDROW))
			{
				rgPendingStatus[ulRows] = DBPENDINGSTATUS_INVALIDROW;
				bSucceeded = false;
				continue;
			}
			if (pRow->m_status != 0)
				rgPendingStatus[ulRows] = pRow->m_status;
			else
				rgPendingStatus[ulRows] = DBPENDINGSTATUS_UNCHANGED;

			ulFetched++;
		}
	}

	if (bSucceeded)
	{
		return S_OK;
	}
	else
	{
		if (ulFetched > 0)
			return DB_S_ERRORSOCCURRED;
		else
			return DB_E_ERRORSOCCURRED;
	}
}

STDMETHODIMP CCUBRIDRowset::Undo(HCHAPTER hReserved, DBCOUNTITEM cRows, const HROW rghRows[],
				DBCOUNTITEM *pcRowsUndone, HROW **prgRowsUndone, DBROWSTATUS **prgRowStatus)
{
	ATLTRACE(atlTraceDBProvider, 2, "CCUBRIDRowset::Undo\n");

	ClearError();
	if(m_nStatus==1) return RaiseError(E_UNEXPECTED, 1, __uuidof(IRowsetUpdate), "This object is in a zombie state");
	CHECK_RESTART(__uuidof(IRowsetUpdate));

	DBCOUNTITEM ulRows = 0; // undo할 row 수
	bool bNotIgnore = true; // prgRowsUndone, prgRowStatus를 무시할지 여부를 나타냄

	// the following lines are used to fix the two _alloca calls below.  Those calls are risky 
	// because we may be allocating huge amounts of data.  So instead I'll allocate that data on heap.
	// But if you use _alloca you don't have to worry about cleaning this memory.  So we will use these
	// temporary variables to allocate memory on heap.  As soon as we exit the function, the memory will
	// be cleaned up, just as if we were using alloca. So now, instead of calling alloca, I'll alloc
	// memory on heap using the two smnart pointers below, and then assing it to the actual pointers.
	CHeapPtr<HROW> spTempRowsUndone;
	CHeapPtr<DBROWSTATUS> spTempRowStatus;

	if(cRows || pcRowsUndone)
	{
		if(prgRowsUndone) *prgRowsUndone = NULL;
		if(prgRowStatus) *prgRowStatus = NULL;		
	}
	else
	{
		bNotIgnore = false;		// Don't do status or row arrays
	}

	// Check to see how many changes we'll undo 
	if(pcRowsUndone)
	{
		*pcRowsUndone = NULL;
		if(prgRowsUndone==NULL) return E_INVALIDARG;
	}

	if(cRows)
	{
		if(rghRows==NULL) return E_INVALIDARG;
		ulRows = cRows;
	}
	else
		ulRows = (DBCOUNTITEM)m_rgRowHandles.GetCount();

	// NULL out pointers
	{
		if(prgRowsUndone && ulRows && bNotIgnore)
		{
			// Make a temporary buffer as we may not fill up everything
			// in the case where cRows == 0
			if(cRows)
				*prgRowsUndone = (HROW*)CoTaskMemAlloc(ulRows * sizeof(HROW));
			else
			{
				spTempRowsUndone.Allocate(ulRows);
				*prgRowsUndone = spTempRowsUndone;
			}
			if (*prgRowsUndone==NULL) return E_OUTOFMEMORY;
		}

		if(prgRowStatus && ulRows && bNotIgnore)
		{
			if(cRows)
				*prgRowStatus = (DBROWSTATUS*)CoTaskMemAlloc(ulRows * sizeof(DBROWSTATUS));
			else
			{
				spTempRowStatus.Allocate(ulRows);
				*prgRowStatus = spTempRowStatus;
			}
			if(*prgRowStatus==NULL)
			{
				if(cRows) CoTaskMemFree(*prgRowsUndone);
				*prgRowsUndone = NULL;
				return E_OUTOFMEMORY;
			}
		}
	}

	bool bSucceeded = false;
	bool bFailed = false;
	ULONG ulUndone = 0; // undo된 row 수
	POSITION pos = m_rgRowHandles.GetStartPosition();
	for (ULONG ulUndoRow = 0; ulUndoRow < ulRows; ulUndoRow++)
	{
		ULONG ulCurrentRow = ulUndone;

		HROW hRowUndo = NULL; // 현재 undo할 row의 handle
		{
			if(cRows)
			{	// row handle이 주어졌음
				hRowUndo = rghRows[ulUndoRow];
			}
			else
			{	// 모든 row에 대해 undo
				// ATLASSERT(ulUndoRow < (ULONG)m_rgRowHandles.GetCount()); // delete된 row가 있으면 성립하지 않는다.
				ATLASSERT( pos != NULL );
				MapClass::CPair* pPair = m_rgRowHandles.GetNext(pos);
				ATLASSERT( pPair != NULL );
				hRowUndo = pPair->m_key;
			}
		}

		if(prgRowsUndone && bNotIgnore)
			(*prgRowsUndone)[ulCurrentRow] = hRowUndo;

		// Fetch the RowClass and determine if it is valid
		CCUBRIDRowsetRow *pRow;
		{
			bool bFound = m_rgRowHandles.Lookup((ULONG)hRowUndo, pRow);
			if (!bFound || pRow == NULL)
			{
				if (prgRowStatus && bNotIgnore)
					(*prgRowStatus)[ulCurrentRow] = DBROWSTATUS_E_INVALID;
				bFailed = true;
				ulUndone++;
				continue;
			}
		}

		// If cRows is zero we'll go through all rows fetched.  We shouldn't
		// increment the count for rows that haven't been modified.
		if (cRows != 0 || (pRow != NULL &&
			pRow->m_status != 0 && pRow->m_status != DBPENDINGSTATUS_UNCHANGED
			&& pRow->m_status != DBPENDINGSTATUS_INVALIDROW))
			ulUndone++;
		else
			continue;

		if(cRows==0)
			pRow->AddRefRow();

		switch (pRow->m_status)
		{
		case DBPENDINGSTATUS_INVALIDROW:
			// 메모리와 storage 모두 존재하지 않는 row
			{
				// provider templates에서는 DELETED인데
				// INVALID가 더 맞지 않을까 싶기도 한다.
				if(prgRowStatus && bNotIgnore)
					(*prgRowStatus)[ulCurrentRow] = DBROWSTATUS_E_DELETED;
				bFailed = true;
			}
			break;
		case DBPENDINGSTATUS_NEW:
			// 메모리 상에만 존재하는 row
			{
				// If the row is newly inserted, go ahead and mark its
				// row as INVALID (according to the specification).

				if(prgRowStatus && bNotIgnore)
					(*prgRowStatus)[ulCurrentRow] = DBROWSTATUS_S_OK;

				MakeRowInvalid(this, pRow);

				bSucceeded = true;
			}
			break;
		case DBPENDINGSTATUS_CHANGED:
		case DBPENDINGSTATUS_DELETED:
			// storage의 데이터를 가져와야 하는 경우
			// delete 된 경우 메모리에 데이터가 있지만, CHANGED->DELETED 인 경우도 있을 수 있다.
			{
				// read data back
				pRow->ReadData(GetRequestHandle());
				pRow->m_status = DBPENDINGSTATUS_UNCHANGED;

				if(prgRowStatus && bNotIgnore)
					(*prgRowStatus)[ulCurrentRow] = DBROWSTATUS_S_OK;
				bSucceeded = true;
			}
			break;
		default: // 0, DBPENDINGSTATUS_UNCHANGED, 
			// storage의 데이터를 가져올 필요가 없는 경우
			{
				pRow->m_status = DBPENDINGSTATUS_UNCHANGED;

				if(prgRowStatus && bNotIgnore)
					(*prgRowStatus)[ulCurrentRow] = DBROWSTATUS_S_OK;
				bSucceeded = true;
			}
			break;
		}

		// Check if we need to release the row because it's ref was 0
		// See the IRowset::ReleaseRows section in the spec for more
		// information
		if (pRow->m_dwRef == 0)
		{
			pRow->AddRefRow();	// Artifically bump this to remove it
			if( FAILED( RefRows(1, &hRowUndo, NULL, NULL, false) ) )
				return E_FAIL;
		}
	}

	// Set the output for rows undone.
	if(pcRowsUndone) *pcRowsUndone = ulUndone;

	if(ulUndone==0)
	{
		if(prgRowsUndone)
		{
			CoTaskMemFree(*prgRowsUndone);
			*prgRowsUndone = NULL;
		}

		if(prgRowStatus)
		{
			CoTaskMemFree(*prgRowStatus);
			*prgRowStatus = NULL;
		}
	}
	else if(cRows==0)
	{
		// In the case where cRows == 0, we need to allocate the final
		// array of data.
		if(prgRowsUndone && bNotIgnore)
		{
			HROW *prgRowsTemp = (HROW *)CoTaskMemAlloc(ulUndone*sizeof(HROW));
			if(prgRowsTemp==NULL) return E_OUTOFMEMORY;
			memcpy(prgRowsTemp, *prgRowsUndone, ulUndone*sizeof(HROW));
			*prgRowsUndone = prgRowsTemp;
		}

		if(prgRowStatus && bNotIgnore)
		{
			HROW *prgRowStatusTemp = (DBROWSTATUS *)CoTaskMemAlloc(ulUndone*sizeof(DBROWSTATUS));
			if(prgRowStatusTemp==NULL)
			{
				CoTaskMemFree(*prgRowsUndone);
				*prgRowsUndone = NULL;
				return E_OUTOFMEMORY;
			}
			memcpy(prgRowStatusTemp, *prgRowStatus, ulUndone*sizeof(DBROWSTATUS));
			*prgRowStatus = prgRowStatusTemp;
		}
	}

	// Send the return value
	if(!bFailed)
		return S_OK;
	else
	{
		return bSucceeded ? DB_S_ERRORSOCCURRED : DB_E_ERRORSOCCURRED;
	}
}

STDMETHODIMP CCUBRIDRowset::Update(HCHAPTER hReserved, DBCOUNTITEM cRows, const HROW rghRows[],
				DBCOUNTITEM *pcRows, HROW **prgRows, DBROWSTATUS **prgRowStatus)
{
	ATLTRACE(atlTraceDBProvider, 2, "CCUBRIDRowset::Update\n");

	ClearError();
	if(m_nStatus==1) return RaiseError(E_UNEXPECTED, 1, __uuidof(IRowsetUpdate), "This object is in a zombie state");
	CHECK_RESTART(__uuidof(IRowsetUpdate));

	DBCOUNTITEM ulRows = 0; // update할 row 수
	bool bNotIgnore = true; // prgRows, prgRowStatus를 무시할지 여부를 나타냄

	// the following lines are used to fix the two _alloca calls below.  Those calls are risky 
	// because we may be allocating huge amounts of data.  So instead I'll allocate that data on heap.
	// But if you use _alloca you don't have to worry about cleaning this memory.  So we will use these
	// temporary variables to allocate memory on heap.  As soon as we exit the function, the memory will
	// be cleaned up, just as if we were using alloca. So now, instead of calling alloca, I'll alloc
	// memory on heap using the two smnart pointers below, and then assing it to the actual pointers.
	CHeapPtr<HROW> spTempRows;
	CHeapPtr<DBROWSTATUS> spTempRowStatus;

	if(cRows || pcRows)
	{
		if(prgRows) *prgRows = NULL;
		if(prgRowStatus) *prgRowStatus = NULL;		
	}
	else
	{
		bNotIgnore = false;		// Don't do status or row arrays
	}

	// Check to see how many changes we'll undo 
	if(pcRows)
	{
		*pcRows = NULL;
		if(prgRows==NULL) return E_INVALIDARG;
	}

	if(cRows)
	{
		if(rghRows==NULL) return E_INVALIDARG;
		ulRows = cRows;
	}
	else
		ulRows = (DBCOUNTITEM)m_rgRowHandles.GetCount();

	int hConn;
	HRESULT hr = GetConnectionHandle(&hConn);
	if(FAILED(hr)) return E_FAIL;

	// NULL out pointers
	{
		if(prgRows && ulRows && bNotIgnore)
		{
			// Make a temporary buffer as we may not fill up everything
			// in the case where cRows == 0
			if(cRows)
				*prgRows = (HROW*)CoTaskMemAlloc(ulRows * sizeof(HROW));
			else
			{
				spTempRows.Allocate(ulRows);
				*prgRows = spTempRows;
			}
			if (*prgRows==NULL) return E_OUTOFMEMORY;
		}

		if(prgRowStatus && ulRows && bNotIgnore)
		{
			if(cRows)
				*prgRowStatus = (DBROWSTATUS*)CoTaskMemAlloc(ulRows * sizeof(DBROWSTATUS));
			else
			{
				spTempRowStatus.Allocate(ulRows);
				*prgRowStatus = spTempRowStatus;
			}
			if(*prgRowStatus==NULL)
			{
				if(cRows) CoTaskMemFree(*prgRows);
				*prgRows = NULL;
				return E_OUTOFMEMORY;
			}
		}
	}

	bool bSucceeded = false;
	bool bFailed = false;
	ULONG ulCount = 0; // update된 row 수
	POSITION pos = m_rgRowHandles.GetStartPosition();
	for (ULONG ulRow = 0; ulRow < ulRows; ulRow++)
	{
		ULONG ulCurrentRow = ulCount;
		bool bDupRow = false; // 중복된 row
		ULONG ulAlreadyProcessed = 0; // 중복된 row의 handle의 위치

		HROW hRowUpdate = NULL; // 현재 update할 row의 handle
		{
			if(cRows)
			{	// row handle이 주어졌음
				hRowUpdate = rghRows[ulRow];
				for (ULONG ulCheckDup = 0; ulCheckDup < ulRow; ulCheckDup++)
				{
					if (hRowUpdate==rghRows[ulCheckDup] ||
						IsSameRow(hRowUpdate, rghRows[ulCheckDup]) == S_OK)
					{
						ulAlreadyProcessed = ulCheckDup;
						bDupRow = true;
						break;
					}
				}
			}
			else
			{	// 모든 row에 대해 update
				//ATLASSERT(ulRow < (ULONG)m_rgRowHandles.GetCount()); // delete된 row가 있으면 성립하지 않는다.
				ATLASSERT( pos != NULL );
				MapClass::CPair* pPair = m_rgRowHandles.GetNext(pos);
				ATLASSERT( pPair != NULL );
				hRowUpdate = pPair->m_key;
			}
		}

		if(prgRows && bNotIgnore)
			(*prgRows)[ulCurrentRow] = hRowUpdate;

		if(bDupRow)
		{
			// We've already set the row before, just copy status and
			// continue processing
			if(prgRowStatus && bNotIgnore)
				(*prgRowStatus)[ulCurrentRow] = (*prgRowStatus)[ulAlreadyProcessed];
			ulCount++;
			continue;
		}

		// Fetch the RowClass and determine if it is valid
		CCUBRIDRowsetRow *pRow;
		{
			bool bFound = m_rgRowHandles.Lookup((ULONG)hRowUpdate, pRow);
			if (!bFound || pRow == NULL)
			{
				if (prgRowStatus && bNotIgnore)
					(*prgRowStatus)[ulCurrentRow] = DBROWSTATUS_E_INVALID;
				bFailed = true;
				ulCount++;
				continue;
			}
		}

		// If cRows is zero we'll go through all rows fetched.  We
		// shouldn't increment the attempted count for rows that are
		// not changed
		if (cRows != 0 || (pRow != NULL &&
			pRow->m_status != 0 && pRow->m_status != DBPENDINGSTATUS_UNCHANGED
			&& pRow->m_status != DBPENDINGSTATUS_INVALIDROW))
			ulCount++;
		else
			continue;

		if(cRows==0)
			pRow->AddRefRow();

		switch (pRow->m_status)
		{
		case DBPENDINGSTATUS_INVALIDROW:	// Row is bad or deleted
			{
				if(prgRowStatus && bNotIgnore)
					(*prgRowStatus)[ulCurrentRow] = DBROWSTATUS_E_DELETED;
				bFailed = true;
			}
			break;
		case DBPENDINGSTATUS_UNCHANGED:
		case 0:
			{
				// If the row's status is not changed, then just put S_OK
				// and continue.  The spec says we should not transmit the
				// request to the data source (as nothing would change).
				if(prgRowStatus && bNotIgnore)
					(*prgRowStatus)[ulCurrentRow] = DBROWSTATUS_S_OK;
				bSucceeded = true;
			}
			break;
		default:
			{
				DBORDINAL cCols;
				ATLCOLUMNINFO *pColInfo = GetColumnInfo(this, &cCols);
				HRESULT hr = pRow->WriteData(hConn, GetRequestHandle(), m_strTableName);
				if(FAILED(hr))
				{
					DBROWSTATUS stat = DBROWSTATUS_E_FAIL;
					if(hr==DB_E_INTEGRITYVIOLATION) stat = DBROWSTATUS_E_INTEGRITYVIOLATION;
					if(prgRowStatus && bNotIgnore)
						(*prgRowStatus)[ulCurrentRow] = stat;
					bFailed = true;
				}
				else
				{
					//// m_iRowset을 적당히 조정한다.
					//if(pRow->m_status==DBPENDINGSTATUS_NEW)
					//{
					//	// NEW인 row는 항상 rowset의 뒤에 몰려있다.
					//	// 그 row 중 가장 작은 m_iRowset이 update 된 row의 m_iRowset이 되면 된다.
					//	CCUBRIDRowsetRow::KeyType key = pRow->m_iRowset;
					//	POSITION pos = m_rgRowHandles.GetStartPosition();
					//	while(pos)
					//	{
					//		CCUBRIDRowset::MapClass::CPair *pPair = m_rgRowHandles.GetNext(pos);
					//		ATLASSERT(pPair);
					//		CCUBRIDRowsetRow *pCheckRow = pPair->m_value;
					//		if( pCheckRow && pCheckRow->m_iRowset < key )
					//		{
					//			if(pCheckRow->m_iRowset<pRow->m_iRowset)
					//				pRow->m_iRowset = pCheckRow->m_iRowset;
					//			pCheckRow->m_iRowset++;
					//		}
					//	}

					//	// TODO: 북마크 업데이트가 필요한데 어떻게 해야 할지 모르겠다.

					//	// 새로 추가된 Row의 OID를 읽어들인다.
					//	pRow->ReadData(GetRequestHandle(), true);
					//}

					if(pRow->m_status==DBPENDINGSTATUS_DELETED)
						MakeRowInvalid(this, pRow);
					else
						pRow->m_status = DBPENDINGSTATUS_UNCHANGED;

					if(prgRowStatus && bNotIgnore)
						(*prgRowStatus)[ulCurrentRow] = DBROWSTATUS_S_OK;
					bSucceeded = true;

					// Check if we need to release the row because it's ref was 0
					// See the IRowset::ReleaseRows section in the spec for more
					// information
					if (pRow->m_dwRef == 0)
					{
						pRow->AddRefRow();	// Artifically bump this to remove it
						if( FAILED( RefRows(1, &hRowUpdate, NULL, NULL, false) ) )
							return E_FAIL;
					}
				}
			}
			break;
		}
	}

	// Set the output for rows undone.
	if(pcRows) *pcRows = ulCount;

	if(ulCount==0)
	{
		if(prgRows)
		{
			CoTaskMemFree(*prgRows);
			*prgRows = NULL;
		}

		if(prgRowStatus)
		{
			CoTaskMemFree(*prgRowStatus);
			*prgRowStatus = NULL;
		}
	}
	else if(cRows==0)
	{
		// In the case where cRows == 0, we need to allocate the final
		// array of data.
		if(prgRows && bNotIgnore)
		{
			HROW *prgRowsTemp = (HROW *)CoTaskMemAlloc(ulCount*sizeof(HROW));
			if(prgRowsTemp==NULL) return E_OUTOFMEMORY;
			memcpy(prgRowsTemp, *prgRows, ulCount*sizeof(HROW));
			*prgRows = prgRowsTemp;
		}

		if(prgRowStatus && bNotIgnore)
		{
			HROW *prgRowStatusTemp = (DBROWSTATUS *)CoTaskMemAlloc(ulCount*sizeof(DBROWSTATUS));
			if(prgRowStatusTemp==NULL)
			{
				CoTaskMemFree(*prgRows);
				*prgRows = NULL;
				return E_OUTOFMEMORY;
			}
			memcpy(prgRowStatusTemp, *prgRowStatus, ulCount*sizeof(DBROWSTATUS));
			*prgRowStatus = prgRowStatusTemp;
		}
	}

	DoCommit(this); // commit

	// Send the return value
	if(!bFailed)
		return S_OK;
	else
	{
		return bSucceeded ? DB_S_ERRORSOCCURRED : DB_E_ERRORSOCCURRED;
	}
}
