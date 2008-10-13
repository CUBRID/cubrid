// Rowset.h : Declaration of the CCUBRIDRowset

#pragma once

#include "resource.h"       // main symbols
#include "command.h"
#include "util.h"
#include "ColumnsRowset.h" // IColumnsRowset 구현
#include "RowsetRow.h" // RowClass를 정의한다.

class CCUBRIDDataSource;
class CCUBRIDSession;
class CCUBRIDCommand;

/*
 * CRowsetImpl의 원래 구조에서는
 * IOpenRowset::OpenRowset(CCUBRIDSession) 혹은 ICommand::Execute(CCUBRIDCommand)에서
 * CreateRowset을 호출하고 그 안에서 CCUBRIDRowset::Execute이 불려진다. 그러면,
 * CCUBRIDRowset::Execute에서는 Storage Type의 배열인 m_rgRowData에 모든 데이터를 넣는다.
 * 그러면 Accessor를 생성하고 데이터를 Consumer에게 넘기는 등의 일은 CRowsetImpl이
 * 이 데이터를 보고 자동으로 한다.
 * Schema Rowset의 구현에서는 이 구조를 그대로 사용한다.
 *
 * Rowset의 경우에는 Schema Rowset과 같이 고정된 Storage가 없고
 * 구현을 한다고 해도 모든 데이터를 메모리에 전부 넣을 수 없는 경우도 있기 때문에
 * 그대로 사용할 수 없다(Schema Rowset은 데이터 양이 많은 경우가 없다고 생각한다).
 * 그래서 다음과 같이 변형해서 구현했다.
 *
 * Stroage는 CDummy로 쓰이지 않는다.
 * Array of Storage(m_rgRowData)도 단순히 데이터의 개수를 위해서만 존재한다.
 * 다음 함수에서 사용된다(나머지는 m_rgRowData를 사용하지 않도록 재구현)
 *		IRowsetImpl::GetNextRowsSkipDeleted
 *		IRowsetImpl::GetNextRows
 *		IRowsetLocateImpl::GetRowsAt
 * 실제 데이터는 GetNextRows시 가져와서 RowClass에 저장하고
 * ReleaseRows에서 없어진다.
 */

// 값을 반환하고 다시 체크하는 것을 피하기 위해
// 클래스의 멤버가 아닌 매크로로 만들었다.
// TODO: IRowsetImpl::RestartPosition에서는 row handle의 상태도
// 검사해 CHANGED일 때만 반환하는데 어느쪽이 맞는 건지 모르겠다.
#define CHECK_CANHOLDROWS(iid)														\
	do																				\
	{																				\
		CComVariant varHoldRows;													\
		GetPropValue(&DBPROPSET_ROWSET, DBPROP_CANHOLDROWS, &varHoldRows);			\
		if(V_BOOL(&varHoldRows)==ATL_VARIANT_FALSE && m_rgRowHandles.GetCount()>0)	\
			return RaiseError(DB_E_ROWSNOTRELEASED, 0, iid);						\
	} while(0)

#define CHECK_RESTART(iid)															\
	do																				\
	{	/* 에러 발생 or 질의 재실행 */												\
		/*if(m_nStatus==2) Reexecute();*/											\
		/*if(m_nStatus==2) return RaiseError(E_FAIL, 1, iid, "You must call IRowset::RestartPosition before this");*/ \
	} while(0)

/*
 * 1. IRowsetChange와 CANHOLDROWS를 exclusive 하게
 *
 * 2. OWNUPDATEDELETE와 OTHERUPDATEDELETE를 같은 값을 가지게
 *
 *		CCI 구조상 어쩔 수 없다. Consumer가 둘 다 set 할 경우
 *		나중에 set 되는 값이 된다. CONFLICT가 나게 하려면
 *		기본값인지 Consumer에 의해 set 된 값인지 구분할 수 있어야 하는데
 *		간단하게는 안 될 듯 하다.
 *
 *		1은 기본값이 FALSE이므로 TRUE면 사용자가 set 한 걸로 해서 CONFLICT를
 *		낼 수 있다.
 *
 * 3. BOOKMARKS가 TRUE면 CANSCROLLBACKWARDS, CANFETCHBACKWARDS를 TRUE로
 *
 * 4. IRowsetLocate가 TRUE면 IRowsetScroll을 TRUE로
 *
 *		임시. ADO에서 IRowsetScroll의 함수를 호출하면서
 *		DBPROP_IRowsetScroll은 요청하지 않는다.
 *
 * 5. DBPROP_UPDATABILITY, DBPROP_BOOKMARKS가 DBPROP_IRowsetChange, DBPROP_IRowsetLocate와
 *	 CONFLICT나지 않도록
 *
 * 6. IRowsetFind가 TRUE면 BOOKMARKS를 TRUE로
 *
 *		LTM을 보니 IRowsetFind 속성을 별도로 요청하는 경우는 없는 것 같다. 일단 주석처리.
 */
