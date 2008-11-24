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

#include "type.h"
#include "Error.h"

#define CUBRID_COLENTRY_EX(name, ordinal, flags, colSize, dbtype, precision, scale, dbid, member) \
	{ \
		(LPOLESTR)OLESTR(name), \
		NULL, \
		(DBORDINAL)ordinal, \
		flags, \
		colSize, \
		dbtype, \
		(BYTE)precision, \
		(BYTE)scale, \
		{ \
			EXPANDGUID(dbid.uGuid.guid), \
			dbid.eKind, \
			dbid.uName.pwszName /* sizeof(LPOLESTR)==sizeof(ULONG)이거나 little-endian이면
								상관없는데, 그 외에는 문제가 있지 않을까 싶다. */ \
		}, \
		offsetof(_Class, member) \
	},

#define CUBRID_COLENTRY(name, ordinal, maybenull, precision, dbid, member) \
	CUBRID_COLENTRY_EX(name, ordinal, DBCOLUMNFLAGS_ISFIXEDLENGTH | ( maybenull ? DBCOLUMNFLAGS_MAYBENULL : 0 ), \
					(DBLENGTH)SIZEOF_MEMBER(_Class, member), ATL::_GetOleDBType(((_Class*)0)->member), \
					precision, 255, dbid, member)

// atldb.h에선 colSize==255인데, LTM에서 실패해서 127로 변경했다.
#define CUBRID_COLENTRY_WSTR(name, ordinal, dbid, member) \
	CUBRID_COLENTRY_EX(name, ordinal, DBCOLUMNFLAGS_MAYBENULL, 127, DBTYPE_WSTR, 255, 255, dbid, member)

#define CUBRID_COLENTRY_BOOL(name, ordinal, dbid, member) \
	CUBRID_COLENTRY_EX(name, ordinal, DBCOLUMNFLAGS_ISFIXEDLENGTH | DBCOLUMNFLAGS_MAYBENULL, \
					sizeof(VARIANT_BOOL), DBTYPE_BOOL, 255, 255, dbid, member)

const int g_cRequiredMetadataColumns = 11;
		const int g_cOptionalMetadataColumns = 9;

class CColumnsRowsetRow
{
public:
	WCHAR m_szIDName[129];
	GUID m_guid;
	ULONG m_ulPropid;
	WCHAR m_szName[129];
	ULONG m_ulNumber;
	USHORT m_nType;
	IUnknown *m_pTypeInfo;
	ULONG m_ulColumnSize;
	USHORT m_nPrecision;
	SHORT m_nScale;
	ULONG m_ulFlags;
	ULONG m_ulSearchable;
	VARIANT_BOOL m_bCaseSensitive;
	WCHAR m_szBaseColumnName[256];
	WCHAR m_szBaseTableName[256];
	VARIANT m_varDefault;
	VARIANT_BOOL m_bHasDefault;
	VARIANT_BOOL m_bIsUnique;
	ULONG m_ulDateTimePrecision;
	ULONG m_ulCharOctetLength;

