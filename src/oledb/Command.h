#pragma once

#include "resource.h"       // main symbols
#include "util.h"
#include "ColumnsRowset.h"

#define IMultipleResults_Type	VT_BOOL

class CCUBRIDDataSource;
class CCUBRIDSession;
class CCUBRIDRowset;

// CCUBRIDCommand
[
	coclass,
	noncreatable,
	uuid("3FA55BC9-F4E2-4926-906C-2B630A5F8530"),
	threading("apartment"),
	support_error_info(IAccessor),
	support_error_info(ICommand),
	support_error_info(ICommandPrepare),
	support_error_info(ICommandWithParameters),
	support_error_info(ICommandProperties),
	support_error_info(IColumnsInfo),
	support_error_info(ICommandText),
	registration_script("none")
]
class ATL_NO_VTABLE CCUBRIDCommand : 
	public IAccessorImpl<CCUBRIDCommand>,
	public ICommandTextImpl<CCUBRIDCommand>, //ICommandImpl을 상속받는다
	public ICommandWithParameters,
	public ICommandPropertiesImpl<CCUBRIDCommand>,
	public IColumnsRowsetImpl<CCUBRIDCommand, CCUBRIDCommand>,
	public ICommandPrepare,
	public IColumnsInfoImpl<CCUBRIDCommand>,
	public IObjectWithSiteImpl<CCUBRIDCommand>,
	public IConvertTypeImpl<CCUBRIDCommand>,
	public IInternalCommandConnectionImpl<CCUBRIDCommand>,
	public Util::ITxnCallback
{
public:
	CCUBRIDDataSource *GetDataSourcePtr();
	CCUBRIDSession *GetSessionPtr();
	static CCUBRIDCommand *GetCommandPtr(IObjectWithSite *pSite);

private:
	ULONG				m_prepareIndex;

// ICommand
public:
	HRESULT GetConnectionHandle(int *phConn);

	HACCESSOR*			m_phAccessor;
	DBBINDING*			m_pBindings;
	DBCOUNTITEM			m_cBindings;

	//ICommandWithParameters 변수들
	DBCOUNTITEM			m_cParams; //ICommandWithParameters를 통해 세팅된 파라메터 갯수
	DBCOUNTITEM			m_cParamsInQuery;//쿼리에 등장하는 파라메터 갯수('?' 갯수)
	DBPARAMINFO*		m_pParamInfo;

	CAtlArray<int> m_gOIDIndex; // start from 1
	CAtlArray<CStringA> m_gOID;

	int					m_hReq;
	ULONG				m_cExpectedRuns;
	bool				m_isPrepared;

	Util::CColumnsInfo		m_Columns;

	CComBSTR m_strTableName;

	CCUBRIDCommand();
	~CCUBRIDCommand();

	STDMETHOD(SetSite)(IUnknown *pUnkSite);
	virtual void TxnCallback(const ITxnCallback *pOwner);

	HRESULT FinalConstruct()
	{
		HRESULT hr = CConvertHelper::FinalConstruct();
		if (FAILED (hr))
			return hr;
		hr = IAccessorImpl<CCUBRIDCommand>::FinalConstruct();
		if (FAILED(hr))
			return hr;
		return CUtlProps<CCUBRIDCommand>::FInit();
	}

	void FinalRelease()
	{
		IAccessorImpl<CCUBRIDCommand>::FinalRelease();
	}

	virtual HRESULT OnPropertyChanged(ULONG iCurSet, DBPROP* pDBProp);
	virtual HRESULT IsValidValue(ULONG iCurSet, DBPROP* pDBProp);

	int GetRequestHandle() const { return m_hReq; }

	STDMETHOD(PrepareCommand)(int hConn, REFIID riid = GUID_NULL);
	//STDMETHOD(CheckIfCharCompatible)(int hConn, DBORDINAL iOrdinal, T_CCI_U_TYPE* uType);

	//ICommand::Execute Override
	STDMETHOD(Execute)(IUnknown * pUnkOuter, REFIID riid, DBPARAMS * pParams, 
						  DBROWCOUNT* pcRowsAffected, IUnknown** ppRowset);

	//ICommandText:SetCommandText override
	STDMETHOD(SetCommandText)(REFGUID rguidDialect,LPCOLESTR pwszCommand);

	//ICommandPrepare methods
	STDMETHOD(Prepare)(ULONG cExpectedRuns);
	STDMETHOD(Unprepare)(void);

	//IColumnsInfo methods
	STDMETHOD(MapColumnIDs)(DBORDINAL cColumnIDs, const DBID rgColumnIDs[],
							DBORDINAL rgColumns[]);
	STDMETHOD(GetColumnInfo)(DBORDINAL *pcColumns, DBCOLUMNINFO **prgInfo, OLECHAR **ppStringsBuffer);
	static ATLCOLUMNINFO* GetColumnInfo(CCUBRIDCommand* pv, ULONG* pcInfo);
	

	//ICommandWithParameters methods
	STDMETHOD(GetParameterInfo)(DB_UPARAMS *pcParams, DBPARAMINFO **prgParamInfo,
            OLECHAR **ppNamesBuffer);
	STDMETHOD(MapParameterNames)(DB_UPARAMS cParamNames, const OLECHAR *rgParamNames[],
			DB_LPARAMS rgParamOrdinals[]);
	STDMETHOD(SetParameterInfo)(DB_UPARAMS cParams, const DB_UPARAMS rgParamOrdinals[],
		const DBPARAMBINDINFO rgParamBindInfo[]);

BEGIN_PROPSET_MAP(CCUBRIDCommand)
	BEGIN_PROPERTY_SET(DBPROPSET_ROWSET)
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(ABORTPRESERVE, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) // R
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(ACCESSORDER, DBPROPVAL_AO_RANDOM, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		PROPERTY_INFO_ENTRY            (BOOKMARKINFO) // 0, R
		PROPERTY_INFO_ENTRY            (BOOKMARKS) // FALSE, RW
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(BOOKMARKSKIPPED, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(BOOKMARKTYPE, DBPROPVAL_BMK_NUMERIC, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		PROPERTY_INFO_ENTRY_VALUE      (CANFETCHBACKWARDS, ATL_VARIANT_FALSE) // RW
		PROPERTY_INFO_ENTRY_VALUE      (CANHOLDROWS, ATL_VARIANT_FALSE) // RW
		PROPERTY_INFO_ENTRY_VALUE      (CANSCROLLBACKWARDS, ATL_VARIANT_FALSE) // RW
		PROPERTY_INFO_ENTRY_VALUE      (CHANGEINSERTEDROWS, ATL_VARIANT_FALSE) // R
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(COMMITPRESERVE, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) // R
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(FINDCOMPAREOPS, 0, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ | DBPROPFLAGS_COLUMNOK | DBPROPFLAGS_CHANGE)
		PROPERTY_INFO_ENTRY            (HIDDENCOLUMNS) // 0, R
		PROPERTY_INFO_ENTRY            (IAccessor) // TRUE, R
		PROPERTY_INFO_ENTRY            (IColumnsInfo) // TRUE, R
		PROPERTY_INFO_ENTRY_VALUE      (IColumnsRowset, ATL_VARIANT_TRUE) // R
		PROPERTY_INFO_ENTRY            (IConvertType) // TRUE, R
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(IGetRow, ATL_VARIANT_TRUE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(IGetSession, ATL_VARIANT_TRUE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) //TRUE, R
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(IMMOBILEROWS, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		PROPERTY_INFO_ENTRY_VALUE	   (IMultipleResults, ATL_VARIANT_FALSE)
		PROPERTY_INFO_ENTRY            (IRow) // FALSE, RW
		PROPERTY_INFO_ENTRY            (IRowset) // TRUE, R
		PROPERTY_INFO_ENTRY            (IRowsetChange) // FALSE, RW
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(IRowsetFind, ATL_VARIANT_TRUE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		PROPERTY_INFO_ENTRY            (IRowsetIdentity) // TRUE, R
		PROPERTY_INFO_ENTRY            (IRowsetInfo) // TRUE, R
		PROPERTY_INFO_ENTRY            (IRowsetLocate) // FALSE, RW
		PROPERTY_INFO_ENTRY_VALUE	   (IRowsetRefresh, ATL_VARIANT_FALSE)
		PROPERTY_INFO_ENTRY            (IRowsetScroll) // FALSE, RW
		PROPERTY_INFO_ENTRY            (IRowsetUpdate) // FALSE, RW
		PROPERTY_INFO_ENTRY_VALUE      (ISequentialStream, ATL_VARIANT_TRUE) // R
		PROPERTY_INFO_ENTRY_VALUE	   (ISupportErrorInfo, ATL_VARIANT_TRUE) // R
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(LITERALBOOKMARKS, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(LITERALIDENTITY, ATL_VARIANT_TRUE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		PROPERTY_INFO_ENTRY            (MAXOPENROWS) // 0, R
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(MAXPENDINGROWS, 0, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		PROPERTY_INFO_ENTRY            (MAXROWS) // 0, RW
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(ORDEREDBOOKMARKS, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(OTHERINSERT, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		PROPERTY_INFO_ENTRY            (OTHERUPDATEDELETE) // FALSE, RW
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(OWNINSERT, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		PROPERTY_INFO_ENTRY            (OWNUPDATEDELETE) // FALSE, RW
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(REMOVEDELETED, ATL_VARIANT_TRUE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		//PROPERTY_INFO_ENTRY            (REPORTMULTIPLECHANGES) // FALSE, R
		//PROPERTY_INFO_ENTRY_VALUE      (RETURNPENDINGINSERTS, ATL_VARIANT_TRUE) // R
		//PROPERTY_INFO_ENTRY            (ROWRESTRICT) // FALSE, R
		PROPERTY_INFO_ENTRY_VALUE      (ROWTHREADMODEL, DBPROPVAL_RT_APTMTTHREAD) // R
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(SERVERDATAONINSERT, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ)
		PROPERTY_INFO_ENTRY            (STRONGIDENTITY) // FALSE, R
		PROPERTY_INFO_ENTRY_VALUE      (UPDATABILITY, 0/*DBPROPVAL_UP_CHANGE|DBPROPVAL_UP_INSERT|DBPROPVAL_UP_DELETE*/) // RW
	END_PROPERTY_SET(DBPROPSET_ROWSET)
END_PROPSET_MAP()
};