template <class T>
HRESULT CUBRIDOnPropertyChanged(T *pT, ULONG iCurSet, DBPROP* pDBProp)
{
	ATLASSERT(pDBProp);

	switch(pDBProp->dwPropertyID)
	{
	//case DBPROP_IRowsetChange:
	//case DBPROP_CANHOLDROWS:
	//	{
	//		CComVariant var;
	//		pT->GetPropValue(&DBPROPSET_ROWSET, (pDBProp->dwPropertyID==DBPROP_CANHOLDROWS?
	//						DBPROP_IRowsetChange:DBPROP_CANHOLDROWS), &var);
	//		if(V_BOOL(&pDBProp->vValue)==ATL_VARIANT_TRUE && V_BOOL(&var)==ATL_VARIANT_TRUE)
	//		{
	//			pDBProp->dwStatus = DBPROPSTATUS_CONFLICTING;
	//			var = false;
	//			pT->SetPropValue(&DBPROPSET_ROWSET, pDBProp->dwPropertyID, &var);
	//			return E_FAIL;
	//		}
	//	}
	//	break;

	case DBPROP_OWNUPDATEDELETE:
	case DBPROP_OTHERUPDATEDELETE:
		{
			pT->SetPropValue(&DBPROPSET_ROWSET, (pDBProp->dwPropertyID==DBPROP_OWNUPDATEDELETE?
						DBPROP_OTHERUPDATEDELETE:DBPROP_OWNUPDATEDELETE), &pDBProp->vValue);
		}
		break;

	case DBPROP_UPDATABILITY:
		if(V_I4(&pDBProp->vValue)!=0)
		{	// IRowsetChange가 default FALSE인지, set FALSE인지 구별해서 TRUE 설정, 에러를 결정한다.
			UPROPVAL* pUPropVal = &(pT->m_pUProp[iCurSet].pUPropVal[pT->GetUPropValIndex(iCurSet, DBPROP_IRowsetChange)]);
			if(V_BOOL(&pUPropVal->vValue)==ATL_VARIANT_FALSE)
			{
				if(pUPropVal->dwFlags & DBINTERNFLAGS_CHANGED)
				{
					pDBProp->dwStatus = DBPROPSTATUS_CONFLICTING;
					CComVariant var = 0;
					pT->SetPropValue(&DBPROPSET_ROWSET, DBPROP_UPDATABILITY, &var);
					return E_FAIL;
				}
				else
				{
					CComVariant var = true;
					pT->SetPropValue(&DBPROPSET_ROWSET, DBPROP_IRowsetChange, &var);
				}
			}
		}
		return S_OK;

	case DBPROP_BOOKMARKS:
		if(V_BOOL(&pDBProp->vValue)==ATL_VARIANT_TRUE)
		{
			pT->SetPropValue(&DBPROPSET_ROWSET, DBPROP_CANFETCHBACKWARDS, &pDBProp->vValue);
			pT->SetPropValue(&DBPROPSET_ROWSET, DBPROP_CANSCROLLBACKWARDS, &pDBProp->vValue);
		}
		return S_OK;

	case DBPROP_IRowsetChange:
	case DBPROP_IRowsetUpdate:
		if(V_BOOL(&pDBProp->vValue)==ATL_VARIANT_TRUE)
		{
			VARIANT var;
			var.vt = VT_I4;
			var.lVal = DBPROPVAL_UP_CHANGE|DBPROPVAL_UP_DELETE|DBPROPVAL_UP_INSERT;
			pT->SetPropValue(&DBPROPSET_ROWSET, DBPROP_UPDATABILITY, &var);

			if (pDBProp->dwPropertyID == DBPROP_IRowsetUpdate)
			{
				VARIANT tmpVar;
				::VariantInit(&tmpVar);
				tmpVar.vt = VT_BOOL;
				tmpVar.boolVal = VARIANT_TRUE;

				pT->SetPropValue(&DBPROPSET_ROWSET, DBPROP_IRowsetChange, &tmpVar);
				::VariantClear(&tmpVar);
			}
		} 
		return S_OK;
	case DBPROP_IRowsetLocate:
		if(V_BOOL(&pDBProp->vValue)==ATL_VARIANT_TRUE)
		{
			// DBPROP_CANSCROLLBACKWARDS가 default FALSE인지, set FALSE인지 구별해서 TRUE 설정, 통과를 결정한다.
			UPROPVAL* pUPropVal = &(pT->m_pUProp[iCurSet].pUPropVal[pT->GetUPropValIndex(iCurSet, DBPROP_CANSCROLLBACKWARDS)]);
			if(V_BOOL(&pUPropVal->vValue)==ATL_VARIANT_FALSE)
			{
				if(!(pUPropVal->dwFlags & DBINTERNFLAGS_CHANGED))
					pT->SetPropValue(&DBPROPSET_ROWSET, DBPROP_CANSCROLLBACKWARDS, &pDBProp->vValue);
			}

			// DBPROP_CANFETCHBACKWARDS가 default FALSE인지, set FALSE인지 구별해서 TRUE 설정, 통과를 결정한다.
			pUPropVal = &(pT->m_pUProp[iCurSet].pUPropVal[pT->GetUPropValIndex(iCurSet, DBPROP_CANFETCHBACKWARDS)]);
			if(V_BOOL(&pUPropVal->vValue)==ATL_VARIANT_FALSE)
			{
				if(!(pUPropVal->dwFlags & DBINTERNFLAGS_CHANGED))
					pT->SetPropValue(&DBPROPSET_ROWSET, DBPROP_CANFETCHBACKWARDS, &pDBProp->vValue);
			}

			pT->SetPropValue(&DBPROPSET_ROWSET, DBPROP_IRowsetScroll, &pDBProp->vValue);
		}
		pT->SetPropValue(&DBPROPSET_ROWSET, DBPROP_BOOKMARKS, &pDBProp->vValue);
		return S_OK;

	//case DBPROP_IRowsetFind:
	//	if(V_BOOL(&pDBProp->vValue)==ATL_VARIANT_TRUE)
	//	{
	//		pT->SetPropValue(&DBPROPSET_ROWSET, DBPROP_BOOKMARKS, &pDBProp->vValue);
	//		pT->SetPropValue(&DBPROPSET_ROWSET, DBPROP_CANFETCHBACKWARDS, &pDBProp->vValue);
	//		pT->SetPropValue(&DBPROPSET_ROWSET, DBPROP_CANSCROLLBACKWARDS, &pDBProp->vValue);
	//	}
	//	break;
	}

	return S_FALSE;
}

