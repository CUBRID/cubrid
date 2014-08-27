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
#include "Error.h"
#include "ProviderInfo.h"

//const OLEDBDECLSPEC GUID DBPROPSET_UNIPROVIDER_DBINIT = {0x7f555b1d,0xc6d2,0x40ce,{0x9a,0xb4,0x49,0x62,0x78,0x1e,0xb6,0x6c}};

//extern "C" {
//char cci_client_name[8] = "ODBC";
//}

CCUBRIDDataSource *CCUBRIDDataSource::GetDataSourcePtr(IObjectWithSite *pSite)
{
	CComPtr<IDBCreateSession> spCom;
	HRESULT hr = pSite->GetSite(__uuidof(IDBCreateSession), (void **)&spCom);
	// 제대로 프로그래밍 됐을때, 실패하는 경우가 있을까?
	ATLASSERT(SUCCEEDED(hr));
	// 굳이 오버헤드를 감수해가며 dynamic_cast를 쓸 필요는 없을 듯
	return static_cast<CCUBRIDDataSource *>((IDBCreateSession *)spCom);
}

STDMETHODIMP CCUBRIDDataSource::Initialize(void)
{
	ATLTRACE2(atlTraceDBProvider, 2, "CCUBRIDDataSource::Initialize\n");

	// 현재 오류객체를 제거한다.
	ClearError();

	// 연결정보들(ID, 암호등)이 올바른지 확인한다.
	char dbmsver[16];
	{
		int hConn = 0;
		//T_CCI_ERROR error;

		HRESULT hr = Util::Connect(this, &hConn);
		if(FAILED(hr)) return hr;

		char buf[16];
		T_CCI_ERROR error;
		int rc = cci_get_db_version(hConn, buf, sizeof(buf));
		if(rc<0)
		{
			ATLASSERT(rc!=CCI_ER_CON_HANDLE);
			// rc==CCI_ER_CONNECT -> 주소나 포트가 틀림
			// rc==CAS_ER_DBMS -> DB 이름이나 ID, 암호가 틀림

			ATLTRACE2(atlTraceDBProvider, 0, "CCUBRIDDataSource::Initialize : cci_get_db_version failed with rc=%d\n", rc);
			Util::Disconnect(&hConn);

			CComVariant var;
			var = DBPROPVAL_CS_COMMUNICATIONFAILURE;
			SetPropValue(&DBPROPSET_DATASOURCEINFO, DBPROP_CONNECTIONSTATUS, &var);

			return RaiseError(DB_SEC_E_AUTH_FAILED, 0, __uuidof(IDBInitialize), (LPWSTR)0, L"42000");
		}

		//최대 스트링 갯수를 나타내는 데이터베이스 파라메터를 가져온다.
		rc = cci_get_db_parameter(hConn, CCI_PARAM_MAX_STRING_LENGTH, &PARAM_MAX_STRING_LENGTH, &error);
		if (rc < 0)
		{
			Util::Disconnect(&hConn);

			CComVariant var;
			var = DBPROPVAL_CS_COMMUNICATIONFAILURE;
			SetPropValue(&DBPROPSET_DATASOURCEINFO, DBPROP_CONNECTIONSTATUS, &var);

			return RaiseError(E_FAIL, 0, __uuidof(IDBInitialize), error.err_msg);
		}

		Util::Disconnect(&hConn);

		int a=0, b=0, c=0;
		sscanf(buf, "%2d.%2d.%2d", &a, &b, &c);
		sprintf(dbmsver, "%02d.%02d.%04d", a, b, c);
	}

	// ATL의 초기화 루틴을 호출
	{
		HRESULT hr = IDBInitializeImpl<CCUBRIDDataSource>::Initialize();
		if(FAILED(hr)) return hr;
	}

	// set properties
	{
		CComVariant var;
		// 읽기 전용 속성이므로 IDBProperties::SetProperties를 이용할 수 없다.
		// 대신 내부적으로 IDBProperties에 이용된 CUtlProps::SetPropValue를 사용한다.
		// 외부적으론 변경할 수 없지만, 내부적으로 변경할 수 있도록
		// DBPROPFLAGS_CHANGE flag를 추가한다.
		var = dbmsver;
		SetPropValue(&DBPROPSET_DATASOURCEINFO, DBPROP_DBMSVER, &var);

		VariantClear(&var);
		VariantInit(&var);
		var = "2.0.01.004";

		SetPropValue(&DBPROPSET_DATASOURCEINFO, DBPROP_PROVIDERVER, &var);

		GetPropValue(&DBPROPSET_DBINIT, DBPROP_INIT_LOCATION, &var);
		SetPropValue(&DBPROPSET_DATASOURCEINFO, DBPROP_DATASOURCENAME, &var);

		var = DBPROPVAL_CS_INITIALIZED;
		SetPropValue(&DBPROPSET_DATASOURCEINFO, DBPROP_CONNECTIONSTATUS, &var);
		
	}

	return S_OK;
}

