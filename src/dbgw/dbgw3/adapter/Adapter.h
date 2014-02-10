/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#ifndef ADAPTER_H_
#define ADAPTER_H_

#if defined(WINDOWS) || defined(_WIN32) || defined(_WIN64)
#  include <Winsock2.h>
#  if defined(DBGW_ADAPTER_API_EXPORT)
#    define DECLSPECIFIER __declspec(dllexport)
#  else /* defined(DBGW_ADAPTER_API_EXPORT) */
#    define DECLSPECIFIER __declspec(dllimport)
#    if defined(_DEBUG)
#      define PREFIX_DEBUG "D"
#    else /* defined(_DEBUG) */
#      define PREFIX_DEBUG ""
#    endif /* !defined(_DEBUG) */
#    if defined(USE_NCLAVIS)
#      define PREFIX_NCLAVIS "NClavis"
#    else /* defined(USE_NCLAVIS) */
#      define PREFIX_NCLAVIS ""
#    endif /* !defined(USE_NCLAVIS) */
#    if defined(USE_MYSQL)
#      define PREFIX_DB "MySQL"
#    else /* defined(USE_MYSQL) */
#      define PREFIX_DB ""
#    endif /* !defined(USE_MYSQL) */
#    define DBGW_LIB_NAME "DBGWConnector3"##PREFIX_DEBUG PREFIX_NCLAVIS PREFIX_DB##".lib"
#    pragma comment(lib, DBGW_LIB_NAME)
#  endif /* !defined(DBGW_ADAPTER_API_EXPORT) */
typedef __int64 int64;
#else /* defined(WINDOWS) || defined(_WIN32) || defined(_WIN64) */
#  define __stdcall
#  define DECLSPECIFIER
#endif /* !(defined(WINDOWS) || defined(_WIN32) || defined(_WIN64)) */

#include "dbgw3/Exception.h"

namespace DBGW3
{

  enum QueryMapperVersion
  {
    DBGW_QUERY_MAP_VER_UNKNOWN,
    DBGW_QUERY_MAP_VER_10,
    DBGW_QUERY_MAP_VER_20,
    DBGW_QUERY_MAP_VER_30
  };

  enum ValueType
  {
    DBGW_VAL_TYPE_UNDEFINED = -1,
    DBGW_VAL_TYPE_INT = 0,
    DBGW_VAL_TYPE_STRING,
    DBGW_VAL_TYPE_LONG,
    DBGW_VAL_TYPE_CHAR,
    DBGW_VAL_TYPE_FLOAT,
    DBGW_VAL_TYPE_DOUBLE,
    DBGW_VAL_TYPE_DATETIME,
    DBGW_VAL_TYPE_DATE,
    DBGW_VAL_TYPE_TIME,
    DBGW_VAL_TYPE_BYTES,
    DBGW_VAL_TYPE_CLOB,
    DBGW_VAL_TYPE_BLOB,
    DBGW_VAL_TYPE_BOOL
  };

  enum _FAULT_TYPE
  {
    DBGW_FAULT_TYPE_NONE = 0,
    DBGW_FAULT_TYPE_EXEC_BEFORE,
    DBGW_FAULT_TYPE_EXEC_AFTER,
  };

  DECLSPECIFIER unsigned int __stdcall GetLastError();
  DECLSPECIFIER int __stdcall GetLastInterfaceErrorCode();
  DECLSPECIFIER const char *__stdcall GetLastErrorMessage();
  DECLSPECIFIER const char *__stdcall GetFormattedErrorMessage();

  namespace Environment
  {

    typedef void *Handle, *THandle;

    /**
     * this is for backward compatibility.
     */
    DECLSPECIFIER Handle __stdcall CreateHandle(size_t nThreadPoolSize);
    DECLSPECIFIER Handle __stdcall CreateHandle(const char *szConfFileName);
    DECLSPECIFIER void __stdcall DestroyHandle(Handle hEnv);


    DECLSPECIFIER void __stdcall SetDefaultTimeout(Handle hEnv,
        unsigned long ulMilliseconds);
    DECLSPECIFIER bool __stdcall GetDefaultTimeout(Handle hEnv,
        unsigned long *pTimeout);
    DECLSPECIFIER bool __stdcall LoadConnector(Handle hEnv);
    DECLSPECIFIER bool __stdcall LoadQueryMapper(Handle hEnv);
    /**
     * do not call this method directly.
     */
    DECLSPECIFIER bool __stdcall LoadConnector(Handle hEnv,
        const char *szConnectorFileName);
    DECLSPECIFIER bool __stdcall LoadQueryMapper(Handle hEnv,
        QueryMapperVersion version, const char *szQueryMapFileName,
        bool bAppend = false);

  }

  namespace Connector
  {

    typedef Environment::Handle *Handle, *THandle;

    /**
     * this is for backward compatibility.
     */
    DECLSPECIFIER Handle __stdcall CreateHandle(Environment::Handle hEnv,
        const char *szMRSAddress);
    DECLSPECIFIER Handle __stdcall CreateHandle(Environment::Handle hEnv);
    DECLSPECIFIER void __stdcall DestroyHandle(Handle hEnv);

