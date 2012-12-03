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
#include "DBGWClient.h"
#include "DBGWAdapter.h"

namespace DBGW3
{

#define CONVERT_PREVIOUS_DBGWEXCEPTION(e, err) \
do { \
    convertPreviousDBGWException(e, err, #err); \
} while (0)

  static const char *DEFAULT_CONFIGURATION_PATH = "DBGWConnector3Config.xml";

  void convertPreviousDBGWException(const dbgw::DBGWException &e,
      int nDefaultErrorCode, const char *szDefaultErrorMessage)
  {
    switch (e.getErrorCode())
      {
      case dbgw::DBGW_ER_EXTERNAL_DBGW_INVALID_HANDLE:
        nDefaultErrorCode = DBGWCONNECTOR_INVALID_HANDLE;
        szDefaultErrorMessage = "DBGWCONNECTOR_INVALID_HANDLE";
        break;
      case dbgw::DBGW_ER_EXTERNAL_MEMORY_ALLOC_FAIL:
        nDefaultErrorCode = DBGWCONNECTOR_NOT_ENOUGH_MEMORY;
        szDefaultErrorMessage = "DBGWCONNECTOR_NOT_ENOUGH_MEMORY";
        break;
      case dbgw::DBGW_ER_CLIENT_ACCESS_DATA_BEFORE_FETCH:
        nDefaultErrorCode = DBGWCONNECTOR_NOT_PROPER_OP;
        szDefaultErrorMessage = "DBGWCONNECTOR_NOT_PROPER_OP";
        break;
      case dbgw::DBGW_ER_CLIENT_NO_MORE_DATA:
        nDefaultErrorCode = DBGWCONNECTOR_NOMORE_FETCH;
        szDefaultErrorMessage = "DBGWCONNECTOR_NOMORE_FETCH";
        break;
      case dbgw::DBGW_ER_CLIENT_ALREADY_IN_TRANSACTION:
        nDefaultErrorCode = DBGWCONNECTOR_ALREAY_IN_TRANSACTION;
        szDefaultErrorMessage = "DBGWCONNECTOR_ALREAY_IN_TRANSACTION";
        break;
      case dbgw::DBGW_ER_CLIENT_NOT_IN_TRANSACTION:
        nDefaultErrorCode = DBGWCONNECTOR_NOT_IN_TRANSACTION;
        szDefaultErrorMessage = "DBGWCONNECTOR_NOT_IN_TRANSACTION";
        break;
      }

    dbgw::DBGWPreviousException pe = dbgw::DBGWPreviousExceptionFactory::create(e,
        nDefaultErrorCode, szDefaultErrorMessage);
    DBGW_LOG_ERROR(pe.what());
    dbgw::setLastException(pe);
  }

  DECLSPECIFIER unsigned int __stdcall GetLastError()
  {
    return dbgw::getLastErrorCode();
  }

  DECLSPECIFIER int __stdcall GetLastInterfaceErrorCode()
  {
    return dbgw::getLastInterfaceErrorCode();
  }

  DECLSPECIFIER const char *__stdcall GetLastErrorMessage()
  {
    return dbgw::getLastErrorMessage();
  }

  DECLSPECIFIER const char *__stdcall GetFormattedErrorMessage()
  {
    return dbgw::getFormattedErrorMessage();
  }

  namespace Environment
  {

    DECLSPECIFIER Handle __stdcall CreateHandle(size_t nThreadPoolSize)
    {
      return CreateHandle(DEFAULT_CONFIGURATION_PATH);
    }

    DECLSPECIFIER Handle __stdcall CreateHandle(const char *szConfFileName)
    {
      dbgw::clearException();

      Handle hEnv = NULL;
      try
        {
          hEnv = new dbgw::DBGWConfiguration(szConfFileName);
          if (hEnv == NULL)
            {
              dbgw::MemoryAllocationFail e(sizeof(Handle));
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return hEnv;
        }
      catch (dbgw::DBGWException &e)
        {
          if (hEnv != NULL)
            {
              delete hEnv;
            }

          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_CREATE_FAILED);
          return NULL;
        }
    }

    DECLSPECIFIER void __stdcall DestroyHandle(Handle hEnv)
    {
      dbgw::clearException();

      try
        {
          if (hEnv == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          delete hEnv;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
        }
    }

    DECLSPECIFIER bool __stdcall LoadConnector(Handle hEnv)
    {
      return LoadConnector(hEnv, NULL);
    }

    DECLSPECIFIER bool __stdcall LoadQueryMapper(Handle hEnv)
    {
      return LoadQueryMapper(hEnv, dbgw::DBGW_QUERY_MAP_VER_30, NULL, false);
    }

    DECLSPECIFIER bool __stdcall LoadConnector(Handle hEnv,
        const char *szConnectorFileName)
    {
      dbgw::clearException();

      try
        {
          if (hEnv == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return hEnv->loadConnector(szConnectorFileName);
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall LoadQueryMapper(Handle hEnv,
        dbgw::DBGWQueryMapperVersion version, const char *szQueryMapFileName,
        bool bAppend)
    {
      dbgw::clearException();

      try
        {
          if (hEnv == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return hEnv->loadQueryMapper(version, szQueryMapFileName, bAppend);
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

  }

  namespace Connector
  {

    DECLSPECIFIER Handle __stdcall CreateHandle(Environment::Handle hEnv,
        const char *szMRSAddress)
    {
      return CreateHandle(hEnv);
    }

    DECLSPECIFIER Handle __stdcall CreateHandle(Environment::Handle hEnv)
    {
      dbgw::clearException();

      Handle hConnector = NULL;
      try
        {
          if (hEnv == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          hConnector = new Environment::Handle();
          if (hConnector == NULL)
            {
              dbgw::MemoryAllocationFail e(sizeof(Environment::Handle));
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          *hConnector = hEnv;

          return hConnector;
        }
      catch (dbgw::DBGWException &e)
        {
          if (hConnector != NULL)
            {
              delete hConnector;
            }

          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_CREATE_FAILED);
          return NULL;
        }
    }

    DECLSPECIFIER void __stdcall DestroyHandle(Handle hConnector)
    {
      dbgw::clearException();

      try
        {
          if (hConnector == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          delete hConnector;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
        }
    }

  }

  namespace ParamSet
  {

    DECLSPECIFIER Handle __stdcall CreateHandle()
    {
      dbgw::clearException();

      Handle hParam = NULL;
      try
        {
          hParam = new dbgw::DBGWClientParameter();
          if (hParam == NULL)
            {
              dbgw::MemoryAllocationFail e(sizeof(Handle));
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return hParam;
        }
      catch (dbgw::DBGWException &e)
        {
          if (hParam != NULL)
            {
              delete hParam;
            }

          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_CREATE_FAILED);
          return NULL;
        }
    }

    DECLSPECIFIER void __stdcall DestroyHandle(Handle hParam)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          delete hParam;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_HANDLE);
        }
    }

    DECLSPECIFIER bool __stdcall Clear(Handle hParam)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          hParam->clear();
          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_HANDLE);
          return false;
        }
    }

    DECLSPECIFIER size_t __stdcall Size(Handle hParam)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return hParam->size();
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_HANDLE);
          return 0;
        }
    }

    DECLSPECIFIER  bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, int nParamValue)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hParam->put(szParamName, nParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        int nParamValue)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hParam->set(nIndex, nParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, int64 nParamValue)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hParam->put(szParamName, nParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        int64 nParamValue)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hParam->set(nIndex, nParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }


    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, float fParamValue)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hParam->put(szParamName, fParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        float fParamValue)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hParam->set(nIndex, fParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, double dParamValue)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hParam->put(szParamName, dParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        double dParamValue)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hParam->set(nIndex, dParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, const char *szParamValue, size_t nLen)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hParam->put(szParamName, dbgw::DBGW_VAL_TYPE_STRING,
              (void *) szParamValue, szParamValue == NULL, nLen) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        const char *szParamValue, size_t nLen)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hParam->set(nIndex, dbgw::DBGW_VAL_TYPE_STRING,
              (void *) szParamValue, szParamValue == NULL, nLen) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, size_t nSize, const void *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hParam->put(szParamName, nSize, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        size_t nSize, const void *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hParam->set(nIndex, nSize, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

  }

  namespace ParamList
  {
    DECLSPECIFIER Handle __stdcall CreateHandle()
    {
      dbgw::clearException();

      Handle hParamList = NULL;
      try
        {
          hParamList = new dbgw::DBGWClientParameterList();
          if (hParamList == NULL)
            {
              dbgw::MemoryAllocationFail e(sizeof(Handle));
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return hParamList;
        }
      catch (dbgw::DBGWException &e)
        {
          if (hParamList != NULL)
            {
              delete hParamList;
            }

          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_CREATE_FAILED);
          return NULL;
        }
    }

    DECLSPECIFIER void __stdcall DestroyHandle(Handle hParamList)
    {
      dbgw::clearException();

      try
        {
          if (hParamList == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          delete hParamList;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_HANDLE);
        }
    }

    DECLSPECIFIER size_t __stdcall GetSize(Handle hParamList)
    {
      dbgw::clearException();

      try
        {
          if (hParamList == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return hParamList->size();
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_HANDLE);
          return -1;
        }
    }

    DECLSPECIFIER bool __stdcall Add(Handle hParamList, ParamSet::Handle hParam)
    {
      dbgw::clearException();

      try
        {
          if (hParamList == NULL || hParam == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          hParamList->push_back(*hParam);
          return true;

        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }
  }

  namespace ResultSet
  {
    DECLSPECIFIER Handle __stdcall CreateHandle()
    {
      dbgw::clearException();

      Handle hResult = NULL;
      try
        {
          hResult = new dbgw::DBGWClientResultSetSharedPtr();
          if (hResult == NULL)
            {
              dbgw::MemoryAllocationFail e(sizeof(Handle));
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return hResult;
        }
      catch (dbgw::DBGWException &e)
        {
          if (hResult != NULL)
            {
              delete hResult;
            }

          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_CREATE_FAILED);
          return NULL;
        }
    }

    DECLSPECIFIER void  __stdcall DestroyHandle(Handle hResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          delete hResult;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_HANDLE);
        }
    }

    DECLSPECIFIER size_t __stdcall GetRowCount(Handle hResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          size_t nRowCount = (*hResult)->getRowCount();
          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return nRowCount;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_NOT_PROPER_OP);
          return 0;
        }
    }

    DECLSPECIFIER bool  __stdcall First(Handle hResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hResult)->first() == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_NOT_PROPER_OP);
          return false;
        }
    }

    DECLSPECIFIER bool  __stdcall Fetch(Handle hResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hResult)->next() == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_NOT_PROPER_OP);
          return false;
        }
    }

    DECLSPECIFIER size_t __stdcall GetAffectedCount(Handle hResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          size_t nAffectedRow = (*hResult)->getAffectedRow();
          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return nAffectedRow;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_NOT_PROPER_OP);
          return 0;
        }
    }

    DECLSPECIFIER bool __stdcall IsNeedFetch(Handle hResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return (*hResult)->isNeedFetch();
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_HANDLE);
          return 0;
        }
    }

    DECLSPECIFIER const dbgw::DBGWResultSetMetaDataSharedPtr __stdcall
    GetMetaDataList(Handle hResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          const dbgw::DBGWResultSetMetaDataSharedPtr pMetaData =
              (*hResult)->getMetaData();
          if (pMetaData == NULL)
            {
              throw dbgw::getLastException();
            }

          return pMetaData;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_NOT_PROPER_OP);
          return dbgw::DBGWResultSetMetaDataSharedPtr();
        }
    }

    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        int *pValue)
    {
      return GetColumn(hResult, nIndex, pValue);
    }

    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, int *pValue)
    {
      return GetColumn(hResult, szName, pValue);
    }

    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        int64 *pValue)
    {
      return GetColumn(hResult, nIndex, pValue);
    }

    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, int64 *pValue)
    {
      return GetColumn(hResult, szName, pValue);
    }

    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        float *pValue)
    {
      return GetColumn(hResult, nIndex, pValue);
    }

    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, float *pValue)
    {
      return GetColumn(hResult, szName, pValue);
    }

    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        double *pValue)
    {
      return GetColumn(hResult, nIndex, pValue);
    }

    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, double *pValue)
    {
      return GetColumn(hResult, szName, pValue);
    }

    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        char *szBuffer, int BufferSize, size_t *pLen)
    {
      return GetColumn(hResult, nIndex, szBuffer, BufferSize, pLen);
    }

    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, char *szBuffer, int BufferSize, size_t *pLen)
    {
      return GetColumn(hResult, szName, szBuffer, BufferSize, pLen);
    }

    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        size_t *pSize, char **pValue)
    {
      return GetColumn(hResult, nIndex, pSize, pValue);
    }

    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, size_t *pSize, char **pValue)
    {
      return GetColumn(hResult, szName, pSize, pValue);
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        int *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hResult)->getInt(nIndex, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        int *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hResult)->getInt(szName, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        int64 *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hResult)->getLong(nIndex, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        int64 *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hResult)->getLong(szName, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        float *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hResult)->getFloat(nIndex, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        float *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hResult)->getFloat(szName, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        double *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hResult)->getDouble(nIndex, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        double *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hResult)->getDouble(szName, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        char *szBuffer, int BufferSize, size_t *pLen)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          const dbgw::DBGWValue *pValue = (*hResult)->getValue(nIndex);
          if (pValue == NULL)
            {
              throw dbgw::getLastException();
            }

          int nValueSize = pValue->size();

          if (pValue->isNull())
            {
              memset(szBuffer, 0, BufferSize);
            }
          else
            {
              if (nValueSize > BufferSize)
                {
                  dbgw::NotEnoughBufferException e;
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }

              char *szValue = NULL;
              pValue->getCString(&szValue);

              memcpy(szBuffer, szValue, nValueSize);
            }

          *pLen = nValueSize;
          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        char *szBuffer, int BufferSize, size_t *pLen)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          const dbgw::DBGWValue *pValue = (*hResult)->getValue(szName);
          if (pValue == NULL)
            {
              throw dbgw::getLastException();
            }

          int nValueSize = pValue->size();

          if (pValue->isNull())
            {
              memset(szBuffer, 0, BufferSize);
            }
          else
            {
              if (nValueSize > BufferSize)
                {
                  dbgw::NotEnoughBufferException e;
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }

              char *szValue = NULL;
              pValue->getCString(&szValue);

              memcpy(szBuffer, szValue, nValueSize);
            }

          *pLen = nValueSize;
          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        size_t *pSize, char **pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hResult)->getBytes(nIndex, pSize, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        size_t *pSize, char **pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hResult)->getBytes(szName, pSize, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_PARAMETER);
          return false;
        }
    }

  }

  namespace BatchResult
  {
    DECLSPECIFIER Handle __stdcall CreateHandle()
    {
      dbgw::clearException();

      Handle hResult = NULL;
      try
        {
          hResult = new dbgw::DBGWClientBatchResultSetSharedPtr();
          if (hResult == NULL)
            {
              dbgw::MemoryAllocationFail e(sizeof(Handle));
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return hResult;
        }
      catch (dbgw::DBGWException &e)
        {
          if (hResult != NULL)
            {
              delete hResult;
            }

          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_CREATE_FAILED);
          return NULL;
        }
    }

    DECLSPECIFIER void __stdcall DestroyHandle(Handle hBatchResult)
    {
      dbgw::clearException();

      try
        {
          if (hBatchResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          delete hBatchResult;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_HANDLE);
        }
    }

    DECLSPECIFIER bool __stdcall GetSize(Handle hBatchResult, int *pSize)
    {
      dbgw::clearException();

      try
        {
          if (hBatchResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          *pSize = (*hBatchResult)->size();
          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_HANDLE);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetExecuteStatus(Handle hBatchResult,
        dbgw::DBGWBatchExecuteStatus *pStatus)
    {
      dbgw::clearException();

      try
        {
          if (hBatchResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          *pStatus = (*hBatchResult)->getExecuteStatus();
          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_HANDLE);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetAffectedCount(Handle hBatchResult,
        int nIndex, int *pAffectedCount)
    {
      dbgw::clearException();

      try
        {
          if (hBatchResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hBatchResult)->getAffectedRow(nIndex, pAffectedCount) == false)
            {
              throw dbgw::getLastException();
            }
          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_NOT_PROPER_OP);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetErrorCode(Handle hBatchResult,
        int nIndex, int *pErrorCode)
    {
      dbgw::clearException();

      try
        {
          if (hBatchResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hBatchResult)->getErrorCode(nIndex, pErrorCode) == false)
            {
              throw dbgw::getLastException();
            }
          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_NOT_PROPER_OP);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetErrorMessage(Handle hBatchResult,
        int nIndex, const char **pErrorMessage)
    {
      dbgw::clearException();

      try
        {
          if (hBatchResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hBatchResult)->getErrorMessage(nIndex, pErrorMessage)
              == false)
            {
              throw dbgw::getLastException();
            }
          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_NOT_PROPER_OP);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetStatementType(Handle hBatchResult,
        int nIndex, dbgw::DBGWStatementType *pStatementType)
    {
      dbgw::clearException();

      try
        {
          if (hBatchResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if ((*hBatchResult)->getStatementType(nIndex, pStatementType) == false)
            {
              throw dbgw::getLastException();
            }
          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_NOT_PROPER_OP);
          return false;
        }
    }

  }

  namespace Executor
  {

    DECLSPECIFIER Handle __stdcall CreateHandle(
        DBGW3::Connector::Handle hConnector, const char *szNamespace)
    {
      dbgw::clearException();

      Handle hExecutor = NULL;
      try
        {
          if (hConnector == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          Environment::Handle hEnv = *hConnector;

          hExecutor = new dbgw::DBGWClient(*hEnv, szNamespace);
          if (hExecutor == NULL)
            {
              dbgw::MemoryAllocationFail e(sizeof(Handle));
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return hExecutor;
        }
      catch (dbgw::DBGWException &e)
        {
          if (hExecutor != NULL)
            {
              delete hExecutor;
            }

          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_CREATE_FAILED);
          return NULL;
        }
    }

    DECLSPECIFIER void __stdcall DestroyHandle(Handle hExecutor)
    {
      dbgw::clearException();

      try
        {
          if (hExecutor == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          delete hExecutor;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_INVALID_HANDLE);
        }
    }

    DECLSPECIFIER bool __stdcall Execute(Handle hExecutor, const char *szMethod,
        DBGW3::ParamSet::Handle hParam, DBGW3::ResultSet::Handle &hResult)
    {
      dbgw::clearException();

      try
        {
          if (hExecutor == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          *hResult = hExecutor->exec(szMethod, hParam);
          if (*hResult == NULL)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_EXEC_FAILED);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall ExecuteBatch(Handle hExecutor, const char *szMethod,
        DBGW3::ParamList::Handle hParamList, DBGW3::BatchResult::Handle &hBatchResult)
    {
      dbgw::clearException();

      try
        {
          if (hExecutor == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          *hBatchResult = hExecutor->execBatch(szMethod, *hParamList);
          if (*hBatchResult == NULL)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_EXEC_FAILED);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall BeginTransaction(Handle hExecutor)
    {
      dbgw::clearException();

      try
        {
          if (hExecutor == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hExecutor->setAutocommit(false) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_EXEC_FAILED);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall CommitTransaction(Handle hExecutor)
    {
      dbgw::clearException();

      try
        {
          if (hExecutor == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hExecutor->commit() == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_EXEC_FAILED);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall RollbackTransaction(Handle hExecutor)
    {
      dbgw::clearException();

      try
        {
          if (hExecutor == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (hExecutor->rollback() == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::DBGWException &e)
        {
          CONVERT_PREVIOUS_DBGWEXCEPTION(e, DBGWCONNECTOR_EXEC_FAILED);
          return false;
        }
    }

  }

}

namespace dbgw
{

  DBGWPreviousException::DBGWPreviousException(
      const dbgw::DBGWExceptionContext &context) throw() :
    dbgw::DBGWException(context)
  {
  }

  DBGWPreviousException::~DBGWPreviousException() throw()
  {
  }

  DBGWPreviousException DBGWPreviousExceptionFactory::create(
      const dbgw::DBGWException &e, int nPreviousErrorCdoe,
      const char *szPreviousErrorMessage)
  {
    dbgw::DBGWExceptionContext context =
    {
      nPreviousErrorCdoe, dbgw::DBGW_ER_NO_ERROR,
      szPreviousErrorMessage, "", false
    };

    std::stringstream buffer;
    buffer << "[" << context.nErrorCode << "]";
    buffer << " " << szPreviousErrorMessage;
    buffer << " (" << e.what() << ")";
    context.what = buffer.str();

    return DBGWPreviousException(context);
  }

}
