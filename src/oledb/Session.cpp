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
#include "DataSource.h"
#include "Session.h"
#include "Rowset.h"
#include "Row.h"
#include "util.h"
#include "type.h"
#include "ProviderInfo.h"
#include "Error.h"

CCUBRIDDataSource *CCUBRIDSession::GetDataSourcePtr()
{
	return CCUBRIDDataSource::GetDataSourcePtr(this);
}

CCUBRIDSession *CCUBRIDSession::GetSessionPtr(IObjectWithSite *pSite)
{
	CComPtr<IGetDataSource> spCom;
	HRESULT hr = pSite->GetSite(__uuidof(IGetDataSource), (void **)&spCom);
	// 제대로 프로그래밍 됐을때, 실패하는 경우가 있을까?
	ATLASSERT(SUCCEEDED(hr));
	// 굳이 오버헤드를 감수해가며 dynamic_cast를 쓸 필요는 없을 듯
	return static_cast<CCUBRIDSession *>((IGetDataSource *)spCom);
}

HRESULT CCUBRIDSession::RegisterTxnCallback(Util::ITxnCallback *pTxnCallback, bool bRegister)
{
	ATLASSERT(pTxnCallback);

	if(bRegister)
	{
		ATLASSERT(m_grpTxnCallbacks.Find(pTxnCallback)==NULL); // 이미 있는지 검사한다.
		m_grpTxnCallbacks.AddTail(pTxnCallback);
	}
	else
	{
		POSITION pos = m_grpTxnCallbacks.Find(pTxnCallback);
		if(pos!=NULL) // 없을 수도 있다(생성하다 실패한 경우)
			m_grpTxnCallbacks.RemoveAt(pos);
	}

	return S_OK;
}

HRESULT CCUBRIDSession::GetConnectionHandle(int *phConn)
{
	*phConn = 0;

	if(m_hConn<=0)
	{
		CComPtr<IDBProperties> spDBProps;
		HRESULT hr = GetDataSource(__uuidof(IDBProperties), (IUnknown **)&spDBProps);
		ATLASSERT(SUCCEEDED(hr)); // 제대로 프로그래밍 됐을때, 실패하는 경우가 있을까?

		hr = Util::Connect(spDBProps, &m_hConn);
		if(FAILED(hr)) return hr;

		EnterAutoCommitMode();
	}

	*phConn = m_hConn;
	return S_OK;
}

void CCUBRIDSession::FinalRelease() 
{
	ATLTRACE2(atlTraceDBProvider, 3, "CCUBRIDSession::FinalRelease\n");
	if(m_hConn>0)
		Util::Disconnect(&m_hConn);
}

HRESULT	CCUBRIDSession::IsValidValue(ULONG /*iCurSet*/, DBPROP* pDBProp)
{
	// 다른 속성은 없다.
	ATLASSERT(pDBProp->dwPropertyID==DBPROP_SESS_AUTOCOMMITISOLEVELS);

	HRESULT hr = SetCASCCIIsoLevel(V_I4(&pDBProp->vValue), true);
	if(FAILED(hr))
		return S_FALSE;

	return S_OK;
}

HRESULT CCUBRIDSession::OnPropertyChanged(ULONG /*iCurSet*/, DBPROP *pDBProp)
{
	// 다른 속성은 없다.
	ATLASSERT(pDBProp->dwPropertyID==DBPROP_SESS_AUTOCOMMITISOLEVELS);

	if(m_bAutoCommit)
	{
		HRESULT hr = SetCASCCIIsoLevel(V_I4(&pDBProp->vValue));
		if(FAILED(hr))
			return DB_E_ERRORSOCCURRED;
	}

	return S_OK;
}