	CColumnsRowsetRow()
		: m_guid(GUID_NULL),
		  m_ulPropid(0),
		  m_ulNumber(0),
		  m_nType(0),
		  m_pTypeInfo(NULL),
		  m_ulColumnSize(0),
		  m_nPrecision(0),
		  m_nScale(0),
		  m_ulFlags(0),
		  m_ulSearchable(0),
		  m_bCaseSensitive(ATL_VARIANT_FALSE),
		  m_bHasDefault(ATL_VARIANT_FALSE),
		  m_bIsUnique(ATL_VARIANT_FALSE),
		  m_ulDateTimePrecision(0),
		  m_ulCharOctetLength(0)
	{
		m_szIDName[0] = NULL;
		m_szName[0] = NULL;
		m_szBaseColumnName[0] = NULL;
		m_szBaseTableName[0] = NULL;
		::VariantInit(&m_varDefault);
	}

BEGIN_PROVIDER_COLUMN_MAP(CColumnsRowsetRow)
	// Required Metadata Columns
	CUBRID_COLENTRY_WSTR("DBCOLUMN_IDNAME",     1,            DBCOLUMN_IDNAME,     m_szIDName)
	CUBRID_COLENTRY     ("DBCOLUMN_GUID",       2, true, 255, DBCOLUMN_GUID,       m_guid)
	CUBRID_COLENTRY     ("DBCOLUMN_PROPID",     3, true,  10, DBCOLUMN_PROPID,     m_ulPropid)
	CUBRID_COLENTRY_WSTR("DBCOLUMN_NAME",       4,            DBCOLUMN_NAME,       m_szName)
	CUBRID_COLENTRY     ("DBCOLUMN_NUMBER",     5, false, 10, DBCOLUMN_NUMBER,     m_ulNumber)
	CUBRID_COLENTRY     ("DBCOLUMN_TYPE",       6, false,  5, DBCOLUMN_TYPE,       m_nType)
	CUBRID_COLENTRY     ("DBCOLUMN_TYPEINFO",   7, true, 255, DBCOLUMN_TYPEINFO,   m_pTypeInfo)
	CUBRID_COLENTRY     ("DBCOLUMN_COLUMNSIZE", 8, false, 10, DBCOLUMN_COLUMNSIZE, m_ulColumnSize)
	CUBRID_COLENTRY     ("DBCOLUMN_PRECISION",  9, true,   5, DBCOLUMN_PRECISION,  m_nPrecision)
	CUBRID_COLENTRY     ("DBCOLUMN_SCALE",     10, true,   5, DBCOLUMN_SCALE,      m_nScale)
	CUBRID_COLENTRY     ("DBCOLUMN_FLAGS",     11, false, 10, DBCOLUMN_FLAGS,      m_ulFlags)
	// Optional Metadata Columns
	CUBRID_COLENTRY     ("DBCOLUMN_ISSEARCHABLE",    0, true,  10, DBCOLUMN_ISSEARCHABLE,    m_ulSearchable)
	CUBRID_COLENTRY_BOOL("DBCOLUMN_ISCASESENSITIVE", 0,            DBCOLUMN_ISCASESENSITIVE, m_bCaseSensitive)
	CUBRID_COLENTRY_WSTR("DBCOLUMN_BASECOLUMNNAME",  0,            DBCOLUMN_BASECOLUMNNAME,  m_szBaseColumnName)
	CUBRID_COLENTRY_WSTR("DBCOLUMN_BASETABLENAME",   0,            DBCOLUMN_BASETABLENAME,   m_szBaseTableName)
	CUBRID_COLENTRY     ("DBCOLUMN_DEFAULTVALUE",    0, true, 255, DBCOLUMN_DEFAULTVALUE,    m_varDefault)
	CUBRID_COLENTRY_BOOL("DBCOLUMN_HASDEFAULT",      0,            DBCOLUMN_HASDEFAULT,      m_bHasDefault)
	CUBRID_COLENTRY_BOOL("DBCOLUMN_ISUNIQUE",        0,            DBCOLUMN_ISUNIQUE,        m_bIsUnique)
	CUBRID_COLENTRY     ("DBCOLUMN_DATETIMEPRECISION", 0, true, 10, DBCOLUMN_DATETIMEPRECISION, m_ulDateTimePrecision)
	CUBRID_COLENTRY     ("DBCOLUMN_OCTETLENGTH",     0, true,  10, DBCOLUMN_OCTETLENGTH,     m_ulCharOctetLength)
END_PROVIDER_COLUMN_MAP()
};