    DECLSPECIFIER void __stdcall SetDefaultTimeout(Handle hConnector,
        unsigned long ulMilliseconds);
    DECLSPECIFIER bool __stdcall GetDefaultTimeout(Handle hConnector,
        unsigned long *pulMilliseconds);
#ifdef ENABLE_UNUSED_FUNCTION
    /**
     * DBGW 3.0 cannot support this feature.
     */
    DECLSPECIFIER bool __stdcall SetLocalCharset(Handle hEnv,
        DBGW::CodePage codepage);
    DECLSPECIFIER DBGW::CodePage __stdcall GetLocalCharset(Handle hEnv);
#endif

  }

  namespace ParamSet
  {

    typedef void *Handle, *THandle;

    DECLSPECIFIER Handle __stdcall CreateHandle();
    DECLSPECIFIER void __stdcall DestroyHandle(Handle hParam);

    DECLSPECIFIER bool __stdcall Clear(Handle hParam);
    DECLSPECIFIER size_t __stdcall Size(Handle hParam);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, int nParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        int nParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, char cParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        char cParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, int64 nParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        int64 nParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, float fParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        float fParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, double dParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        double dParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, bool bParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        bool bParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, const char *szParamValue, size_t nLen);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        const char *szParamValue, size_t nLen);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, size_t nSize, const void *pValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        size_t nSize, const void *pValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, ValueType type, const struct tm &value);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        ValueType type, const struct tm &value);

  }

  namespace ParamList
  {

    typedef void *Handle, *Thandle;

    DECLSPECIFIER Handle __stdcall CreateHandle();
    DECLSPECIFIER void __stdcall DestroyHandle(Handle hParamList);

    DECLSPECIFIER size_t __stdcall GetSize(Handle hParamList);
    DECLSPECIFIER bool __stdcall Add(Handle hParamList, ParamSet::Handle hParam);

  }

  namespace ResultSetMeta
  {

    typedef void *Handle, *THandle;

    DECLSPECIFIER void __stdcall DestroyHandle(Handle hMeta);

    DECLSPECIFIER size_t __stdcall GetColumnCount(Handle hMeta);
    DECLSPECIFIER bool __stdcall GetColumnName(Handle hMeta, size_t nIndex,
        const char **szName);
    DECLSPECIFIER bool __stdcall GetColumnType(Handle hMeta, size_t nIndex,
        ValueType *pType);

  }

  namespace ResultSet
  {

    typedef void *Handle, *THandle;

    DECLSPECIFIER Handle __stdcall CreateHandle();
    DECLSPECIFIER void  __stdcall DestroyHandle(Handle hResult);

    DECLSPECIFIER size_t __stdcall GetRowCount(Handle hResult);
    DECLSPECIFIER bool  __stdcall First(Handle hResult);
    DECLSPECIFIER bool  __stdcall Fetch(Handle hResult);
    DECLSPECIFIER size_t __stdcall GetAffectedCount(Handle hResult);
    DECLSPECIFIER bool __stdcall IsNeedFetch(Handle hResult);
    DECLSPECIFIER ResultSetMeta::Handle __stdcall GetMetaData(Handle hResult);
    DECLSPECIFIER bool __stdcall IsNull(Handle hResult, int nIndex,
        bool *pIsNull);
    DECLSPECIFIER bool __stdcall IsNull(Handle hResult, const char *szName,
        bool *pIsNull);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        int *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, int *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        int64 *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, int64 *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        float *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, float *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        double *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, double *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        bool *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, bool *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        char *szBuffer, int BufferSize, size_t *pLen);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, char *szBuffer, int BufferSize, size_t *pLen);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        size_t *pSize, const char **pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, size_t *pSize, const char **pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        int *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        int *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        int64 *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        int64 *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        float *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        float *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        double *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        double *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        bool *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        bool *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        char *szBuffer, int BufferSize, size_t *pLen);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        char *szBuffer, int BufferSize, size_t *pLen);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        size_t *pSize, const char **pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        size_t *pSize, const char **pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        struct tm *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        struct tm *pValue);
    DECLSPECIFIER bool __stdcall GetResultSet(Handle hResult, int nIndex,
        Handle hOutResult);
    DECLSPECIFIER bool __stdcall GetResultSet(Handle hResult,
        const char *szName, Handle hOutResult);
    DECLSPECIFIER bool __stdcall GetType(Handle hResult, int nIndex,
        ValueType *pType);
    DECLSPECIFIER bool __stdcall GetType(Handle hResult, const char *szName,
        ValueType *pType);

  }

  namespace ResultSetList
  {

    typedef void *Handle, *THandle;

    DECLSPECIFIER Handle __stdcall CreateHandle();
    DECLSPECIFIER void  __stdcall DestroyHandle(Handle hResultSetList);

    DECLSPECIFIER size_t __stdcall GetSize(Handle hResultSetList);
    DECLSPECIFIER ResultSet::Handle __stdcall GetResultSetHandle(
        Handle hResultSetList, size_t nIndex);

  }

  namespace Exception
  {

    typedef void *Handle, *THandle;

    DECLSPECIFIER int __stdcall GetErrorCode(Handle hExp);
    DECLSPECIFIER const char *__stdcall GetErrorMessage(Handle hExp);
    DECLSPECIFIER const char *__stdcall GetFormattedErrorMessage(Handle hExp);
    DECLSPECIFIER int __stdcall GetInterfaceErrorCode(Handle hExp);

  }

  namespace Executor
  {

    typedef void *Handle, *THandle;

    typedef void (*AsyncCallBack)(int nHandleId, ResultSet::Handle,
        const Exception::Handle, void *);

    typedef void (*BatchAsyncCallBack)(int nHandleId,
        ResultSetList::Handle, const Exception::Handle, void *);

    DECLSPECIFIER Handle __stdcall CreateHandle(DBGW3::Connector::Handle henv,
        const char *szNamespace = NULL);
    DECLSPECIFIER void __stdcall DestroyHandle(Handle hExecutor);

    DECLSPECIFIER bool __stdcall SetContainerKey(Handle hExecutor,
        const char *szKey);
    DECLSPECIFIER bool __stdcall Execute(Handle hExecutor, const char *szMethod,
        DBGW3::ParamSet::Handle hParam, DBGW3::ResultSet::Handle hResult);
    DECLSPECIFIER bool __stdcall Execute(Handle hExecutor, const char *szMethod,
        unsigned long ulMilliseconds, DBGW3::ParamSet::Handle hParam,
        DBGW3::ResultSet::Handle hResult);
    DECLSPECIFIER int __stdcall ExecuteAsync(Handle hExecutor,
        const char *szMethod, DBGW3::ParamSet::Handle hParam,
        AsyncCallBack pCallBack, void *pData = NULL);
    DECLSPECIFIER int __stdcall ExecuteAsync(Handle hExecutor,
        const char *szMethod, unsigned long ulMilliseconds,
        DBGW3::ParamSet::Handle hParam, AsyncCallBack pCallBack,
        void *pData = NULL);
    DECLSPECIFIER bool __stdcall ExecuteBatch(Handle hExecutor,
        const char *szMethod, DBGW3::ParamList::Handle hParamList,
        DBGW3::ResultSetList::Handle hResultSetList);
    DECLSPECIFIER bool __stdcall ExecuteBatch(Handle hExecutor,
        const char *szMethod, unsigned long ulMilliseconds,
        DBGW3::ParamList::Handle hParamList,
        DBGW3::ResultSetList::Handle hResultSetList);
    DECLSPECIFIER int __stdcall ExecuteBatchAsync(Handle hExecutor,
        const char *szMethod, DBGW3::ParamList::Handle hParamList,
        BatchAsyncCallBack pCallBack, void *pData = NULL);
    DECLSPECIFIER int __stdcall ExecuteBatchAsync(Handle hExecutor,
        const char *szMethod, unsigned long ulMilliseconds,
        DBGW3::ParamList::Handle hParamList, BatchAsyncCallBack pCallBack,
        void *pData = NULL);
    DECLSPECIFIER bool __stdcall BeginTransaction(Handle hExecutor);
    DECLSPECIFIER bool __stdcall BeginTransaction(Handle hExecutor,
        unsigned long ulWaitTimeMilSec);
    DECLSPECIFIER bool __stdcall CommitTransaction(Handle hExecutor);
    DECLSPECIFIER bool __stdcall CommitTransaction(Handle hExecutor,
        unsigned long ulWaitTimeMilSec);
    DECLSPECIFIER bool __stdcall RollbackTransaction(Handle hExecutor);
    DECLSPECIFIER bool __stdcall RollbackTransaction(Handle hExecutor,
        unsigned long ulWaitTimeMilSec);

  }

  namespace Mock
  {

    typedef void *Handle;

    /**
     * do not call this method directly.
     */
    DECLSPECIFIER Handle __stdcall GetInstance();

    DECLSPECIFIER void __stdcall AddCCIReturnErrorFault(Handle hMock,
        const char *szFaultFunction, _FAULT_TYPE type,
        int nReturnCode = 0, int nErrorCode = 0, const char *szErrorMessage = "");
    DECLSPECIFIER void __stdcall AddOCIReturnErrorFault(Handle hMock,
        const char *szFaultFunction, _FAULT_TYPE type,
        int nReturnCode = 0, int nErrorCode = 0, const char *szErrorMessage = "");
    DECLSPECIFIER void __stdcall AddCCISleepFault(Handle hMock,
        const char *szFaultFunction, _FAULT_TYPE type,
        unsigned long ulSleepMilSec);
    DECLSPECIFIER void __stdcall ClearFaultAll(Handle hMock);

  }

}

#endif
