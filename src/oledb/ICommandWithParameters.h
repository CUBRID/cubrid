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

template <class T>
class ATL_NO_VTABLE ICommandWithParametersImpl : public ICommandWithParameters
{
public:
        HRESULT STDMETHODCALLTYPE GetParameterInfo( 
            DB_UPARAMS *pcParams,
            DBPARAMINFO **prgParamInfo,
            OLECHAR **ppNamesBuffer)
		{
			ATLTRACE(atlTraceDBProvider, 2, _T("ICommandWithParamtersImpl::GetParameterInfo\n"));

			T* pT = (T*)this;
			T::ObjectLock lock(pT);

			DBCOUNTITEM		cParams = pT->m_cParams; //파라메터 갯수
			DBPARAMINFO*	pParamInfo = pT->m_pParamInfo; //파라메터 정보 구조체 배열

			//파라미터 갯수 초기에 0으로 세팅
			*pcParams = 0;

			//argument 체크
			if (pcParams == NULL || prgParamInfo == NULL)
				return E_INVALIDARG;

			//초기화
			*prgParamInfo = NULL;
			
			//파라메터 이름 배열 NULL로 초기화
			if (ppNamesBuffer != NULL)
				*ppNamesBuffer = NULL;

			//세팅된 파라메터가 없을 경우
			if (cParams == 0)
				return DB_E_PARAMUNAVAILABLE;

			//파라메터 갯수 카피
			*pcParams = cParams;

			//파라메터 정보 구조체 배열 카피
			*prgParamInfo = (DBPARAMINFO*)CoTaskMemAlloc(cParams * sizeof(DBPARAMINFO));
			memcpy(*prgParamInfo, pParamInfo, cParams * sizeof(DBPARAMINFO));

			//파라메터 이름이 세팅되어 있는지 체크
			if (pParamInfo[0].pwszName != NULL)
			{
				//파라메터 이름 배열에 사용될 총 Byte 길이를 계산
				ULONG i;
				size_t cChars;
				size_t cBytes = 0;

				for (i = 0; i < cParams; i++)
				{
					//배열상의 각각의 이름 뒤에는 null character가 온다
					cChars = wcslen(pParamInfo[i].pwszName) + 1;

					//파라메터 이름의 길이를 임시 저장
					(*prgParamInfo)[i].pwszName = (WCHAR*)cChars;
					
					cBytes += cChars * sizeof(WCHAR);
				}

				//계산 된 바이트 만큼 메모리 할당
				*ppNamesBuffer = (WCHAR*)CoTaskMemAlloc(cBytes);

				//Fill out the string buffer
				WCHAR* pstrTemp = *ppNamesBuffer;
				for (i = 0; i < cParams; i++)
				{
					//임시 저장된 파라메터 이름의 길이
					cChars = (size_t)(*prgParamInfo)[i].pwszName;

					//파라메터 이름 카피
					wcscpy(pstrTemp, pParamInfo[i].pwszName);
					(*prgParamInfo)[i].pwszName = pstrTemp;

					//파라메터 이름 배열상의 인덱스 이동
					pstrTemp += cChars;
				}
			}

			return S_OK;
		}
        
        HRESULT STDMETHODCALLTYPE MapParameterNames( 
			DB_UPARAMS cParamNames,
			const OLECHAR *rgParamNames[],
			DB_LPARAMS rgParamOrdinals[])
		{
			ATLTRACE(atlTraceDBProvider, 2, _T("ICommandWithParamtersImpl::MapParameterNames\n"));

			T* pT = (T*)this;
			T::ObjectLock lock(pT);

			bool bParamFound = false;
			bool bParamNotFound = false;

			//cParamNames가 0이면 그냥 S_OK를 리턴
			if (cParamNames == 0)
				return S_OK;

			//cParamNames가 0이 아니고 rgParamNames 혹은 rgParamOrdinals가 NULL이면
			//E_INVALIDARG를 리턴
			if (rgParamNames == NULL || rgParamOrdinals == NULL)
				return E_INVALIDARG;

			for (ULONG i = 0; i < cParamNames; i++)
			{
				bool bFound = false;
				for (ULONG j = 0; j < pT->m_cParams; j++)
				{
					//_wcsicmp -> lower case comparison
					if (_wcsicmp(rgParamNames[i], pT->m_pParamInfo[j].pwszName) == 0)
					{
						bFound = true;
						break;
					}
				}

				//파라메터 이름이 같은 것이 발견되면 bParamFound를 true로 세팅하고
				//하나라도 발견되지 못하면 bParamNotFound를 true로 세팅
				if (bFound)
				{
					rgParamOrdinals[i] = j + 1;
					bParamFound = true;
				}
				else
				{
					rgParamOrdinals[i] = 0;
					bParamNotFound = true;
				}

			}

			//파라메터 이름이 기존의 파라메터 이름과 일치할 때
			if (bParamFound && !bParamNotFound)
				return S_OK;

			//하나라도 일치하지 않으면
			if (bParamFound && bParamNotFound)
				return DB_S_ERRORSOCCURRED;

			return DB_E_ERRORSOCCURRED;
		}
        