STDMETHODIMP CCUBRIDSession::OpenRowset(IUnknown *pUnk, DBID *pTID, DBID *pInID, REFIID riid,
									 ULONG cSets, DBPROPSET rgSets[], IUnknown **ppRowset)
{
	ATLTRACE2(atlTraceDBProvider, 2, "CCUBRIDSession::OpenRowset");
	HRESULT hr = S_OK, _hr = S_OK;
	CComVariant var;
	char prepareFlag = 0;
	bool bCreateRow = false;

	if(ppRowset)
		*ppRowset = NULL;

	if (pUnk && riid != IID_IUnknown)
		return RaiseError(DB_E_NOAGGREGATION, 0, __uuidof(IOpenRowset));

	if (pInID != NULL)
		return RaiseError(DB_E_NOINDEX, 0, _uuidof(IOpenRowset));

	if (!pTID)
		return RaiseError(E_INVALIDARG, 0, __uuidof(IOpenRowset));

	//NAME만을 지원함
	if (pTID->eKind != DBKIND_NAME &&
		pTID->eKind != DBKIND_GUID_NAME &&
		pTID->eKind != DBKIND_PGUID_NAME)
		return RaiseError(DB_E_NOTABLE, 0, __uuidof(IOpenRowset));

	//DBPROP_IRow 처리
	if (rgSets && InlineIsEqualGUID(rgSets->guidPropertySet, DBPROPSET_ROWSET))
	{
		for (ULONG i = 0; i < rgSets->cProperties; i++)
		{
			if(rgSets->rgProperties) // else면 INVALIDARG지만 일단은 패스. 후에 에러 반환
			{
				if (rgSets->rgProperties[i].dwPropertyID == DBPROP_IRow &&
					V_BOOL(&rgSets->rgProperties[i].vValue) == ATL_VARIANT_TRUE )
					bCreateRow = true;
			//	DBPROPSTATUS* status = &rgSets->rgProperties[i].dwStatus;
			//	DBPROPOPTIONS options = rgSets->rgProperties[i].dwOptions;

			//	*status = DBPROPSTATUS_OK;
			//	if (rgSets->rgProperties[i].dwPropertyID == DBPROP_IRow)
			//	{
			//		//DBPROP_IRow가 세팅되고 riid가 IID_IRow가 아니면
			//		//DBPROPSTATUS_CONFLICTING status를 세팅한다.
			//		if (riid == IID_IRow || riid == IID_IUnknown)
			//		{
			//			if (V_BOOL(&rgSets->rgProperties[i].vValue) == ATL_VARIANT_TRUE)
			//				bCreateRow = true;
			//		} else
			//		{
			//			if (riid == IID_IRowset || riid == IID_IRowsetChange || riid == IID_IRowsetInfo)
			//			{
			//				*status = DBPROPSTATUS_CONFLICTING;
			//			} else
			//			{
			//				*status = DBPROPSTATUS_NOTSUPPORTED;
			//			}

			//			if (options == DBPROPOPTIONS_REQUIRED)
			//				hr = DB_E_ERRORSOCCURRED;
			//			else if (options == DBPROPOPTIONS_OPTIONAL)
			//				hr = DB_S_ERRORSOCCURRED;
			//		}
			//	} else if (rgSets->rgProperties[i].dwPropertyID == DBPROP_ISupportErrorInfo)
			//	{
			//		*status = DBPROPSTATUS_NOTSETTABLE;

			//		if (options == DBPROPOPTIONS_REQUIRED)
			//			hr = DB_E_ERRORSOCCURRED;
			//		else if (options == DBPROPOPTIONS_OPTIONAL)
			//			hr = DB_S_ERRORSOCCURRED;
			//		else
			//			*status = DBPROPSTATUS_BADOPTION;
			//	} else if (rgSets->rgProperties[i].dwPropertyID == DBPROP_IRowSchemaChange ||
			//				rgSets->rgProperties[i].dwPropertyID == DBPROP_IRowChange ||
			//				rgSets->rgProperties[i].dwPropertyID == DBPROP_ICreateRow ||
			//				rgSets->rgProperties[i].dwPropertyID == DBPROP_IGetSourceRow)
			//	{
			//		*status = DBPROPSTATUS_NOTSUPPORTED;

			//		if (options == DBPROPOPTIONS_REQUIRED)
			//			hr = DB_E_ERRORSOCCURRED;
			//		else if (options == DBPROPOPTIONS_OPTIONAL)
			//			hr = DB_S_ERRORSOCCURRED;
			//		else
			//			*status = DBPROPSTATUS_BADOPTION;
			//	}
			}
		}
		//if (FAILED(hr)) return hr;
	}

	//prepare시 사용할 flag를 결정한다.
	//if (Util::RequestedRIIDNeedsOID(riid))
	//	prepareFlag |= CCI_PREPARE_INCLUDE_OID;
	if (Util::RequestedRIIDNeedsUpdatability(riid))
		prepareFlag |= (CCI_PREPARE_INCLUDE_OID | CCI_PREPARE_UPDATABLE);
	if (cSets > 0 && rgSets)
	{
		//if (Util::CheckOIDFromProperties(cSets, rgSets))
		//	prepareFlag |= CCI_PREPARE_INCLUDE_OID;
		if (Util::CheckUpdatabilityFromProperties(cSets, rgSets))
			prepareFlag |= (CCI_PREPARE_INCLUDE_OID | CCI_PREPARE_UPDATABLE);
	}
	
	//prepareFlag = CCI_PREPARE_INCLUDE_OID | CCI_PREPARE_UPDATABLE;

	HRESULT originalHRESULT = hr;
	if (riid == IID_IRow || bCreateRow)
	{
		int hConn;
		{
			_hr = GetConnectionHandle(&hConn);
			if(FAILED(_hr)) return E_FAIL;
		}
		int hReq, cResult;
		{
			_hr = Util::OpenTable(hConn, pTID->uName.pwszName, &hReq, &cResult, prepareFlag);
			if(FAILED(_hr)) return _hr;
		}

		hr = _hr;
		if(cResult==0) return DB_E_NOTFOUND;

		CComPolyObject<CCUBRIDRow> *pRow;
		_hr = CComPolyObject<CCUBRIDRow>::CreateInstance(pUnk, &pRow);
		if(FAILED(_hr))
			return _hr;

		// 생성된 COM 객체를 참조해서, 실패시 자동 해제하도록 한다.
		CComPtr<IUnknown> spUnk;
		_hr = pRow->QueryInterface(&spUnk);
		if(FAILED(_hr))
		{
			delete pRow; // 참조되지 않았기 때문에 수동으로 지운다.
			return _hr;
		}

		// Session object의 IUnknown을 Row의 Site로 설정한다.
		CComPtr<IUnknown> spOuterUnk;
		QueryInterface(__uuidof(IUnknown), (void **)&spOuterUnk);
		pRow->m_contained.SetSite(spOuterUnk, CCUBRIDRow::Type::FromSession);

		//Row object initialize
		_hr = pRow->m_contained.Initialize(hReq);
		if(FAILED(_hr))
		{
			return _hr;
		}
		if (cResult > 1) {
			if (originalHRESULT != DB_S_ERRORSOCCURRED)
				hr = DB_S_NOTSINGLETON;
		}

		//Selection 결과가 1 이상일 때
		if (cResult > 1)
		{
			if (hr != DB_S_ERRORSOCCURRED)
				hr = DB_S_NOTSINGLETON;
		}

		//생성된 Row 객체의 IRow 인터페이스 반환
		_hr = pRow->QueryInterface(riid, (void **)ppRowset);
		if(FAILED(_hr))
		{
			return _hr;
		}
	} else
	{
		CComPtr<IUnknown> spRowset;
		CCUBRIDRowset* pRowset = 0;
		_hr = CreateRowset(pUnk, pTID, pInID, riid, cSets, rgSets,
									(ppRowset?&spRowset:NULL), pRowset);
		if(FAILED(_hr)) return _hr;

		// Rowset Object를 생성하지 않았다.
		if(!ppRowset) return _hr;

		hr = _hr;

		_hr = pRowset->InitFromSession(pTID, prepareFlag);
		if(FAILED(_hr)) return _hr;

		if (ppRowset) *ppRowset = spRowset.Detach();	
	}

	if (FAILED(originalHRESULT))
		return originalHRESULT;
	else if (originalHRESULT == DB_S_ERRORSOCCURRED)
		return originalHRESULT;

	return hr; // CreateRowset이 DB_S_ERRORSOCCURRED를 반환할 수도 있으니까 hr을 써야 한다.
}

// 각 Schema에 대해 지원하는 restriction을 지정한다.
void CCUBRIDSession::SetRestrictions(ULONG cRestrictions, GUID* rguidSchema, ULONG* rgRestrictions)
{
	ATLTRACE2(atlTraceDBProvider, 2, "CCUBRIDSession::SetRestrictions\n");

	for (ULONG l=0; l<cRestrictions; l++)
	{
		if (InlineIsEqualGUID(rguidSchema[l], DBSCHEMA_TABLES))
			rgRestrictions[l] = 0x0c; // TABLE_NAME | TABLE_TYPE
		else if (InlineIsEqualGUID(rguidSchema[l], DBSCHEMA_COLUMNS))
			rgRestrictions[l] = 0x0c; // TABLE_NAME | COLUMN_NAME
		else if (InlineIsEqualGUID(rguidSchema[l], DBSCHEMA_PROVIDER_TYPES))
			rgRestrictions[l] = 0x00; // nothing
		else if (InlineIsEqualGUID(rguidSchema[l], DBSCHEMA_TABLE_PRIVILEGES))
			rgRestrictions[l] = 0x04; // TABLE_NAME
		else if (InlineIsEqualGUID(rguidSchema[l], DBSCHEMA_COLUMN_PRIVILEGES))
			rgRestrictions[l] = 0x0c; // TABLE_NAME | COLUMN_NAME
		else if (InlineIsEqualGUID(rguidSchema[l], DBSCHEMA_TABLE_CONSTRAINTS))
			rgRestrictions[l] = 0x60; // TABLE_NAME | CONSTRAINT_TYPE
		else if (InlineIsEqualGUID(rguidSchema[l], DBSCHEMA_TABLES_INFO))
			rgRestrictions[l] = 0x0c; // TABLE_NAME | TABLE_TYPE
		else if (InlineIsEqualGUID(rguidSchema[l], DBSCHEMA_STATISTICS))
			rgRestrictions[l] = 0x04; // TABLE_NAME
		else if (InlineIsEqualGUID(rguidSchema[l], DBSCHEMA_INDEXES))
			rgRestrictions[l] = 0x14; // TABLE_NAME | INDEX_NAME
		else if (InlineIsEqualGUID(rguidSchema[l], DBSCHEMA_VIEW_COLUMN_USAGE))
			rgRestrictions[l] = 0x04; // VIEW_NAME
		else if (InlineIsEqualGUID(rguidSchema[l], DBSCHEMA_VIEWS))
			rgRestrictions[l] = 0x04; // TABLE_NAME
	}
}