// CreatorClass = CCUBRIDCommand 인데 고정시키는 것과 다음처럼
// template화 하는 것중 어느 것이 좋을지 모르겠음
// 고정시키면 구현을 *.cpp로 분리할 수 있어 의존성이 줄긴 함
template <class CreatorClass>
class CColumnsRowset :
	public CSchemaRowsetImpl<CColumnsRowset<CreatorClass>, CColumnsRowsetRow, CreatorClass>
{
private:
	bool m_bIsTable;
	DBORDINAL m_cColumns;
	ATLCOLUMNINFO *m_rgColumns;
public:

	typedef CColumnsRowset<CreatorClass> _RowsetClass;
BEGIN_PROPSET_MAP(_RowsetClass)
	BEGIN_PROPERTY_SET(DBPROPSET_ROWSET)
		PROPERTY_INFO_ENTRY(IAccessor)
		PROPERTY_INFO_ENTRY(IColumnsInfo)
		PROPERTY_INFO_ENTRY(IConvertType)
		PROPERTY_INFO_ENTRY(IRowset)
		PROPERTY_INFO_ENTRY(IRowsetIdentity)
		PROPERTY_INFO_ENTRY(IRowsetInfo)
		PROPERTY_INFO_ENTRY(CANFETCHBACKWARDS)
		PROPERTY_INFO_ENTRY(CANHOLDROWS)
		PROPERTY_INFO_ENTRY(CANSCROLLBACKWARDS)
		PROPERTY_INFO_ENTRY_VALUE(MAXOPENROWS, 0)
		PROPERTY_INFO_ENTRY_VALUE(MAXROWS, 0)
	END_PROPERTY_SET(DBPROPSET_ROWSET)
END_PROPSET_MAP()

	CColumnsRowset() : m_bIsTable(false), m_rgColumns(0)
	{
	}

	~CColumnsRowset()
	{
		delete [] m_rgColumns;
	}

	HRESULT Execute(int hConn, int hReq, char *szTableName, DBORDINAL cOptColumns, const DBID rgOptColumns[], bool bHasBookmark)
	{
		bool bAll = false;
		if(cOptColumns==0)
		{
			bAll = true;
			cOptColumns = g_cOptionalMetadataColumns;
		}

		m_rgColumns = new ATLCOLUMNINFO[g_cRequiredMetadataColumns+cOptColumns];
		if(!m_rgColumns)
			return RaiseError(E_OUTOFMEMORY, 0, __uuidof(IColumnsRowset));

		int cRows, rc;
		ULONG ulMaxLen;
		T_CCI_ERROR error;
		T_CCI_CUBRID_STMT cmd_type;
		T_CCI_COL_INFO *info = cci_get_result_info(hReq, &cmd_type, &cRows);
		if(info==NULL) return RaiseError(E_FAIL, 1, __uuidof(IColumnsRowset), "cci_get_result_info failed");
		//ATLASSERT(cmd_type==CUBRID_STMT_SELECT || cmd_type==CUBRID_STMT_INSERT);

		rc = cci_get_db_parameter(hConn, CCI_PARAM_MAX_STRING_LENGTH, &ulMaxLen, &error);
		if (rc < 0) return RaiseError(E_FAIL, 1, __uuidof(IColumnsRowset), error.err_msg);

		CAtlArray<Type::TableInfo> tbl_infos;
		if(szTableName)
		{
			m_bIsTable = true;
			Type::GetTableInfo(hConn, szTableName, tbl_infos);
		}

		if (bHasBookmark)
		{
			CColumnsRowsetRow crrData;

			crrData.m_ulNumber = 0;
			crrData.m_nType = DBTYPE_UI4;
			crrData.m_ulSearchable = DB_UNSEARCHABLE;
			crrData.m_ulFlags = DBCOLUMNFLAGS_ISBOOKMARK|DBCOLUMNFLAGS_ISFIXEDLENGTH;
			crrData.m_nPrecision = 10;
			crrData.m_nScale = 0;
			memcpy(&crrData.m_guid, &DBCOL_SPECIALCOL, sizeof(GUID));
			crrData.m_ulPropid = 2;
			crrData.m_ulColumnSize = sizeof(ULONG);
			crrData.m_bCaseSensitive = VARIANT_FALSE;
			crrData.m_bHasDefault = VARIANT_FALSE;
			crrData.m_bIsUnique = VARIANT_FALSE;
			crrData.m_ulDateTimePrecision = ~0;
			crrData.m_ulCharOctetLength = ~0;
			_ATLTRY
			{
				m_rgRowData.Add(crrData);
			}
			_ATLCATCHALL()
			{
				ATLTRACE2("out of memory\n");
				return RaiseError(E_OUTOFMEMORY, 0, __uuidof(IColumnsRowset));
			}
		}

		for(int i=0;i<cRows;i++)
		{
			CColumnsRowsetRow crrData;

			int type = CCI_GET_RESULT_INFO_TYPE(info, i+1);
			const Type::StaticTypeInfo &sta_info =
				( type==-1 ? Type::GetStaticTypeInfo(CCI_U_TYPE_STRING) :
							 Type::GetStaticTypeInfo(info, i+1) );
			Type::DynamicTypeInfo dyn_info =
				( type==-1 ? Type::GetDynamicTypeInfo(CCI_U_TYPE_STRING, 0, 0, true) :
							 Type::GetDynamicTypeInfo(info, i+1) );

			wcscpy(crrData.m_szName, CA2W(CCI_GET_RESULT_INFO_NAME(info, i+1)));
			crrData.m_ulNumber = i+1;
			crrData.m_nType = sta_info.nOLEDBType;
			crrData.m_ulSearchable = sta_info.ulSearchable;
			crrData.m_ulFlags = dyn_info.ulFlags;
			crrData.m_nPrecision = (dyn_info.bPrecision==(BYTE)~0 ? (USHORT)~0 : dyn_info.bPrecision);
			crrData.m_nScale = (dyn_info.bScale==(BYTE)~0 ? -1 : dyn_info.bScale);
			crrData.m_bCaseSensitive = sta_info.bCaseSensitive;
			crrData.m_ulDateTimePrecision = dyn_info.ulDateTimePrecision;
			crrData.m_guid.Data1 = crrData.m_ulNumber; 

			if(dyn_info.ulColumnSize>ulMaxLen)
				crrData.m_ulColumnSize = ulMaxLen;
			else
				crrData.m_ulColumnSize = dyn_info.ulColumnSize;

			if(dyn_info.ulCharOctetLength!=(ULONG)~0 && dyn_info.ulCharOctetLength>ulMaxLen)
				crrData.m_ulCharOctetLength = ulMaxLen;
			else
				crrData.m_ulCharOctetLength = dyn_info.ulCharOctetLength;

			// TODO: Columns Schema Rowset에서는 NULL 인데,
			// 여기서는 15로 해야 LTM을 통과한다. 이유는 잘 모르겠다.
			// LTM이 틀렸을 수도.
			if(crrData.m_nType==DBTYPE_DBDATE || crrData.m_nType==DBTYPE_DBTIME)
			{
				crrData.m_ulDateTimePrecision = 15;
			}

			if(szTableName)
			{
				// tbl_infos[j].iOrdinal == j+1 일 것 같긴 한데 확실하진 않다.
				for(size_t j=0;j<tbl_infos.GetCount();j++)
				{
					if(tbl_infos[j].iOrdinal==crrData.m_ulNumber)
					{
						Type::TableInfo &cur_info = tbl_infos[j];
						wcscpy(crrData.m_szBaseColumnName, CA2W(CCI_GET_RESULT_INFO_ATTR_NAME(info, i+1)));
						wcscpy(crrData.m_szBaseTableName, cur_info.strSourceClass);
						_wcsupr(crrData.m_szBaseTableName);
						if(V_VT(&cur_info.varDefault)!=VT_EMPTY)
							crrData.m_bHasDefault = ATL_VARIANT_TRUE;
						::VariantCopy(&crrData.m_varDefault, &cur_info.varDefault);
						if(cur_info.bIsUnique)
							crrData.m_bIsUnique = ATL_VARIANT_TRUE;
						break;
					}
				}
			}
			
			_ATLTRY
			{
				m_rgRowData.Add(crrData);
			}
			_ATLCATCHALL()
			{
				ATLTRACE2("out of memory\n");
				return RaiseError(E_OUTOFMEMORY, 0, __uuidof(IColumnsRowset));
			}
		}

		// 요청되는 optional metadata columns가 다르므로
		// CColumnsRowsetRow::GetColumnInfo를 바로 이용할 수 없다.
		// 그래서 적당히 복사해서 새로 만든다.
		DBORDINAL tmp;
		ATLCOLUMNINFO *pInfo = _StorageClass::GetColumnInfo(this, &tmp);
		m_cColumns = g_cRequiredMetadataColumns + cOptColumns;

		// Required Column의 정보를 복사
		memcpy(m_rgColumns, pInfo, sizeof(ATLCOLUMNINFO)*g_cRequiredMetadataColumns);

		// Optional Column의 정보를 복사
		for(DBORDINAL i=0;i<cOptColumns;i++)
		{
			DBORDINAL j = g_cRequiredMetadataColumns;
			if(bAll)
				j += i; // 순서대로 모든 optional column 정보 복사
			else
			{	// DBID를 보고 optional column을 찾는다.
				while(j<g_cRequiredMetadataColumns+g_cOptionalMetadataColumns)
				{
					if(memcmp(&pInfo[j].columnid, rgOptColumns+i, sizeof(DBID))==0)
						break;
					j++;
				}
			}
			if(j==g_cRequiredMetadataColumns+g_cOptionalMetadataColumns) // 지원하지 않는 optional column
				return RaiseError(DB_E_BADCOLUMNID, 0, __uuidof(IColumnsRowset));
			memcpy(m_rgColumns+g_cRequiredMetadataColumns+i, pInfo+j, sizeof(ATLCOLUMNINFO));
			m_rgColumns[g_cRequiredMetadataColumns+i].iOrdinal = g_cRequiredMetadataColumns+i+1;
		}

		return S_OK;
	}

	DBSTATUS GetDBStatus(CSimpleRow *pRow, ATLCOLUMNINFO *pInfo)
	{
		ATLTRACE2(atlTraceDBProvider, 3, "CColumnsRowset::GetDBStatus\n");

		CColumnsRowsetRow &crrData = m_rgRowData[pRow->m_iRowset];

		//if (crrData.m_ulNumber == 0) //Bookmark
		//{
		//	switch(pInfo->iOrdinal)
		//	{
		//	case 2:
		//	case 3:
		//	case 5:
		//	case 6:
		//	case 8:
		//	case 9:
		//	case 11:
		//		return DBSTATUS_S_OK;
		//	}
		//} else
		{
			switch(pInfo->iOrdinal)
			{
				case 1: // DBCOLUMN_IDNAME
				case 3: // DBCOLUMN_PROPID
				case 7: // DBCOLUMN_TYPEINFO
					return DBSTATUS_S_ISNULL;
				case 9: // DBCOLUMN_PRECISION
					if(crrData.m_nPrecision==(USHORT)~0)
						return DBSTATUS_S_ISNULL;
					else
						return DBSTATUS_S_OK;
				case 10: // DBCOLUMN_SCALE
					if(crrData.m_nScale==-1)
						return DBSTATUS_S_ISNULL;
					else
						return DBSTATUS_S_OK;
				case 2: // DBCOLUMN_GUID
				case 4: // DBCOLUMN_NAME
				case 5: // DBCOLUMN_NUMBER
				case 6: // DBCOLUMN_TYPE
				case 8: // DBCOLUMN_COLUMNSIZE
				case 11: // DBCOLUMN_FLAGS
					return DBSTATUS_S_OK;
			}
		}

		// optional metadata columns
		if(memcmp(&pInfo->columnid, &DBCOLUMN_ISSEARCHABLE, sizeof(DBID))==0)
		{
			return DBSTATUS_S_OK;
		}
		if(memcmp(&pInfo->columnid, &DBCOLUMN_ISCASESENSITIVE, sizeof(DBID))==0)
		{
			return DBSTATUS_S_OK;
		}
		if(memcmp(&pInfo->columnid, &DBCOLUMN_DATETIMEPRECISION, sizeof(DBID))==0)
		{
			if(crrData.m_ulDateTimePrecision==(ULONG)~0)
				return DBSTATUS_S_ISNULL;
			else
				return DBSTATUS_S_OK;
		}
		if(memcmp(&pInfo->columnid, &DBCOLUMN_OCTETLENGTH, sizeof(DBID))==0)
		{
			if(crrData.m_ulCharOctetLength==(ULONG)~0)
				return DBSTATUS_S_ISNULL;
			else
				return DBSTATUS_S_OK;
		}

		// SQL의 경우 다음 항목은 값을 가지지 않는다.
		if(m_bIsTable)
		{
			if(memcmp(&pInfo->columnid, &DBCOLUMN_HASDEFAULT, sizeof(DBID))==0)
			{
				return DBSTATUS_S_OK;
			}
			if(memcmp(&pInfo->columnid, &DBCOLUMN_DEFAULTVALUE, sizeof(DBID))==0)
			{
				if(V_VT(&crrData.m_varDefault)==VT_EMPTY)
					return DBSTATUS_S_ISNULL;
				else
					return DBSTATUS_S_OK;
			}
			if(memcmp(&pInfo->columnid, &DBCOLUMN_BASECOLUMNNAME, sizeof(DBID))==0)
			{
				return DBSTATUS_S_OK;
			}
			if(memcmp(&pInfo->columnid, &DBCOLUMN_BASETABLENAME, sizeof(DBID))==0)
			{
				return DBSTATUS_S_OK;
			}
			if(memcmp(&pInfo->columnid, &DBCOLUMN_ISUNIQUE, sizeof(DBID))==0)
			{
				return DBSTATUS_S_OK;
			}
		}

		return DBSTATUS_S_ISNULL;
	}

	static ATLCOLUMNINFO *GetColumnInfo(CColumnsRowset *pv, DBORDINAL *pcCols)
	{
		*pcCols = pv->m_cColumns;
		return pv->m_rgColumns;
	}
};