class CDummy
{
};

class CCUBRIDRowset :
	public CRowsetImpl<CCUBRIDRowset, CDummy, CCUBRIDCommand, CAtlArray<CDummy>, CCUBRIDRowsetRow,
							IRowsetLocateImpl<CCUBRIDRowset, IRowsetScroll, CCUBRIDRowsetRow> >,
	public IColumnsRowsetImpl<CCUBRIDRowset, CCUBRIDCommand>,
	public IGetRow,
	public IRowsetUpdate, // IRowsetChange도 포함한다.
	public ISupportErrorInfo,
	public IRowsetFind,
	public IRowsetRefresh,
	public Util::ITxnCallback
{
	//===== Rowset 타입
public:
	enum Type { Invalid, FromSession, FromCommand, FromRow } m_eType;

	//===== Helper
public:
	typedef CAtlMap<_HRowClass::KeyType, _HRowClass *> MapClass;

	CCUBRIDDataSource *GetDataSourcePtr();
	CCUBRIDSession *GetSessionPtr();
	CCUBRIDCommand *GetCommandPtr();
	static CCUBRIDRowset *GetRowsetPtr(IObjectWithSite *pSite);

	//===== 핸들
private:
	int m_hReq; // FromCommand일 때는 Invalid
public:
	HRESULT GetConnectionHandle(int *phConn);
	int GetRequestHandle();

	//===== 컴파일이 되게 하기 위한 변수, 함수. 아무 일도 하지 않는다.
public:
	bool m_isPrepared; // for IColumnsRowsetImpl
	HRESULT Execute(DBPARAMS *pParams, LONG *pcRowsAffected)
	{
		return S_OK;
	}

	//===== Initialize, Finalize
private:
	HRESULT InitCommon(int cResult, bool bRegist=true);
public:
	CCUBRIDRowset();
	~CCUBRIDRowset();

	// TODO_REMOVE: hReq
	HRESULT ValidateCommandID(DBID* pTableID, DBID* pIndexID); // called by IOpenRowset::OpenRowset
	HRESULT InitFromSession(DBID *pTID, char flag); // called by IOpenRowset::OpenRowset
	HRESULT InitFromCommand(int hReq, int cResult, bool bAsynch=false); // called by ICommand::Execute
//	HRESULT InitFromRow(int hReq, int cResult); // called by IRow::Open

	//===== 기타 멤버
private:
	bool m_bAsynch;
	HRESULT Reexecute();
public:
	int m_nStatus; // 0: normal, 1: zombie, 2: need reexecute
	CComBSTR m_strTableName;
	virtual void TxnCallback(const ITxnCallback *pOwner);

	//===== IUnknown
	//   속성이 FALSE인 인터페이스에 대한 QI를 실패하게 한다.
	static HRESULT WINAPI InternalQueryInterface(void *pThis,
		const _ATL_INTMAP_ENTRY *pEntries, REFIID iid, void **ppvObject)
	{
		struct
		{
			const GUID *iid;
			DBPROPID dwPropId;
		} list[] = {
			{ &__uuidof(IRowsetChange), DBPROP_IRowsetChange },
			{ &__uuidof(IRowsetUpdate), DBPROP_IRowsetUpdate },
			{ &__uuidof(IRowsetLocate), DBPROP_IRowsetLocate },
			{ &__uuidof(IRowsetScroll), DBPROP_IRowsetScroll },
		};
		for(int i=0;i<sizeof(list)/sizeof(*list);i++)
		{
			if( InlineIsEqualGUID(iid, *list[i].iid) )
			{
				CComVariant var;
				((CCUBRIDRowset *)pThis)->GetPropValue(&DBPROPSET_ROWSET, list[i].dwPropId, &var);
				if(V_BOOL(&var)==ATL_VARIANT_FALSE) return E_NOINTERFACE;
			}
		}
		return _RowsetBaseClass::InternalQueryInterface(pThis, pEntries, iid, ppvObject);
	}

	//===== ISupportErrorInfo
	STDMETHOD(InterfaceSupportsErrorInfo)(REFIID riid)
	{
		if( InlineIsEqualGUID(riid, __uuidof(IAccessor)) ||
			InlineIsEqualGUID(riid, __uuidof(IColumnsInfo)) ||
			InlineIsEqualGUID(riid, __uuidof(IColumnsRowset)) ||
			InlineIsEqualGUID(riid, __uuidof(IConvertType)) ||
			InlineIsEqualGUID(riid, __uuidof(IGetRow)) ||
			InlineIsEqualGUID(riid, __uuidof(IRowset)) ||
			InlineIsEqualGUID(riid, __uuidof(IRowsetChange)) ||
			InlineIsEqualGUID(riid, __uuidof(IRowsetIdentity)) ||
			InlineIsEqualGUID(riid, __uuidof(IRowsetInfo)) ||
			InlineIsEqualGUID(riid, __uuidof(IRowsetFind)) ||
			InlineIsEqualGUID(riid, __uuidof(IRowsetLocate)) ||
			InlineIsEqualGUID(riid, __uuidof(IRowsetRefresh)) ||
			InlineIsEqualGUID(riid, __uuidof(IRowsetScroll)) ||
			InlineIsEqualGUID(riid, __uuidof(IRowsetUpdate)) )
			return S_OK;
		else
			return S_FALSE;
	}

	//===== IAccessor
	STDMETHOD(AddRefAccessor)(HACCESSOR hAccessor, DBREFCOUNT *pcRefCount);
	STDMETHOD(CreateAccessor)(DBACCESSORFLAGS dwAccessorFlags, DBCOUNTITEM cBindings,
			const DBBINDING rgBindings[], DBLENGTH cbRowSize,
			HACCESSOR *phAccessor, DBBINDSTATUS rgStatus[]);
	STDMETHOD(GetBindings)(HACCESSOR hAccessor, DBACCESSORFLAGS *pdwAccessorFlags,
			DBCOUNTITEM *pcBindings, DBBINDING **prgBindings);
	STDMETHOD(ReleaseAccessor)(HACCESSOR hAccessor, DBREFCOUNT *pcRefCount);

	//===== IColumnsInfoImpl
private:
	Util::CColumnsInfo m_Columns;
public:
	static ATLCOLUMNINFO* GetColumnInfo(CCUBRIDRowset *pv, DBORDINAL *pcCols);
	STDMETHOD(GetColumnDefaultValue)(CCUBRIDRowset* pv);

	STDMETHOD(GetColumnInfo)(DBORDINAL *pcColumns, DBCOLUMNINFO **prgInfo,
							OLECHAR **ppStringsBuffer);
	STDMETHOD(MapColumnIDs)(DBORDINAL cColumnIDs, const DBID rgColumnIDs[],
							DBORDINAL rgColumns[]);

	//===== IConvertType
	STDMETHOD(CanConvert)(DBTYPE wFromType, DBTYPE wToType, DBCONVERTFLAGS dwConvertFlags);

	//===== IRowset
private:
	HRESULT GetNextRowsAsynch(HCHAPTER hReserved, DBROWOFFSET lRowsOffset,
						DBROWCOUNT cRows, DBCOUNTITEM *pcRowsObtained, HROW **prghRows);
public:
	HRESULT CreateRow(DBROWOFFSET lRowsOffset, DBCOUNTITEM &cRowsObtained, HROW *rgRows);
	STDMETHOD(AddRefRows)(DBCOUNTITEM cRows, const HROW rghRows[],
						DBREFCOUNT rgRefCounts[], DBROWSTATUS rgRowStatus[]);
	STDMETHOD(GetData)(HROW hRow, HACCESSOR hAccessor, void *pDstData);
	STDMETHOD(GetNextRows)(HCHAPTER hReserved, DBROWOFFSET lRowsOffset,
						DBROWCOUNT cRows, DBCOUNTITEM *pcRowsObtained, HROW **prghRows);
	STDMETHOD(ReleaseRows)(DBCOUNTITEM cRows, const HROW rghRows[],
						DBROWOPTIONS rgRowOptions[], DBREFCOUNT rgRefCounts[],
						DBROWSTATUS rgRowStatus[]);
	STDMETHOD(RestartPosition)(HCHAPTER /*hReserved*/);

	//===== IRowsetChange : defined in RowsetChange.cpp
	STDMETHOD(DeleteRows)(HCHAPTER hReserved, DBCOUNTITEM cRows,
				const HROW rghRows[], DBROWSTATUS rgRowStatus[]);
	STDMETHOD(SetData)(HROW hRow, HACCESSOR hAccessor, void *pData);
	STDMETHOD(InsertRow)(HCHAPTER hReserved, HACCESSOR hAccessor, void *pData, HROW *phRow);

	//===== IRowsetUpdate : defined in RowsetChange.cpp
	STDMETHOD(GetOriginalData)(HROW hRow, HACCESSOR hAccessor, void *pData);
	STDMETHOD(GetPendingRows)(HCHAPTER hReserved, DBPENDINGSTATUS dwRowStatus,
				DBCOUNTITEM *pcPendingRows, HROW **prgPendingRows,
				DBPENDINGSTATUS **prgPendingStatus);
	STDMETHOD(GetRowStatus)(HCHAPTER hReserved, DBCOUNTITEM cRows,
				const HROW rghRows[], DBPENDINGSTATUS rgPendingStatus[]);
	STDMETHOD(Undo)(HCHAPTER hReserved, DBCOUNTITEM cRows, const HROW rghRows[],
				DBCOUNTITEM *pcRowsUndone, HROW **prgRowsUndone, DBROWSTATUS **prgRowStatus);
	STDMETHOD(Update)(HCHAPTER hReserved, DBCOUNTITEM cRows, const HROW rghRows[],
				DBCOUNTITEM *pcRows, HROW **prgRows, DBROWSTATUS **prgRowStatus);

	//===== IRowsetIdentity
	STDMETHOD(IsSameRow)(HROW hThisRow, HROW hThatRow);

	//===== IRowsetInfo
	virtual HRESULT OnPropertyChanged(ULONG iCurSet, DBPROP* pDBProp);
	virtual HRESULT IsValidValue(ULONG iCurSet, DBPROP* pDBProp);
	STDMETHOD(GetProperties)(const ULONG cPropertyIDSets, const DBPROPIDSET rgPropertyIDSets[],
					ULONG *pcPropertySets, DBPROPSET **prgPropertySets);
	STDMETHOD(GetReferencedRowset)(DBORDINAL iOrdinal, REFIID riid,
					IUnknown **ppReferencedRowset);
	STDMETHOD(GetSpecification)(REFIID riid, IUnknown **ppSpecification);

	//===== IGetRow
	STDMETHOD(GetRowFromHROW)(IUnknown *pUnkOuter, HROW hRow, REFIID riid, IUnknown **ppUnk);
	STDMETHOD(GetURLFromHROW)(HROW hRow, LPOLESTR *ppwszURL);

	//===== IRowsetFind
private:
	// GetNextRows는 cRows==0이면 그냥 끝내면 되지만
	// FindNextRow는 전에 찾았던 방향으로 찾아봐야 한다.
	bool m_bFindForward; // true: forward, false: backward
public:
	STDMETHOD(FindNextRow)(HCHAPTER hChapter, HACCESSOR hAccessor, void *pFindValue,
				DBCOMPAREOP CompareOp, DBBKMARK cbBookmark, const BYTE *pBookmark,
				DBROWOFFSET lRowsOffset, DBROWCOUNT cRows,
				DBCOUNTITEM *pcRowsObtained, HROW **prghRows);

	//===== IRowsetRefresh
	STDMETHOD(RefreshVisibleData)(HCHAPTER hChapter, DBCOUNTITEM cRows,
				const HROW rghRows[], BOOL fOverWrite, DBCOUNTITEM *pcRowsRefreshed,
				HROW **prghRowsRefreshed, DBROWSTATUS **prgRowStatus);
	STDMETHOD(GetLastVisibleData)(HROW hRow, HACCESSOR hAccessor, void *pData);

	//===== IRowsetLocate
	STDMETHOD(Compare)(HCHAPTER hReserved, DBBKMARK cbBookmark1, const BYTE *pBookmark1,
				DBBKMARK cbBookmark2, const BYTE *pBookmark2, DBCOMPARE *pComparison);
	STDMETHOD(GetRowsAt)(HWATCHREGION hReserved1, HCHAPTER hReserved2,
				DBBKMARK cbBookmark, const BYTE *pBookmark, DBROWOFFSET lRowsOffset,
				DBROWCOUNT cRows, DBCOUNTITEM *pcRowsObtained, HROW **prghRows);
	STDMETHOD(GetRowsByBookmark)(HCHAPTER hReserved, DBCOUNTITEM cRows,
				const DBBKMARK rgcbBookmarks[], const BYTE *rgpBookmarks[],
				HROW rghRows[], DBROWSTATUS rgRowStatus[]);
	STDMETHOD(Hash)(HCHAPTER hReserved, DBBKMARK cBookmarks,
				const DBBKMARK rgcbBookmarks[], const BYTE *rgpBookmarks[],
				DBHASHVALUE rgHashedValues[], DBROWSTATUS rgBookmarkStatus[]);

	//===== IRowsetScroll
	STDMETHOD(GetApproximatePosition)(HCHAPTER hReserved, DBBKMARK cbBookmark,
				const BYTE *pBookmark, DBCOUNTITEM *pulPosition, DBCOUNTITEM *pcRows);
	STDMETHOD(GetRowsAtRatio)(HWATCHREGION hReserved1, HCHAPTER hReserved2,
				DBCOUNTITEM ulNumerator, DBCOUNTITEM ulDenominator,
				DBROWCOUNT cRows, DBCOUNTITEM *pcRowsObtained, HROW **prghRows);

BEGIN_COM_MAP(CCUBRIDRowset)
	COM_INTERFACE_ENTRY(IGetRow)
	COM_INTERFACE_ENTRY(IColumnsRowset)
	COM_INTERFACE_ENTRY(IRowsetLocate)
	COM_INTERFACE_ENTRY(IRowsetScroll)
	COM_INTERFACE_ENTRY(IRowsetChange)
	COM_INTERFACE_ENTRY(IRowsetUpdate)
	COM_INTERFACE_ENTRY(IRowsetFind)
	COM_INTERFACE_ENTRY(IRowsetRefresh)
	COM_INTERFACE_ENTRY(ISupportErrorInfo)
	COM_INTERFACE_ENTRY_CHAIN(_RowsetBaseClass)
END_COM_MAP()
};