/* atldb.h의 루틴은 기본 schema rowset 만 지원한다.
 * 새로운 schema 삽입이 힘든 구조라 전체를 복사해왔다.
 */
// 각 restriction의 타입을 검사한다.
HRESULT CCUBRIDSession::CheckRestrictions(REFGUID rguidSchema, ULONG cRestrictions, 
			const VARIANT rgRestrictions[])
{
	// Use this function to help check the validity of restrictions
	// against a schema rowset.
	const ULONG ulSchemaRowset = 11;
	const GUID *rgGUIDSchema[ulSchemaRowset] = {
		&DBSCHEMA_TABLES,
		&DBSCHEMA_COLUMNS,
		&DBSCHEMA_PROVIDER_TYPES,
		&DBSCHEMA_TABLE_PRIVILEGES,
		&DBSCHEMA_COLUMN_PRIVILEGES,
		&DBSCHEMA_TABLE_CONSTRAINTS,
		&DBSCHEMA_TABLES_INFO,
		&DBSCHEMA_STATISTICS,
		&DBSCHEMA_INDEXES,
		&DBSCHEMA_VIEW_COLUMN_USAGE,
		&DBSCHEMA_VIEWS,
	};
	const VARTYPE rgRestrictionTypes[ulSchemaRowset][7] = {
		{ VT_BSTR, VT_BSTR, VT_BSTR,  VT_BSTR,  VT_EMPTY, VT_EMPTY, VT_EMPTY },	// DBSCHEMA_TABLES
		{ VT_BSTR, VT_BSTR, VT_BSTR,  VT_BSTR,  VT_EMPTY, VT_EMPTY, VT_EMPTY },	// DBSCHEMA_COLUMNS
		{ VT_UI2,  VT_BOOL, VT_EMPTY, VT_EMPTY, VT_EMPTY, VT_EMPTY, VT_EMPTY },	// DBSCHEMA_PROVIDER_TYPES
		{ VT_BSTR, VT_BSTR, VT_BSTR,  VT_BSTR,  VT_BSTR,  VT_EMPTY, VT_EMPTY },	// DBSCHEMA_TABLE_PRIVILEGES
		{ VT_BSTR, VT_BSTR, VT_BSTR,  VT_BSTR,  VT_BSTR,  VT_BSTR,  VT_EMPTY },	// DBSCHEMA_COLUMN_PRIVILEGES
		{ VT_BSTR, VT_BSTR, VT_BSTR,  VT_BSTR,  VT_BSTR,  VT_BSTR,  VT_BSTR  },	// DBSCHEMA_TABLE_CONSTRAINTS
		{ VT_BSTR, VT_BSTR, VT_BSTR,  VT_BSTR,  VT_EMPTY, VT_EMPTY, VT_EMPTY },	// DBSCHEMA_TABLES_INFO
		{ VT_BSTR, VT_BSTR, VT_BSTR,  VT_EMPTY, VT_EMPTY, VT_EMPTY, VT_EMPTY },	// DBSCHEMA_STATISTICS
		{ VT_BSTR, VT_BSTR, VT_BSTR,  VT_BSTR,  VT_BSTR,  VT_EMPTY, VT_EMPTY },	// DBSCHEMA_INDEXES
		{ VT_BSTR, VT_BSTR, VT_BSTR,  VT_EMPTY, VT_EMPTY, VT_EMPTY, VT_EMPTY },	// DBSCHEMA_VIEW_COLUMN_USAGE
		{ VT_BSTR, VT_BSTR, VT_BSTR,  VT_EMPTY, VT_EMPTY, VT_EMPTY, VT_EMPTY },	// DBSCHEMA_VIEWS
	};
	const ULONG ulMaxRestrictions[ulSchemaRowset] = { 4, 4, 2, 5, 6, 7, 4, 3, 5, 3, 3 };
	ULONG ulType;
	for(ulType=0;ulType<ulSchemaRowset;ulType++)
	{
		if(InlineIsEqualGUID(rguidSchema, *rgGUIDSchema[ulType]))
			break;
	}

	if (ulType < ulSchemaRowset)	// I.E. we found one of our supported rowsets
	{
		ULONG ulCurrentRestrictions = 0x00;
		ULONG ulCurrentMask = 0x01;

		// Ask the provider's session object for its list of restrictions
		SetRestrictions(1, (GUID*)&rguidSchema, &ulCurrentRestrictions);

		// We allow VT_EMPTY through in case the consumer wanted to ignore this
		// restriction (basically a way to pass in 'NULL').
		if (cRestrictions > ulMaxRestrictions[ulType])
			return E_INVALIDARG;

		for (ULONG ulRes = 0; ulRes < cRestrictions; ulRes++)
		{
			// Check for obviously invalid types
			if (rgRestrictions[ulRes].vt != rgRestrictionTypes[ulType][ulRes] &&
				rgRestrictions[ulRes].vt != DBTYPE_EMPTY &&
				rgRestrictions[ulRes].vt != VT_NULL )
				return E_INVALIDARG;

			// Check for restrictions the provider doesn't support.
			if (!(ulCurrentMask & ulCurrentRestrictions) &&
				(rgRestrictions[ulRes].vt != DBTYPE_EMPTY))
				return DB_E_NOTSUPPORTED;

			ulCurrentMask <<= 1;		// Increase mask by * 2;
		}
	}

	return S_OK;
}