STDMETHODIMP CCUBRIDDataSource::Uninitialize(void)
{
	ATLTRACE2(atlTraceDBProvider, 2, "CCUBRIDDataSource::Uninitialize\n");

	CComVariant var;

	// 현재 오류객체를 제거한다.
	ClearError();

	// ATL의 루틴을 호출
	{
		HRESULT hr = IDBInitializeImpl<CCUBRIDDataSource>::Uninitialize();
		if(FAILED(hr)) return hr;
	}

	var = DBPROPVAL_CS_UNINITIALIZED;
	SetPropValue(&DBPROPSET_DATASOURCEINFO, DBPROP_CONNECTIONSTATUS, &var);

	return S_OK;
}

STDMETHODIMP CCUBRIDDataSource::CreateSession(IUnknown *pUnkOuter, REFIID riid, IUnknown **ppDBSession)
{
	if(ppDBSession==NULL) return E_INVALIDARG;
	*ppDBSession = NULL;

	// DBPROP_ACTIVESESSIONS 개수 이상의 session을 열 수 없다.
	{
		CComVariant var;
		HRESULT hr = GetPropValue(&DBPROPSET_DATASOURCEINFO, DBPROP_ACTIVESESSIONS, &var);
		if(FAILED(hr)) return hr;

		ATLASSERT(var.vt==VT_I4);
		int cActSessions = V_I4(&var);

		if(cActSessions!=0 && this->m_cSessionsOpen>=cActSessions)
			return DB_E_OBJECTCREATIONLIMITREACHED;
	}

	// DBPROP_MULTIPLECONNECTIONS==FALSE면
	// 여러 개의 connection handle을 여는 것을 허용하지 않음
	/*
	{
		CComVariant var;
		HRESULT hr = GetPropValue(&DBPROPSET_DATASOURCE, DBPROP_MULTIPLECONNECTIONS, &var);
		if(FAILED(hr)) return hr;

		ATLASSERT(var.vt==VT_BOOL);
		bool bMulSessions = V_BOOL(&var);

		if(!bMulSessions && this->m_cSessionsOpen!=0)
			return DB_E_OBJECTOPEN;
	}
	*/

	return IDBCreateSessionImpl<CCUBRIDDataSource, CCUBRIDSession>::CreateSession(pUnkOuter, riid, ppDBSession);
}