template <class T, class CreatorClass>
class ATL_NO_VTABLE IColumnsRowsetImpl : public IColumnsRowset
{
public:
	STDMETHOD(GetAvailableColumns)(DBORDINAL *pcOptColumns, DBID **prgOptColumns)
	{
		ClearError();

		ATLTRACE(atlTraceDBProvider, 2, "IColumnsRowsetImpl::GetAvailableColumns\n");

		T *pT = (T *)this;

		if(pcOptColumns)
			*pcOptColumns = 0;
		if(prgOptColumns)
			*prgOptColumns = 0;
		if(pcOptColumns==NULL || prgOptColumns==NULL)
			return RaiseError(E_INVALIDARG, 0, __uuidof(IColumnsRowset));

		if(pT->m_bIsCommand)
		{
			if(pT->CheckCommandText(pT->GetUnknown())==DB_E_NOCOMMAND)
				return RaiseError(DB_E_NOCOMMAND, 0, __uuidof(IColumnsRowset));
			if(!pT->m_isPrepared)
				return RaiseError(DB_E_NOTPREPARED, 0, __uuidof(IColumnsRowset));
		}

__if_exists(T::m_nStatus)
{
		if(pT->m_nStatus==1)
			return E_UNEXPECTED;
}

		if(g_cOptionalMetadataColumns!=0)
		{
			DBORDINAL cCols;
			ATLCOLUMNINFO *pColInfo = CColumnsRowsetRow::GetColumnInfo(this, &cCols);
			ATLASSERT(cCols==g_cRequiredMetadataColumns+g_cOptionalMetadataColumns);

			*prgOptColumns = (DBID *)CoTaskMemAlloc(g_cOptionalMetadataColumns*sizeof(DBID));
			if(!*prgOptColumns)
				return RaiseError(E_OUTOFMEMORY, 0, __uuidof(IColumnsRowset));
			*pcOptColumns = g_cOptionalMetadataColumns;

			for(DBORDINAL i=0;i<g_cOptionalMetadataColumns;i++)
				memcpy(*prgOptColumns+i, &(pColInfo[g_cRequiredMetadataColumns+i].columnid), sizeof(DBID));
		}

		return S_OK;
	}