STDMETHODIMP CCUBRIDSession::CreateTable(IUnknown *pUnkOuter,
            DBID *pTableID,
            DBORDINAL cColumnDescs,
            const DBCOLUMNDESC rgColumnDescs[],
            REFIID riid,
            ULONG cPropertySets,
            DBPROPSET rgPropertySets[],
            DBID **ppTableID,
            IUnknown **ppRowset)
{
	HRESULT hr = S_OK;
	CComBSTR	tableName, temp_table_name;
	CComBSTR	strQuery;
	int			hConn, hReq, rc, len;
	T_CCI_ERROR	error;
	
	if (ppTableID)
		*ppTableID = NULL;

	ClearError();

	if ((!pTableID && !ppTableID) ||
		(!cColumnDescs) || //컬럼 없는 테이블 지원안함
		(!rgColumnDescs) ||
		(cPropertySets && !rgPropertySets))
		return E_INVALIDARG;

	if (pUnkOuter && riid != IID_IUnknown)
		return DB_E_NOAGGREGATION;

	//테이블 프로퍼티 관련
	for (ULONG i = 0; i < cPropertySets; i++)
	{
		if (rgPropertySets[i].guidPropertySet == DBPROPSET_ROWSET)
		{
			if (rgPropertySets[i].cProperties && !rgPropertySets[i].rgProperties)
				return E_INVALIDARG;

			for (ULONG j = 0; j < rgPropertySets[i].cProperties; j++)
			{
				DBPROPOPTIONS option = rgPropertySets[i].rgProperties[j].dwOptions;
				DBPROPID propID = rgPropertySets[i].rgProperties[j].dwPropertyID;
				DBPROPSTATUS* status = &rgPropertySets[i].rgProperties[j].dwStatus;
				VARIANT value = rgPropertySets[i].rgProperties[j].vValue;

				if (propID == DBPROP_MAYWRITECOLUMN)
				{
					*status = DBPROPSTATUS_NOTSUPPORTED;
					if (option == DBPROPOPTIONS_REQUIRED)
							return DB_E_ERRORSOCCURRED;
						else
							hr = DB_S_ERRORSOCCURRED;
				}
			}
		}
	}
	
	HRESULT _hr = GetConnectionHandle(&hConn);
	if (FAILED(_hr))
	{
		return E_FAIL;
	}

	if (pTableID)
	{
		if (IsValidDBID(pTableID) != S_OK)
			return DB_E_BADTABLEID;
		if (CompareDBIDs(pTableID, &DB_NULLID) == S_OK)
			return DB_E_BADTABLEID;
		if (pTableID->eKind != DBKIND_NAME)
			return DB_E_BADTABLEID;
		if (!pTableID->uName.pwszName || !wcslen(pTableID->uName.pwszName))
			return DB_E_BADTABLEID;
		
		//quoted table name인 경우 quotation을 벗긴다.
		temp_table_name.Append(pTableID->uName.pwszName);
		len = temp_table_name.Length();
		if (temp_table_name.m_str[0] == '\"' && temp_table_name.m_str[len - 1] == '\"')
		{
			temp_table_name.Empty();
			temp_table_name.Append(pTableID->uName.pwszName + 1);
			temp_table_name.m_str[len - 2] = '\0';
		}

		//Maximum Table Length를 넘는지 체크
		if (wcslen(temp_table_name) > ProvInfo::LiteralInfos[8].cchMaxLen)
			return DB_E_BADTABLEID;
		//Invalid character를 포함하는지 체크
		if (wcspbrk(temp_table_name, ProvInfo::LiteralInfos[8].pwszInvalidChars))
			return DB_E_BADTABLEID;

		//테이블 이름 중복검사
		HRESULT hr = Util::DoesTableExist(hConn, CW2A(temp_table_name));
		if(FAILED(hr))
			return hr;
		if(hr==S_OK)
			return DB_E_DUPLICATETABLEID;

		tableName = pTableID->uName.pwszName;
	}
	else
	{
		CComBSTR strTableName;
		Util::GetUniqueTableName(strTableName);
		tableName = strTableName;
	}

	//SQL문 생성 시작
	strQuery.Append(L"CREATE TABLE ");
	strQuery.Append(tableName);
	strQuery.Append(L" (");

	//cColumnDescs 갯수 만큼 컬럼에 대한 SQL 문을 생성하고 이를 Query string에 이어 붙인다.
	for (DBORDINAL i = 0; i < cColumnDescs; i++)
	{
		CComBSTR queryLine;
		CComBSTR typeDef;
		HRESULT _hr = S_OK;

		if (IsValidDBID(&rgColumnDescs[i].dbcid) != S_OK)
			return DB_E_BADCOLUMNID;
		if (CompareDBIDs(&rgColumnDescs[i].dbcid, &DB_NULLID) == S_OK)
			return DB_E_BADCOLUMNID;
		if (rgColumnDescs[i].dbcid.eKind != DBKIND_NAME)
			return DB_E_BADCOLUMNID;
		if (rgColumnDescs[i].dbcid.eKind == DBKIND_NAME && 
			(!rgColumnDescs[i].dbcid.uName.pwszName || !wcslen(rgColumnDescs[i].dbcid.uName.pwszName))) 
			return DB_E_BADCOLUMNID;
		
		//Maximum Column Length를 넘는지 체크
		if (wcslen(rgColumnDescs[i].dbcid.uName.pwszName) > ProvInfo::LiteralInfos[2].cchMaxLen)
			return DB_E_BADCOLUMNID;
		
		//Invalid character를 포함하는지 체크
		if (wcspbrk(rgColumnDescs[i].dbcid.uName.pwszName, ProvInfo::LiteralInfos[2].pwszInvalidChars))
			return DB_E_BADCOLUMNID;

		//컬럼 이름 중복 체크
		for (DBORDINAL j = i+1; j < cColumnDescs; j++)
		{
			if (rgColumnDescs[j].dbcid.uName.pwszName &&
				!wcscmp(rgColumnDescs[i].dbcid.uName.pwszName, rgColumnDescs[j].dbcid.uName.pwszName))
				return DB_E_DUPLICATECOLUMNID;
		}

		queryLine.Append(rgColumnDescs[i].dbcid.uName.pwszName);
		queryLine.Append(L" ");
		
		_hr = Type::GetColumnDefinitionString(rgColumnDescs[i], typeDef);
		if (FAILED(_hr))
			return _hr;
		if (_hr != S_OK)
			hr = _hr;

		queryLine.Append(typeDef);

		strQuery.Append(queryLine);
		if (i+1 != cColumnDescs)
			strQuery.Append(L",");
	}
	//SQL문 생성 종료
	strQuery.Append(L")");

	char flag = 0;
	
	if (Util::RequestedRIIDNeedsUpdatability(riid))
		flag |= (CCI_PREPARE_INCLUDE_OID | CCI_PREPARE_UPDATABLE);
	for (ULONG i = 0; i < cPropertySets; i++)
	{
		if (rgPropertySets[i].guidPropertySet == DBPROPSET_ROWSET)
		{
			ULONG cProp = rgPropertySets[i].cProperties;
			for (ULONG j = 0; j < cProp; j++)
			{
				DBPROPID propID = rgPropertySets[i].rgProperties[j].dwPropertyID;
				if (propID == DBPROP_IRowsetChange ||
					propID == DBPROP_IRowsetUpdate ||
					propID == DBPROP_IRow ||
					propID == DBPROP_IGetRow ||
					propID == DBPROP_IGetSession ||
					propID == DBPROP_OTHERUPDATEDELETE)
				{
					flag |= (CCI_PREPARE_INCLUDE_OID | CCI_PREPARE_UPDATABLE);
					break;
				}
			}

		}
	}
	
	hReq = cci_prepare(hConn, CW2A(strQuery), flag, &error);
	if (hReq < 0)
	{
		ATLTRACE2(atlTraceDBProvider, 3, "%s\n", error.err_msg);
		return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}
	rc = cci_execute(hReq, CCI_EXEC_QUERY_ALL, 0, &error);
	if (rc < 0)
	{
		ATLTRACE2(atlTraceDBProvider, 3, "%s\n", error.err_msg);
		return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}
	AutoCommit(0);
	
	DBID tmp;
	tmp.eKind = DBKIND_NAME;
	tmp.uName.pwszName = tableName;

	//ppRowset이 NULL이 아닌 경우 Rowset을 연다.
	if (ppRowset)
	{
		HRESULT _hr = OpenRowset(pUnkOuter, &tmp, NULL, riid, cPropertySets, rgPropertySets, ppRowset);
		if (FAILED(_hr))
		{
			return _hr;
		}

		//위에서 DB_S_ERRORSOCCURRED등이 발생하지 않았었으면 hr을 다시 세팅한다.
		if (hr == S_OK) hr = _hr;
	}

	//ppTableID에 생성된 테이블 이름을 넘겨준다.
	if(ppTableID)
	{
		*ppTableID = (DBID *)CoTaskMemAlloc(sizeof(DBID));
		CopyDBIDs(*ppTableID, &tmp);
	}

	return hr;
}

