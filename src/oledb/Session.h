// Session.h : Declaration of the CCUBRIDSession

#pragma once

#include "resource.h"       // main symbols
#include "Command.h"
#include "util.h"

class CCUBRIDDataSource;

// SR = Schema Rowset
class CSRTables;
class CSRColumns;
class CSRProviderTypes;
class CSRTablePrivileges;
class CSRColumnPrivileges;
class CSRTableConstraints;
class CSRTablesInfo;
class CSRStatistics;
class CSRIndexes;
class CSRViewColumnUsage;
class CSRViews;

// CCUBRIDSession
[
	coclass,
	noncreatable,
	uuid("F4CD8484-A670-4511-8DF5-F77B2942B985"),
	threading("apartment"),
	registration_script("none")
]
class ATL_NO_VTABLE CCUBRIDSession : 
	public IGetDataSourceImpl<CCUBRIDSession>,
	public IOpenRowsetImpl<CCUBRIDSession>,
	public ISessionPropertiesImpl<CCUBRIDSession>,
	public IObjectWithSiteSessionImpl<CCUBRIDSession>,
	public IDBSchemaRowsetImpl<CCUBRIDSession>,
	public IDBCreateCommandImpl<CCUBRIDSession, CCUBRIDCommand>,
	public ITransactionLocal,
	public ITableDefinition,
	public IIndexDefinition
{
public:
	CCUBRIDDataSource *GetDataSourcePtr();
	static CCUBRIDSession *GetSessionPtr(IObjectWithSite *pSite);

private:
	int m_hConn;
public:
	HRESULT GetConnectionHandle(int *phConn);

	CCUBRIDSession() : m_hConn(0)
	{
		ATLTRACE2(atlTraceDBProvider, 3, "CCUBRIDSession::CCUBRIDSession\n");

		m_bAutoCommit = true;
	}

	~CCUBRIDSession()
	{
		ATLASSERT(m_grpTxnCallbacks.GetCount()==0);
		ATLTRACE2(atlTraceDBProvider, 3, "CCUBRIDSession::~CCUBRIDSession\n");
	}

	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct()
	{
		ATLTRACE2(atlTraceDBProvider, 3, "CCUBRIDSession::FinalConstruct\n");
		return FInit();
	}
	
	void FinalRelease();

	// ITransactionLocal
private:
	ISOLEVEL m_isoLevel;
	bool m_bAutoCommit;
	CAtlList<Util::ITxnCallback *> m_grpTxnCallbacks;

	HRESULT DoCASCCICommit(bool bCommit);
	HRESULT SetCASCCIIsoLevel(ISOLEVEL isoLevel, bool bCheckOnly=false);
	void EnterAutoCommitMode();
public:
	HRESULT AutoCommit(const Util::ITxnCallback *pOwner);
	HRESULT RowsetCommit();
	HRESULT RegisterTxnCallback(Util::ITxnCallback *pTxnCallback, bool bRegister);
	STDMETHOD(GetOptionsObject)(ITransactionOptions **ppOptions);
	STDMETHOD(StartTransaction)(ISOLEVEL isoLevel, ULONG isoFlags,
				ITransactionOptions *pOtherOptions, ULONG *pulTransactionLevel);
    STDMETHOD(Commit)(BOOL fRetaining, DWORD grfTC, DWORD grfRM);
	STDMETHOD(Abort)(BOID *pboidReason, BOOL fRetaining, BOOL fAsync);
	STDMETHOD(GetTransactionInfo)(XACTTRANSINFO *pinfo);

	// IOpenRowset
	STDMETHOD(OpenRowset)(IUnknown *pUnk, DBID *pTID, DBID *pInID, REFIID riid,
						ULONG cSets, DBPROPSET rgSets[], IUnknown **ppRowset);

	// ISessionProperties
	virtual HRESULT	IsValidValue(ULONG /*iCurSet*/, DBPROP* pDBProp);
	virtual HRESULT OnPropertyChanged(ULONG /*iCurSet*/, DBPROP *pDBProp);


	//ITableDefinition
	STDMETHOD(CreateTable)(IUnknown *pUnkOuter,
            DBID *pTableID,
            DBORDINAL cColumnDescs,
            const DBCOLUMNDESC rgColumnDescs[],
            REFIID riid,
            ULONG cPropertySets,
            DBPROPSET rgPropertySets[],
            DBID **ppTableID,
            IUnknown **ppRowset);
    STDMETHOD(DropTable)(DBID *pTableID);
    STDMETHOD(AddColumn)(DBID *pTableID, DBCOLUMNDESC *pColumnDesc, DBID **ppColumnID);
    STDMETHOD(DropColumn)(DBID *pTableID, DBID *pColumnID);

	//IIndexDefinition
	STDMETHOD(CreateIndex)( 
            DBID *pTableID,
            DBID *pIndexID,
            DBORDINAL cIndexColumnDescs,
            const DBINDEXCOLUMNDESC rgIndexColumnDescs[],
            ULONG cPropertySets,
            DBPROPSET rgPropertySets[],
            DBID **ppIndexID);
    STDMETHOD(DropIndex)( 
            DBID *pTableID,
            DBID *pIndexID);


BEGIN_PROPSET_MAP(CCUBRIDSession)
	BEGIN_PROPERTY_SET(DBPROPSET_SESSION)
		PROPERTY_INFO_ENTRY_VALUE(SESS_AUTOCOMMITISOLEVELS, DBPROPVAL_TI_READCOMMITTED)
	END_PROPERTY_SET(DBPROPSET_SESSION)
END_PROPSET_MAP()

	// IDBSchemaRowset
	void SetRestrictions(ULONG cRestrictions, GUID* rguidSchema, ULONG* rgRestrictions);
	HRESULT CheckRestrictions(REFGUID rguidSchema, ULONG cRestrictions, 
				const VARIANT rgRestrictions[]);

BEGIN_SCHEMA_MAP(CCUBRIDSession)
	SCHEMA_ENTRY(DBSCHEMA_TABLES, CSRTables)
	SCHEMA_ENTRY(DBSCHEMA_COLUMNS, CSRColumns)
	SCHEMA_ENTRY(DBSCHEMA_PROVIDER_TYPES, CSRProviderTypes)
	SCHEMA_ENTRY(DBSCHEMA_TABLE_PRIVILEGES, CSRTablePrivileges)
	SCHEMA_ENTRY(DBSCHEMA_COLUMN_PRIVILEGES, CSRColumnPrivileges)
	SCHEMA_ENTRY(DBSCHEMA_TABLE_CONSTRAINTS, CSRTableConstraints)
	SCHEMA_ENTRY(DBSCHEMA_TABLES_INFO, CSRTablesInfo)
	SCHEMA_ENTRY(DBSCHEMA_STATISTICS, CSRStatistics)
	SCHEMA_ENTRY(DBSCHEMA_INDEXES, CSRIndexes)
	SCHEMA_ENTRY(DBSCHEMA_VIEW_COLUMN_USAGE, CSRViewColumnUsage)
	SCHEMA_ENTRY(DBSCHEMA_VIEWS, CSRViews)
END_SCHEMA_MAP()
};