STDMETHODIMP CCUBRIDDataSource::GetLiteralInfo(ULONG cLiterals, const DBLITERAL rgLiterals[],
							  ULONG *pcLiteralInfo, DBLITERALINFO **prgLiteralInfo,
							  OLECHAR **ppCharBuffer)
{
	ATLTRACE(atlTraceDBProvider, 2, _T("CCUBRIDDataSource::GetLiteralInfo\n"));
	ObjectLock lock(this);

	// 현재 오류객체를 제거한다.
	ClearError();

	// 초기화
	if( pcLiteralInfo )
		*pcLiteralInfo = 0;
	if( prgLiteralInfo )
		*prgLiteralInfo = NULL;
	if( ppCharBuffer )
		*ppCharBuffer = NULL;
		
	// 파라미터 체크
	if (!pcLiteralInfo || !prgLiteralInfo || !ppCharBuffer)
		return E_INVALIDARG;
	if( cLiterals != 0 && rgLiterals == NULL )
		return E_INVALIDARG;

	// Data Source가 초기화 되었는지 확인
	if (!(m_dwStatus & DSF_INITIALIZED))
		return E_UNEXPECTED;

	*ppCharBuffer = (WCHAR *)CoTaskMemAlloc(ProvInfo::size_wszAllStrings);
	if(*ppCharBuffer==NULL) return E_OUTOFMEMORY;
	memcpy(*ppCharBuffer, ProvInfo::wszAllStrings, ProvInfo::size_wszAllStrings);

	// 제공되는 모두 literal 정보 수
	const UINT numLiteralInfos = ProvInfo::size_LiteralInfos;

	// cLiterals가 0이면 모든 literal 정보 리턴
	*pcLiteralInfo = ( cLiterals==0 ? numLiteralInfos : cLiterals );

	// *pcLiteralInfo 만큼의 DBLITERALINFO 구조체 배열 할당
	*prgLiteralInfo = (DBLITERALINFO *)CoTaskMemAlloc(*pcLiteralInfo * sizeof(DBLITERALINFO));
	if (!*prgLiteralInfo)
	{
		::CoTaskMemFree(*ppCharBuffer);
		*pcLiteralInfo = 0;
		*ppCharBuffer = 0;
		return E_OUTOFMEMORY;
	}

	ULONG ulSucceeded = 0;
	if(cLiterals)
	{	// 일부 literal 정보만 반환
		for(ULONG i=0;i<*pcLiteralInfo;i++)
		{
			ULONG j;
			// 요청된 literal이 LiteralInfos에 있는지 찾아본다.
			for(j=0;j<numLiteralInfos;j++)
			{
				if(ProvInfo::LiteralInfos[j].lt==rgLiterals[i])
				{
					(*prgLiteralInfo)[i] = ProvInfo::LiteralInfos[j];
					ulSucceeded++;
					break;
				}
			}

			if(j==numLiteralInfos)
			{	// LiteralInfos에 없는 literal
				// fSupported도 자동으로 FALSE가 된다.
				ZeroMemory((*prgLiteralInfo)+i, sizeof(DBLITERALINFO));
				(*prgLiteralInfo)[i].lt = rgLiterals[i];
			}
		}
	}
	else
	{	// 모든 literal 정보를 반환
		for( ; ulSucceeded<numLiteralInfos ; ulSucceeded++ )
			(*prgLiteralInfo)[ulSucceeded] = ProvInfo::LiteralInfos[ulSucceeded];
	}

	if(ulSucceeded==*pcLiteralInfo)
		return S_OK;
	else if(ulSucceeded!=0)
		return DB_S_ERRORSOCCURRED;
	else
	{
		// 스펙에 따라서 string buffer는 free하고, infos buffer는 놔둔다.
		::CoTaskMemFree(*ppCharBuffer);
		*ppCharBuffer = NULL;
		return DB_E_ERRORSOCCURRED;
	}
}