STDMETHODIMP CCUBRIDSession::DropTable(DBID *pTableID)
{
	HRESULT		hr = S_OK;
	int			hConn, hReq, rc, len;
	CComBSTR	strQuery = NULL;
	CComBSTR	table_name;
	T_CCI_ERROR	error;
	CDBIDOps op;

	ClearError();

	if (!pTableID)
		return E_INVALIDARG;

	if (op.IsValidDBID(pTableID) != S_OK)
			return DB_E_NOTABLE;
	if (op.CompareDBIDs(pTableID, &DB_NULLID) == S_OK)
		return DB_E_NOTABLE;
	if (pTableID->eKind != DBKIND_NAME)
		return DB_E_NOTABLE;
	if (!pTableID->uName.pwszName || !wcslen(pTableID->uName.pwszName))
		return DB_E_NOTABLE;

	//quoted table name인 경우 quotation을 삭제한다.
	table_name.Append(pTableID->uName.pwszName);
	len = table_name.Length();
	if (table_name.m_str[0] == '\"' && table_name.m_str[len - 1] == '\"')
	{
		table_name.Empty();
		table_name.Append(pTableID->uName.pwszName + 1);
		table_name.m_str[len - 2] = '\0';
	}

	//Maximum Table Length를 넘는지 체크
	if (wcslen(table_name) > ProvInfo::LiteralInfos[8].cchMaxLen)
		return DB_E_NOTABLE;

	//Invalid character를 포함하는지 체크
	if (wcspbrk(table_name, ProvInfo::LiteralInfos[8].pwszInvalidChars))
		return DB_E_NOTABLE;
	
	hr = GetConnectionHandle(&hConn);

	//테이블 이름이 있는지 검사
	hr = Util::DoesTableExist(hConn, CW2A(table_name));
	if(FAILED(hr))
		return hr;
	if(hr==S_FALSE)
		return DB_E_NOTABLE;

	strQuery.Append(L"DROP TABLE ");
	strQuery.Append(pTableID->uName.pwszName);

	hReq = cci_prepare(hConn, CW2A(strQuery), 0, &error);
	if (hReq < 0)
	{
		return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}
	rc = cci_execute(hReq, CCI_EXEC_QUERY_ALL, 0, &error);
	if (rc < 0)
	{
		return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}
	AutoCommit(0);

	return hr;
}

STDMETHODIMP CCUBRIDSession::AddColumn(DBID *pTableID, DBCOLUMNDESC *pColumnDesc, DBID **ppColumnID)
{
	HRESULT			hr = S_OK;
	int				hConn, hReq, rc, len;
	CComBSTR		strQuery, colDef, table_name, column_name;
	T_CCI_ERROR		error;
	
	ClearError();

	if ((!pTableID || !pColumnDesc) ||
		(pColumnDesc->cPropertySets > 0 && !pColumnDesc->rgPropertySets))
		return E_INVALIDARG;

	if (IsValidDBID(pTableID) != S_OK)
		return DB_E_NOTABLE;
	if (CompareDBIDs(pTableID, &DB_NULLID) == S_OK)
		return DB_E_NOTABLE;
	if (pTableID->eKind != DBKIND_NAME)
		return DB_E_NOTABLE;
	if (!pTableID->uName.pwszName || !wcslen(pTableID->uName.pwszName))
		return DB_E_NOTABLE;

	//quoted table name인 경우 quotation을 삭제한다.
	table_name.Append(pTableID->uName.pwszName);
	len = table_name.Length();
	if (table_name.m_str[0] == '\"' && table_name.m_str[len - 1] == '\"')
	{
		table_name.Empty();
		table_name.Append(pTableID->uName.pwszName + 1);
		table_name.m_str[len - 2] = '\0';
	}

	//Maximum Table Length를 넘는지 체크
	if (wcslen(table_name) > ProvInfo::LiteralInfos[8].cchMaxLen)
		return DB_E_NOTABLE;
	//Invalid character를 포함하는지 체크
	if (wcspbrk(table_name, ProvInfo::LiteralInfos[8].pwszInvalidChars))
		return DB_E_NOTABLE;

	hr = GetConnectionHandle(&hConn);
	if (FAILED(hr))
	{
		return E_FAIL;
	}

	{
		//테이블 이름이 있는지 검사
		hReq = cci_schema_info(hConn, CCI_SCH_CLASS, CW2A(table_name),
							NULL, 0, &error);
		if(hReq<0)
		{
			ATLTRACE2("cci_schema_info fail\n");
			return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
		}
		rc = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &error);
		if(rc==CCI_ER_NO_MORE_DATA)
			return DB_E_NOTABLE;
		else if (rc<0) return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
		cci_close_req_handle(hReq);

		//컬럼 이름이 있는지 검사
		hReq = cci_schema_info(hConn, CCI_SCH_ATTRIBUTE, CW2A(table_name),
							CW2A(pColumnDesc->dbcid.uName.pwszName), 0, &error);
		if(hReq<0)
		{
			ATLTRACE2("cci_schema_info fail\n");
			return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
		}
		rc = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &error);
		if(rc >= 0) //modified 2003.07.09
			return DB_E_DUPLICATECOLUMNID;
		
		cci_close_req_handle(hReq);
	}
	
	strQuery.Append(L"ALTER ");
	strQuery.Append(pTableID->uName.pwszName);
	strQuery.Append(L" ADD ATTRIBUTE ");
	
	if (IsValidDBID(&pColumnDesc->dbcid) != S_OK)
		return DB_E_BADCOLUMNID;
	if (CompareDBIDs(&pColumnDesc->dbcid, &DB_NULLID) == S_OK)
		return DB_E_BADCOLUMNID;
	if (pColumnDesc->dbcid.eKind != DBKIND_NAME)
		return DB_E_BADCOLUMNID;
	if (!pColumnDesc->dbcid.uName.pwszName || !wcslen(pColumnDesc->dbcid.uName.pwszName))
		return DB_E_BADCOLUMNID;
		
	column_name.Append(pColumnDesc->dbcid.uName.pwszName);

	//Maximum Column Length를 넘는지 체크
	if (wcslen(pColumnDesc->dbcid.uName.pwszName) > ProvInfo::LiteralInfos[2].cchMaxLen)
		return DB_E_BADCOLUMNID;
	//Invalid character를 포함하는지 체크
	if (wcspbrk(pColumnDesc->dbcid.uName.pwszName, ProvInfo::LiteralInfos[2].pwszInvalidChars))
		return DB_E_BADCOLUMNID;

	strQuery.Append(column_name);
	strQuery.Append(L" ");
	hr = Type::GetColumnDefinitionString(*pColumnDesc, colDef);
	if (FAILED(hr))
		return hr;
	strQuery.Append(colDef);
	
	hReq = cci_prepare(hConn, CW2A(strQuery), 0, &error);
	if (hReq < 0)
	{
		return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}
	
	rc = cci_execute(hReq, 0, 0, &error);
	if (rc < 0)
	{
		return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}

	AutoCommit(0);

	//ppColumnID에 생성된컬럼 이름을 넘겨준다.
	if(ppColumnID)
	{
		*ppColumnID = (DBID *)CoTaskMemAlloc(sizeof(DBID));
		DBID colID;
		colID.eKind = DBKIND_NAME;
		colID.uName.pwszName = column_name;
		CopyDBIDs(*ppColumnID, &colID);
	}

	return hr;
}