        HRESULT STDMETHODCALLTYPE SetParameterInfo( 
			DB_UPARAMS cParams,
			const DB_UPARAMS rgParamOrdinals[],
			const DBPARAMBINDINFO rgParamBindInfo[])
		{
			ATLTRACE(atlTraceDBProvider, 2, _T("ICommandWithParamtersImpl::SetParameterInfo\n"));

			T* pT = (T*)this;
			T::ObjectLock lock(pT);
			
			ULONG i;
			DBPARAMINFO* pParamInfo = NULL;
			
			HRESULT hr = S_OK;
			DBCOUNTITEM nCount = cParams;

			//함수 argument 체크
			if (cParams != 0 && rgParamOrdinals == NULL)
				return E_INVALIDARG;

			if (cParams == 0)
				goto Success;
			
			
			//기존의 파라메터 수와 세팅할 파라메터수를 합한다
			if (pT->m_cParams != NULL)
				nCount += pT->m_cParams;

			//합한 nCount만큼의 버퍼를 할당한다
			pParamInfo = (DBPARAMINFO*)CoTaskMemAlloc(nCount * sizeof(DBPARAMINFO));
			
			//기존에 파라메터 정보가 있었을 때 먼저 pParamInfo에 카피한다
			if (pT->m_pParamInfo != NULL)
			{
				memcpy(pParamInfo, pT->m_pParamInfo, pT->m_cBindings * sizeof(DBPARAMINFO));

				for (i = 0; i < pT->m_cParams; i++)
					pParamInfo[i].pwszName = SysAllocString(pT->m_pParamInfo[i].pwszName);
			}
						
			ULONG j;
			nCount = pT->m_cParams;
			for (i = 0; i < cParams; i++)
			{
				//파라메터 Ordinal이 0이면 E_INVALIDARD 리턴
				if (rgParamOrdinals[i] == 0)
				{
					hr = E_INVALIDARG;
					goto Error;
				}

				//새로운 파라메터의 ordinal이 기존 정보의 ordinal과 겹치는지의 여부
				bool bFound = false;
				for (j = 0; j < nCount; j++)
				{
					if (pParamInfo[j].iOrdinal == rgParamOrdinals[i])
					{
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					//발견되지 않았으면 새로운 ordinal을 부여
					j = nCount;
					nCount++;
				}
				else
				{
					//기존의 파라메터 이름 해제
					if (pParamInfo[j].pwszName != NULL)
						SysFreeString(pParamInfo[j].pwszName);
					
					//파라메터 정보가 override되었음을 세팅
					hr = DB_S_TYPEINFOOVERRIDDEN; 
				}


				//새로운 파라메터 information 세팅
				if (rgParamBindInfo[i].pwszName == NULL || *rgParamBindInfo[i].pwszName == 0)
					pParamInfo[j].pwszName  = NULL;
				else
					pParamInfo[j].pwszName	= SysAllocString(rgParamBindInfo[i].pwszName);

				pParamInfo[j].dwFlags		= rgParamBindInfo[i].dwFlags;
				pParamInfo[j].iOrdinal		= rgParamOrdinals[i];
				pParamInfo[j].pTypeInfo		= NULL;
				pParamInfo[j].ulParamSize	= rgParamBindInfo[i].ulParamSize;
				pParamInfo[j].wType			= GetOledbTypeFromName(rgParamBindInfo[i].pwszDataSourceType);
				pParamInfo[j].bPrecision	= rgParamBindInfo[i].bPrecision;
				pParamInfo[j].bScale		= rgParamBindInfo[i].bScale;
			}

			//DBPARAMINFO 구조체 배열상의 모든 항목에 파라메터 이름이 세팅되어 있는지 체크
			for (i = 1; i < nCount; i++)
			{
				if ((pParamInfo[0].pwszName == NULL && pParamInfo[i].pwszName != NULL) ||
					(pParamInfo[0].pwszName != NULL && pParamInfo[i].pwszName == NULL)) 
				{
					hr = DB_E_BADPARAMETERNAME;
					goto Error;
				}
			}

		Success:
			//기존의 정보 해제
			if (pT->m_pParamInfo != NULL)
			{
				for (i = 0; i < pT->m_cParams; i++)
					SysFreeString(pT->m_pParamInfo[i].pwszName);

				CoTaskMemFree(pT->m_pParamInfo);
			}

			//새 정보로 세팅
			//m_cParams : 파라메터 갯수
			//m_cParamInfo : 파라메터 정보 구조체 배열
			pT->m_cParams = nCount;
			pT->m_pParamInfo = pParamInfo;

			return hr;

		Error:
			//새로 변경하려던 정보 해제
			for (i = 0; i < nCount; i++)
				SysFreeString(pParamInfo[i].pwszName);

			CoTaskMemFree(pParamInfo);
			
			return hr;
		}

};