#define SR_PROPSET_MAP(Class)						\
BEGIN_PROPSET_MAP(Class)							\
	BEGIN_PROPERTY_SET(DBPROPSET_ROWSET)			\
		PROPERTY_INFO_ENTRY(IAccessor)				\
		PROPERTY_INFO_ENTRY(IColumnsInfo)			\
		PROPERTY_INFO_ENTRY(IConvertType)			\
		PROPERTY_INFO_ENTRY(IRowset)				\
		PROPERTY_INFO_ENTRY(IRowsetIdentity)		\
		PROPERTY_INFO_ENTRY(IRowsetInfo)			\
		PROPERTY_INFO_ENTRY(CANFETCHBACKWARDS)		\
		PROPERTY_INFO_ENTRY(CANHOLDROWS)			\
		PROPERTY_INFO_ENTRY(CANSCROLLBACKWARDS)		\
		PROPERTY_INFO_ENTRY_VALUE(MAXOPENROWS, 0)	\
		PROPERTY_INFO_ENTRY_VALUE(MAXROWS, 0)		\
		/* 이 뒤는 LTM을 위한 속성 */								\
		/* LTM은 CCUBRIDDataSource를 통해 속성을 얻는다 */				\
		/* 따라서 Schema Rowset이 아닌 */							\
		/* CCUBRIDCommand에 정의된 속성을 보고 테스트를 진행한다 */	\
		PROPERTY_INFO_ENTRY_VALUE      (ABORTPRESERVE, ATL_VARIANT_FALSE) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(ACCESSORDER, DBPROPVAL_AO_RANDOM, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(BOOKMARKSKIPPED, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE      (CHANGEINSERTEDROWS, ATL_VARIANT_TRUE) \
		PROPERTY_INFO_ENTRY            (COLUMNRESTRICT) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(COMMITPRESERVE, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE      (IColumnsRowset, ATL_VARIANT_FALSE) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(IGetRow, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(IGetSession, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(IMMOBILEROWS, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(IMultipleResults, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(IRowsetFind, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(IRowsetRefresh, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE      (ISequentialStream, ATL_VARIANT_FALSE) \
		PROPERTY_INFO_ENTRY_VALUE	   (ISupportErrorInfo, ATL_VARIANT_FALSE) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(LITERALBOOKMARKS, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(LITERALIDENTITY, ATL_VARIANT_TRUE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(ORDEREDBOOKMARKS, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(OTHERINSERT, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(OTHERUPDATEDELETE, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(OWNINSERT, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(REMOVEDELETED, ATL_VARIANT_FALSE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(SERVERDATAONINSERT, ATL_VARIANT_TRUE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
		PROPERTY_INFO_ENTRY            (STRONGIDENTITY) \
		PROPERTY_INFO_ENTRY_VALUE_FLAGS(UPDATABILITY, DBPROPVAL_UP_CHANGE|DBPROPVAL_UP_INSERT|DBPROPVAL_UP_DELETE, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ) \
	END_PROPERTY_SET(DBPROPSET_ROWSET)				\
END_PROPSET_MAP()

#include "SRTables.h"
#include "SRColumns.h"
#include "SRProviderTypes.h"
#include "SRTablePrivileges.h"
#include "SRColumnPrivileges.h"
#include "SRTableConstraints.h"
#include "SRTablesInfo.h"
#include "SRStatistics.h"
#include "SRIndexes.h"
#include "SRViewColumnUsage.h"
#include "SRViews.h"
#include "SRPrimaryKeys.h"
#include "SRKeyColumnUsage.h"