STDMETHODIMP CCUBRIDSession::DropColumn(DBID *pTableID, DBID *pColumnID)
{
	HRESULT			hr = S_OK;
	int				hConn, hReq, rc, len;
	CComBSTR		strQuery = NULL, table_name;
	T_CCI_ERROR		error;
	CDBIDOps op;

	ClearError();

	if (!pTableID || !pColumnID)
		return E_INVALIDARG;

	if (op.IsValidDBID(pTableID) != S_OK)
		return DB_E_NOTABLE;
	if (op.CompareDBIDs(pTableID, &DB_NULLID) == S_OK)
		return DB_E_NOTABLE;
	if (pTableID->eKind != DBKIND_NAME)
		return DB_E_NOTABLE;
	if (!pTableID->uName.pwszName || !wcslen(pTableID->uName.pwszName))
		return DB_E_NOTABLE;
	
	if (op.IsValidDBID(pColumnID) != S_OK)
		return DB_E_NOCOLUMN;
	if (op.CompareDBIDs(pColumnID, &DB_NULLID) == S_OK)
		return DB_E_NOCOLUMN;
	if (pColumnID->eKind != DBKIND_NAME)
		return DB_E_NOCOLUMN;
	if (!pColumnID->uName.pwszName || !wcslen(pColumnID->uName.pwszName))
		return DB_E_NOCOLUMN;

	//quoted table name인 경우 quotation을 삭제한다.
	table_name.Append(pTableID->uName.pwszName);
	len = table_name.Length();
	if (table_name.m_str[0] == '\"' && table_name.m_str[len - 1] == '\"')
	{
		table_name.Empty();
		table_name.Append(pTableID->uName.pwszName + 1);
		table_name.m_str[len - 2] = '\0';
	}

	//Maximum Table Length를 넘는지 체크
	if (wcslen(table_name) > ProvInfo::LiteralInfos[8].cchMaxLen)
		return DB_E_NOTABLE;
	//Table이 Invalid character를 포함하는지 체크
	if (wcspbrk(table_name, ProvInfo::LiteralInfos[8].pwszInvalidChars))
		return DB_E_NOTABLE;

	//Maximum Column Length를 넘는지 체크
	if (wcslen(pColumnID->uName.pwszName) > ProvInfo::LiteralInfos[2].cchMaxLen)
		return DB_E_NOCOLUMN;
	//Column이 Invalid character를 포함하는지 체크
	if (wcspbrk(pColumnID->uName.pwszName, ProvInfo::LiteralInfos[2].pwszInvalidChars))
		return DB_E_NOCOLUMN;

	hr = GetConnectionHandle(&hConn);
	if (FAILED(hr))
	{
		return E_FAIL;
	}

	//테이블 이름이 있는지 검사
	hReq = cci_schema_info(hConn, CCI_SCH_CLASS, CW2A(table_name),
						NULL, CCI_CLASS_NAME_PATTERN_MATCH, &error);
	if(hReq<0)
	{
		ATLTRACE2("cci_schema_info fail\n");
		return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}
	rc = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &error);
	if(rc==CCI_ER_NO_MORE_DATA)
		return DB_E_NOTABLE;
	else if (rc<0) return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	cci_close_req_handle(hReq);

	hReq = cci_schema_info(hConn, CCI_SCH_ATTRIBUTE, CW2A(table_name),
						CW2A(pColumnID->uName.pwszName), 0, &error);
	if(hReq<0)
	{
		ATLTRACE2("cci_schema_info fail\n");
		return E_FAIL;
	}
	rc = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &error);
	if(rc==CCI_ER_NO_MORE_DATA)
		return DB_E_NOCOLUMN;
	else if (rc<0) return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	cci_close_req_handle(hReq);

	strQuery.Append(L"ALTER ");
	strQuery.Append(pTableID->uName.pwszName);
	strQuery.Append(L" DROP ");
	strQuery.Append(pColumnID->uName.pwszName);

	hReq = cci_prepare(hConn, CW2A(strQuery), 0, &error);
	if (hReq < 0)
	{
		return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}
	
	rc = cci_execute(hReq, CCI_EXEC_QUERY_ALL, 0, &error);
	if (rc < 0)
	{
		return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}

	AutoCommit(0);

	return hr;
}

