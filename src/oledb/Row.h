// Row.h : Declaration of the CCUBRIDRow

#pragma once

#include "resource.h"       // main symbols
#include "util.h"

class CCUBRIDSession;
class CCUBRIDCommand;
class CCUBRIDRowset;

// CCUBRIDRow
[
	coclass,
	noncreatable,
	uuid("32881E3B-5F95-4019-A36B-0EF4E2AAA1DC"),
	threading("apartment"),
	registration_script("none"),
	support_error_info(IRow),
	support_error_info(IColumnsInfo)
]
class ATL_NO_VTABLE CCUBRIDRow :
	public IObjectWithSiteImpl<CCUBRIDRow>,
	public IConvertTypeImpl<CCUBRIDRow>,
	public IColumnsInfoImpl<CCUBRIDRow>,
	public IRow,
	public IGetSession,
	public Util::ITxnCallback
{
public:
	CCUBRIDSession *GetSessionPtr();
	CCUBRIDCommand *GetCommandPtr();
	CCUBRIDRowset *GetRowsetPtr();
	CCUBRIDRow *GetRowPtr();

public:
	enum Type { Invalid, FromSession, FromCommand, FromRowset, FromRow };
private:
	int m_hReq;
	T_CCI_COL_INFO* m_info;
	T_CCI_SQLX_CMD m_cmdType;
	char	m_szOID[15];
	int m_cCol;
	HROW m_hRow; // valid if and only if m_eType==FromRowset
	DBCOUNTITEM m_iRowset;
	Type m_eType;
	bool m_bBookmarks;
	bool m_bIsValid;

public:
	CCUBRIDRow();
	~CCUBRIDRow();

	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct()
	{
		return CConvertHelper::FinalConstruct();
	}

	void FinalRelease() 
	{
	}

	HRESULT Initialize(int hReq, bool bBookmarks=false, HROW hRow=NULL, DBCOUNTITEM iRowset=0);
	STDMETHOD(SetSite)(IUnknown *pUnkSite, Type eType);
	virtual void TxnCallback(const ITxnCallback *pOwner);

	// IConvertType
	unsigned m_bIsCommand:1;
	unsigned m_bHasParamaters:1;

	// IColumnsInfo
private:
	Util::CColumnsInfo m_Columns;
public:
	static ATLCOLUMNINFO* GetColumnInfo(CCUBRIDRow *pv, DBORDINAL *pcCols);
	STDMETHOD(GetColumnInfo)(DBORDINAL *pcColumns, DBCOLUMNINFO **prgInfo,
							OLECHAR **ppStringsBuffer);
	STDMETHOD(MapColumnIDs)(DBORDINAL cColumnIDs, const DBID rgColumnIDs[],
							DBORDINAL rgColumns[]);

	// IRow
	STDMETHOD(GetColumns)(DBORDINAL cColumns, DBCOLUMNACCESS rgColumns[]);
	STDMETHOD(GetSourceRowset)(REFIID riid, IUnknown **ppRowset, HROW *phRow);
	STDMETHOD(Open)(IUnknown *pUnkOuter, DBID *pColumnID, REFGUID rguidColumnType,
					DWORD dwBindFlags, REFIID riid, IUnknown **ppUnk);

	// IGetSession
	STDMETHOD(GetSession)(REFIID riid, IUnknown **ppSession);
};