	STDMETHOD(GetColumnsRowset)(IUnknown *pUnkOuter, DBORDINAL cOptColumns,
								const DBID rgOptColumns[], REFIID riid, ULONG cPropertySets,
								DBPROPSET rgPropertySets[], IUnknown **ppColRowset)
	{
		ClearError();

		ATLTRACE(atlTraceDBProvider, 2, "IColumnsRowsetImpl::GetColumnsRowset\n");
		HRESULT hr, hrProps, hrExecute;

		T *pT = (T *)this;

		// 인자 검사
		if(ppColRowset==NULL || (cPropertySets>0 && rgPropertySets==NULL)
		   || (cOptColumns>0 && rgOptColumns==NULL))
			return RaiseError(E_INVALIDARG, 0, __uuidof(IColumnsRowset));
		*ppColRowset = NULL;

		for(ULONG i=0;i<cPropertySets;i++)
		{
			if(rgPropertySets[i].cProperties>0 && rgPropertySets[i].rgProperties==NULL)
				return RaiseError(E_INVALIDARG, 0, __uuidof(IColumnsRowset));
		
			for (ULONG j=0; j < rgPropertySets[i].cProperties; j++)
			{
				DBPROPOPTIONS option = rgPropertySets[i].rgProperties[j].dwOptions;
				if (option != DBPROPOPTIONS_REQUIRED &&
					option != DBPROPOPTIONS_OPTIONAL)
					return RaiseError(DB_E_ERRORSOCCURRED, 0, __uuidof(IColumnsRowset));
			}
		}

		if(pT->m_bIsCommand)
		{
			if(pT->CheckCommandText(pT->GetUnknown())==DB_E_NOCOMMAND)
				return RaiseError(DB_E_NOCOMMAND, 0, __uuidof(IColumnsRowset));
			if(!pT->m_isPrepared)
				return RaiseError(DB_E_NOTPREPARED, 0, __uuidof(IColumnsRowset));
		}

__if_exists(T::m_nStatus)
{
		if(pT->m_nStatus==1)
			return E_UNEXPECTED;
}

		if(pUnkOuter && !InlineIsEqualUnknown(riid))
			return RaiseError(DB_E_NOAGGREGATION, 0, __uuidof(IColumnsRowset));

		// Columns Rowset 객체 생성
		CComPolyObject<CColumnsRowset<CreatorClass> > *pPolyObj;
		hr = CComPolyObject<CColumnsRowset<CreatorClass> >::CreateInstance(pUnkOuter, &pPolyObj);
		if(FAILED(hr))
			return RaiseError(hr, 0, __uuidof(IColumnsRowset));

		// 생성된 COM 객체를 참조해서, 실패시 자동 해제하도록 한다.
		CComPtr<IUnknown> spAutoReleaseUnk;
		hr = pPolyObj->QueryInterface(&spAutoReleaseUnk);
		if(FAILED(hr))
		{
			delete pPolyObj; // 참조되지 않았기 때문에 수동으로 지운다.
			return RaiseError(hr, 0, __uuidof(IColumnsRowset));
		}

		// 객체를 초기화 한다.
		CColumnsRowset<CreatorClass> *pRowsetObj = &(pPolyObj->m_contained);
		hr = pRowsetObj->FInit();
		if(FAILED(hr))
			return RaiseError(hr, 0, __uuidof(IColumnsRowset));

		// set properties
		const GUID *ppGuid[1];
		ppGuid[0] = &DBPROPSET_ROWSET;

		hr = pRowsetObj->SetPropertiesArgChk(cPropertySets, rgPropertySets);
		if(FAILED(hr))
			return RaiseError(hr, 0, __uuidof(IColumnsRowset));

		hrProps = pRowsetObj->SetProperties(0, cPropertySets, rgPropertySets, 1, ppGuid, true);
		if(FAILED(hrProps))
			return RaiseError(hrProps, 0, __uuidof(IColumnsRowset));

		// Columns Rowset 객체의 site를 설정한다.
		CComPtr<IUnknown> spOuterUnk;
		((T *)this)->QueryInterface(__uuidof(IUnknown), (void **)&spOuterUnk);
		pRowsetObj->SetSite(spOuterUnk);

		// Check to make sure we set any 'post' properties based on the riid requested.
		if (FAILED(pRowsetObj->OnInterfaceRequested(riid)))
			return RaiseError(hr, 0, __uuidof(IColumnsRowset));

		int hReq = pT->GetRequestHandle();

		// 컬럼 메타 정보를 알아내기 위헤서는 테이블 이름이 필요하다.
		CW2A strTableName = pT->m_strTableName;
		char *szTableName = 0;

		szTableName = strTableName;
#if 0
		if (cci_is_updatable(hReq))
		{
			szTableName = strTableName;
		}
#endif		
		// Columns Rowset 객체의 내용을 채운다.
		int hConn;
		CComVariant var;
		HRESULT _hr = pT->GetPropValue(&DBPROPSET_ROWSET, DBPROP_BOOKMARKS, &var);
		if (FAILED(_hr)) return RaiseError(hr, 0, __uuidof(IColumnsRowset));
		
		pT->GetConnectionHandle(&hConn);
		hrExecute = pRowsetObj->Execute(hConn, hReq, szTableName, cOptColumns, rgOptColumns, V_BOOL(&var)==ATL_VARIANT_TRUE);
		if(FAILED(hrExecute))
			return hrExecute;

		// Columns Rowset 객체에서 riid로 요청된 interface를 구한다.
		if(InlineIsEqualGUID(riid, IID_NULL))
			return RaiseError(E_NOINTERFACE, 0, __uuidof(IColumnsRowset));
		hr = pPolyObj->QueryInterface(riid, (void  **)ppColRowset);
		if(FAILED(hr))
		{
			*ppColRowset = NULL;
			return RaiseError(hr, 0, __uuidof(IColumnsRowset));
		}

		return (hrProps == DB_S_ERRORSOCCURRED && hrExecute != DB_S_STOPLIMITREACHED) ? hrProps : hrExecute;
	}
};