STDMETHODIMP CCUBRIDDataSource::GetKeywords(LPOLESTR *ppwszKeywords)
{
	ATLTRACE(atlTraceDBProvider, 2, _T("CCUBRIDDataSource::GetKeywords\n"));
	ObjectLock lock(this);
	
	// 현재 오류객체를 제거한다.
	ClearError();

	// check params
	if (ppwszKeywords == NULL)
		return E_INVALIDARG;
	*ppwszKeywords = NULL;
	
	// check if data source object is initialized
	if (!(m_dwStatus & DSF_INITIALIZED))
		return E_UNEXPECTED;

	// 키워드 리스트를 comma를 seperate 문자로 하여 continuous한 배열로 선언
	OLECHAR Keywords[] = L"ABORT,ACTIVE,ADD_MONTHS,AFTER,ALIAS,ASYNC,ATTACH,ATTRIBUTE,"
						 L"BEFORE,BOOLEAN,BREADTH,CALL,CHANGE,CLASS,CLASSES,CLUSTER,"
						 L"COMMITTED,COMPLETION,COST,CYCLE,DATA,DATA_TYPE___,DECAY_CONSTANT,"
						 L"DEFINED,DEPTH,DICTIONARY,DIFFERENCE,DIRECTORY,DRAND,EACH,"
						 L"ELSEIF,EQUALS,EVALUATE,EVENT,EXCLUDE,FILE,FUNCTION,GDB,GENERAL,"
						 L"GROUPBY_NUM,GROUPS,HOST,IDENTIFIED,IF,IGNORE,INACTIVE,INCREMENT,"
						 L"INDEX,INFINITE,INHERIT,INOUT,INSTANCES,INST_NUM,"
						 L"INTERSECTION,INTRINSIC,INVALIDATE,LAST_DAY,LDB,LEAVE,LESS,LIMIT,LIST,"
						 L"LOCK,LOOP,LPAD,LTRIM,MAXIMUM,MAXVALUE,MAX_ACTIVE,"
						 L"MEMBERS,METHOD,MINVALUE,MIN_ACTIVE,MODIFY,MOD,MONETARY,"
						 L"MONTHS_BETWEEN,MULTISET,MULTISET_OF,NA,NAME,"
						 L"NEW,NOCYCLE,NOMAXVALUE,NOMINVALUE,NONE,OBJECT,OBJECT_ID,OFF,OID,OLD,"
						 L"OPERATION,OPERATORS,OPTIMIZATION,ORDERBY_NUM,OTHERS,OUT,PARAMETERS,"
						 L"PASSWORD,PENDANT,PREORDER,PRINT,PRIORITY,PRIVATE,PROXY,PROTECTED,"
						 L"QUERY,RAND,RECURSIVE,REF,REFERENCING,REGISTER,REJECT,RENAME,REPEATABLE,"
						 L"REPLACE,RESIGNAL,RETURN,RETURNS,ROLE,ROUTINE,ROW,ROWNUM,"
						 L"RPAD,RTRIM,SAVEPOINT,SCOPE___,SEARCH,SENSITIVE,SEQUENCE,"
						 L"SEQUENCE_OF,SERIAL,SERIALIZABLE,SETEQ,SETNEQ,SET_OF,SHARED,SHORT,SIGNAL,"
                         L"SIMILAR,SQLEXCEPTION,SQLWARNING,STABILITY,START,STATEMENT,STATISTICS,STATUS,"
						 L"STDDEV,STRING,STRUCTURE,SUBCLASS,SUBSET,SUBSETEQ,SUPERCLASS,"
						 L"SUPERSET,SUPERSETEQ,SYS_DATE,SYS_TIME,SYS_TIMESTAMP,SYS_USER,"
						 L"TEST,THERE,TIMEOUT,TO_CHAR,TO_DATE,TO_NUMBER,TO_TIMETO_TIMESTAMP,"
						 L"TRACE,TRIGGERS,TYPE,UNDER,USE,UTIME,VARIABLE,VARIANCE,"
						 L"VCLASS,VIRTUAL,VISIBLE,WAIT,WHILE,WITHOUT";

	// 버퍼를 생성한다 
	*ppwszKeywords = (LPWSTR)CoTaskMemAlloc(sizeof(Keywords));
	if( *ppwszKeywords == NULL )
		return E_OUTOFMEMORY;

	// Copy keywords
	wcscpy( *ppwszKeywords, Keywords );
	
	return S_OK;
}