STDMETHODIMP CCUBRIDSession::CreateIndex( 
            /* [in] */ DBID *pTableID,
            /* [in] */ DBID *pIndexID,
            /* [in] */ DBORDINAL cIndexColumnDescs,
            /* [size_is][in] */ const DBINDEXCOLUMNDESC rgIndexColumnDescs[],
            /* [in] */ ULONG cPropertySets,
            /* [size_is][out][in] */ DBPROPSET rgPropertySets[],
            /* [out] */ DBID **ppIndexID)
{
	HRESULT hr = S_OK;
	CComBSTR	tableName, indexName;
	CComBSTR	strQuery;
	int			hConn, hReq, rc;
	T_CCI_ERROR	error;
	bool		bIsUnique = false;

	if (ppIndexID)
		*ppIndexID = NULL;

	ClearError();

	if ((!pTableID) ||
		(!pIndexID && !ppIndexID) ||
		(!cIndexColumnDescs) ||
		(!rgIndexColumnDescs) ||
		(cPropertySets && !rgPropertySets))
		return E_INVALIDARG;

	if (pIndexID)
	{
		if (CompareDBIDs(pIndexID, &DB_NULLID) == S_OK)
			return DB_E_BADINDEXID;

		if ((pIndexID->eKind == DBKIND_GUID_NAME || pIndexID->eKind == DBKIND_NAME) &&
			(!pIndexID->uName.pwszName || !wcslen(pIndexID->uName.pwszName)))
			return DB_E_BADINDEXID;
	}

	//프로퍼티 검사
	for (ULONG k = 0; k < cPropertySets; k++)
	{
		DBPROPSET propSet = rgPropertySets[k];

		if (propSet.cProperties && !propSet.rgProperties)
			return E_INVALIDARG;

		if (propSet.guidPropertySet == DBPROPSET_INDEX)
		{
			for (ULONG i = 0; i < propSet.cProperties; i++)
			{
				DBPROPID property = propSet.rgProperties[i].dwPropertyID;
				DBPROPSTATUS* status = &propSet.rgProperties[i].dwStatus;
				DBPROPOPTIONS options = propSet.rgProperties[i].dwOptions;
				VARIANT val = propSet.rgProperties[i].vValue;
				VARTYPE type = propSet.rgProperties[i].vValue.vt;

				if (CompareDBIDs(&propSet.rgProperties[i].colid, &DB_NULLID) != S_OK)
					hr = DB_S_ERRORSOCCURRED;

				if (property == DBPROP_INDEX_AUTOUPDATE ||
					property == DBPROP_INDEX_PRIMARYKEY ||
					property == DBPROP_INDEX_CLUSTERED ||
					property == DBPROP_INDEX_SORTBOOKMARKS ||
					property == DBPROP_INDEX_FILLFACTOR ||
					property == DBPROP_INDEX_TEMPINDEX ||
					property == DBPROP_INDEX_INITIALSIZE ||
					property == DBPROP_INDEX_TYPE || 
					property == DBPROP_INDEX_NULLCOLLATION ||
					property == DBPROP_INDEX_NULLS)
				{
					*status = DBPROPSTATUS_NOTSUPPORTED;

					if (options == DBPROPOPTIONS_REQUIRED)
						return DB_E_ERRORSOCCURRED;
				} else
				{
					if (options != DBPROPOPTIONS_REQUIRED &&
						options != DBPROPOPTIONS_OPTIONAL)
					{
						*status = DBPROPSTATUS_BADOPTION;
						return DB_E_ERRORSOCCURRED;
					}

					*status = DBPROPSTATUS_OK;

					if (type != VT_BOOL) //Type Error
					{
						*status = DBPROPSTATUS_BADVALUE;

						if (options == DBPROPOPTIONS_REQUIRED)
						{
							return DB_E_ERRORSOCCURRED;
						} else
							hr = DB_S_ERRORSOCCURRED;
					}
					bIsUnique = (V_BOOL(&val)==VARIANT_TRUE);
				}
			}

		}

	}

	hr = GetConnectionHandle(&hConn);
	if (FAILED(hr))
	{
		return E_FAIL;
	}

	//테이블 이름 존재 여부 검사
	hReq = cci_schema_info(hConn, CCI_SCH_CLASS, CW2A(pTableID->uName.pwszName),
							NULL, CCI_CLASS_NAME_PATTERN_MATCH, &error);
	if(hReq<0)
	{
		ATLTRACE2("cci_schema_info fail\n");
		return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}
	rc = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &error);
	if(rc==CCI_ER_NO_MORE_DATA)
		return DB_E_NOTABLE;
	else if (rc<0) return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	cci_close_req_handle(hReq);
	tableName = pTableID->uName.pwszName;

	if (!pIndexID)
	{
		indexName.Append(L"i_");
		indexName.Append(tableName);
		indexName.Append(L"_");

		for (DBORDINAL i = 0; i < cIndexColumnDescs; i++)
		{
			indexName.Append(rgIndexColumnDescs[i].pColumnID->uName.pwszName);
			if (i+1 != cIndexColumnDescs)
				indexName.Append(L"_");
		}
	} else
		indexName = pIndexID->uName.pwszName;

	//인덱스 이름 존재 여부 검사
	hReq = cci_schema_info(hConn, CCI_SCH_CONSTRAINT, CW2A(pTableID->uName.pwszName),
							NULL, CCI_ATTR_NAME_PATTERN_MATCH, &error);
	if(hReq<0)
	{
		ATLTRACE2("cci_schema_info fail\n");
		return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}
	rc = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &error);
	if (rc<0) return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);

	while (1)
	{
		char* index_name;
		int ind;

		rc = cci_fetch(hReq, &error);
		if (rc<0) return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
		if (rc == CCI_ER_NO_MORE_DATA) break;
		rc = cci_get_data(hReq, 2, CCI_A_TYPE_STR, &index_name, &ind);
		if (rc<0) return RaiseError(E_FAIL, 0, __uuidof(ITableDefinition));
		if (!_wcsicmp(CA2W(index_name), indexName))
			return DB_E_DUPLICATEINDEXID;

		rc = cci_cursor(hReq, 1, CCI_CURSOR_CURRENT, &error);
		if (rc<0) return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}
	cci_close_req_handle(hReq);
	
	if (bIsUnique)
		strQuery.Append(L"CREATE UNIQUE INDEX ");
	else
		strQuery.Append(L"CREATE INDEX ");
	strQuery.Append(indexName);
	strQuery.Append(L" ON ");
	strQuery.Append(tableName);
	strQuery.Append(L" (");
	for (DBORDINAL i = 0; i < cIndexColumnDescs; i++)
	{
		//컬럼 이름 존재 여부 검사
		hReq = cci_schema_info(hConn, CCI_SCH_ATTRIBUTE, CW2A(pTableID->uName.pwszName),
			CW2A(rgIndexColumnDescs[i].pColumnID->uName.pwszName), 0, &error);
		if(hReq<0)
		{
			ATLTRACE2("cci_schema_info fail\n");
			return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
		}
		rc = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &error);
		if(rc==CCI_ER_NO_MORE_DATA)
			return DB_E_NOCOLUMN;
		else if (rc<0) return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
		cci_close_req_handle(hReq);

		strQuery.Append(rgIndexColumnDescs[i].pColumnID->uName.pwszName);
		if (i + 1 != cIndexColumnDescs)
			strQuery.Append(L",");
	}
	strQuery.Append(L")");
	
	hReq = cci_prepare(hConn, CW2A(strQuery), 0, &error);
	if (hReq < 0)
	{
		ATLTRACE2(atlTraceDBProvider, 3, "%s\n", error.err_msg);
		return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}

	rc = cci_execute(hReq, CCI_EXEC_ASYNC, 0, &error);
	if (rc < 0)
	{
		ATLTRACE2(atlTraceDBProvider, 3, "%s\n", error.err_msg);
		return RaiseError(E_FAIL, 1, __uuidof(ITableDefinition), error.err_msg);
	}
		
	if(ppIndexID)
	{
		DBID tmp;
		tmp.eKind = DBKIND_NAME;
		tmp.uName.pwszName = indexName;
		*ppIndexID = (DBID *)CoTaskMemAlloc(sizeof(DBID));
		CopyDBIDs(*ppIndexID, &tmp);
	}
	AutoCommit(0);


	return hr;
}
        
STDMETHODIMP CCUBRIDSession::DropIndex( 
            /* [unique][in] */ DBID *pTableID,
            /* [unique][in] */ DBID *pIndexID)
{
	HRESULT hr = S_OK;
	CComBSTR	tableName, indexName;
	CComBSTR	strQuery;
	int			hConn, hReq, rc;
	T_CCI_ERROR	error;

	if (!pTableID)
		return E_INVALIDARG;

	ClearError();

	hr = GetConnectionHandle(&hConn);
	if (FAILED(hr))
	{
		return E_FAIL;
	}

	//테이블 이름 존재 여부 검사
	hReq = cci_schema_info(hConn, CCI_SCH_CLASS, CW2A(pTableID->uName.pwszName),
							NULL, CCI_CLASS_NAME_PATTERN_MATCH, &error);
	if(hReq<0)
	{
		ATLTRACE2("cci_schema_info fail\n");
		return RaiseError(E_FAIL, 1, __uuidof(IIndexDefinition), error.err_msg);
	}
	rc = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &error);
	if(rc==CCI_ER_NO_MORE_DATA)
		return DB_E_NOTABLE;
	else if (rc<0) return RaiseError(E_FAIL, 1, __uuidof(IIndexDefinition), error.err_msg);
	cci_close_req_handle(hReq);
	tableName = pTableID->uName.pwszName;

	if (pIndexID)
	{
		bool indexFound = false;
		bool bIsUniqueIndex = false;

		indexName = pIndexID->uName.pwszName;

		//Index 이름 존재 여부 검사
		hReq = cci_schema_info(hConn, CCI_SCH_CONSTRAINT, CW2A(pTableID->uName.pwszName),
								NULL, CCI_ATTR_NAME_PATTERN_MATCH, &error);
		rc = cci_cursor(hReq, 1, CCI_CURSOR_FIRST, &error);
		if (rc == CCI_ER_NO_MORE_DATA)
			return DB_E_NOINDEX;
		else if (rc<0) return RaiseError(E_FAIL, 1, __uuidof(IIndexDefinition), error.err_msg);

		while (1)
		{
			char* index_name;
			int isUnique;
			int ind;

			rc = cci_fetch(hReq, &error);
			if (rc == CCI_ER_NO_MORE_DATA) break;
			if (rc<0) return RaiseError(E_FAIL, 1, __uuidof(IIndexDefinition), error.err_msg);
			rc = cci_get_data(hReq, 1, CCI_A_TYPE_INT, &isUnique, &ind);
			if (rc<0) return RaiseError(E_FAIL, 0, __uuidof(IIndexDefinition));
			rc = cci_get_data(hReq, 2, CCI_A_TYPE_STR, &index_name, &ind);
			if (rc<0) return RaiseError(E_FAIL, 0, __uuidof(IIndexDefinition));

			if (!_wcsicmp(CA2W(index_name), indexName))
			{
				indexFound = true;
				if (isUnique == 0) bIsUniqueIndex = true;
				break;
			}
			rc = cci_cursor(hReq, 1, CCI_CURSOR_CURRENT, &error);
			if (rc<0) return RaiseError(E_FAIL, 1, __uuidof(IIndexDefinition), error.err_msg);
		}
		cci_close_req_handle(hReq);

		if (!indexFound)
			return DB_E_NOINDEX;

		if (bIsUniqueIndex)
			strQuery.Append(L"DROP UNIQUE INDEX ");
		else
			strQuery.Append(L"DROP INDEX ");
		strQuery.Append(indexName);
		strQuery.Append(L" ON ");
		strQuery.Append(tableName);
		
		hReq = cci_prepare(hConn, CW2A(strQuery), 0, &error);
		if (hReq < 0)
		{
			ATLTRACE2(atlTraceDBProvider, 3, "%s\n", error.err_msg);
			return RaiseError(E_FAIL, 1, __uuidof(IIndexDefinition), error.err_msg);
		}

		rc = cci_execute(hReq, CCI_EXEC_ASYNC, 0, &error);
		if (rc < 0)
		{
			ATLTRACE2(atlTraceDBProvider, 3, "%s\n", error.err_msg);
			return RaiseError(E_FAIL, 1, __uuidof(IIndexDefinition), error.err_msg);
		}
	} else
	{
		CAtlArray<CStringA> rgIndexNames;
		CAtlArray<int> rgIndexTypes;

		hr = Util::GetIndexNamesInTable(hConn, CW2A(tableName), rgIndexNames, rgIndexTypes);
		if (FAILED(hr)) return E_FAIL;
		size_t cIndex = rgIndexNames.GetCount();

		for(size_t i=0;i<cIndex;i++)
		{
			if (rgIndexTypes.GetAt(i) == 0) 
				strQuery.Append(L"DROP UNIQUE INDEX ");
			else
				strQuery.Append(L"DROP INDEX ");
			strQuery.Append(rgIndexNames[i].GetBuffer());
			strQuery.Append(L" ON ");
			strQuery.Append(tableName);

			if (i+1 != cIndex)
				strQuery.Append(L";");
		}

		hReq = cci_prepare(hConn, CW2A(strQuery), 0, &error);
		if (hReq < 0)
		{
			ATLTRACE2(atlTraceDBProvider, 3, "%s\n", error.err_msg);
			return RaiseError(E_FAIL, 1, __uuidof(IIndexDefinition), error.err_msg);
		}

		rc = cci_execute(hReq, CCI_EXEC_QUERY_ALL, 0, &error);
		if (rc < 0)
		{
			ATLTRACE2(atlTraceDBProvider, 3, "%s\n", error.err_msg);
			return RaiseError(E_FAIL, 1, __uuidof(IIndexDefinition), error.err_msg);
		}

	}
	AutoCommit(0);

	return hr;
}