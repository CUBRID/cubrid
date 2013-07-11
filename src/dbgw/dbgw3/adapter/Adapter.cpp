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

#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Lob.h"
#include "dbgw3/Value.h"
#include "dbgw3/ValueSet.h"
#include "dbgw3/SynchronizedResource.h"
#include "dbgw3/system/Mutex.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/sql/ResultSetMetaData.h"
#include "dbgw3/adapter/Adapter.h"
#include "dbgw3/client/Client.h"
#include "dbgw3/client/Mock.h"

static const char *DEFAULT_CONFIGURATION_PATH = "DBGWConnector3Config.xml";

namespace DBGW3
{

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
          hEnv = new dbgw::Configuration(szConfFileName);
          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return hEnv;
        }
      catch (dbgw::Exception &e)
        {
          if (hEnv != NULL)
            {
              delete(dbgw::Configuration *) hEnv;
            }

          dbgw::setLastException(e);
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

          delete(dbgw::Configuration *) hEnv;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
        }
    }

    DECLSPECIFIER void __stdcall SetDefaultTimeout(Handle hEnv,
        unsigned long ulMilliseconds)
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

          ((dbgw::Configuration *) hEnv)->setWaitTimeMilSec(ulMilliseconds);
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
        }
    }

    DECLSPECIFIER bool __stdcall GetDefaultTimeout(Handle hEnv,
        unsigned long *pTimeout)
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

          *pTimeout = ((dbgw::Configuration *) hEnv)->getWaitTimeMilSec();
          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall LoadConnector(Handle hEnv)
    {
      return LoadConnector(hEnv, NULL);
    }

    DECLSPECIFIER bool __stdcall LoadQueryMapper(Handle hEnv)
    {
      return LoadQueryMapper(hEnv, DBGW_QUERY_MAP_VER_30, NULL, false);
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

          return ((dbgw::Configuration *) hEnv)->loadConnector(
              szConnectorFileName);
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall LoadQueryMapper(Handle hEnv,
        QueryMapperVersion version, const char *szQueryMapFileName,
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

          return ((dbgw::Configuration *) hEnv)->loadQueryMapper(
              (dbgw::QueryMapperVersion) version, szQueryMapFileName, bAppend);
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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
          *hConnector = hEnv;

          return hConnector;
        }
      catch (dbgw::Exception &e)
        {
          if (hConnector != NULL)
            {
              delete hConnector;
            }

          dbgw::setLastException(e);
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
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
        }
    }

    DECLSPECIFIER void __stdcall SetDefaultTimeout(Handle hConnector,
        unsigned long ulMilliseconds)
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

          return Environment::SetDefaultTimeout(*hConnector, ulMilliseconds);
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
        }
    }

    DECLSPECIFIER bool __stdcall GetDefaultTimeout(Handle hConnector,
        unsigned long *pulMilliseconds)
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

          return Environment::GetDefaultTimeout(*hConnector, pulMilliseconds);
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
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
          hParam = new dbgw::_Parameter();
          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return hParam;
        }
      catch (dbgw::Exception &e)
        {
          if (hParam != NULL)
            {
              delete(dbgw::_Parameter *) hParam;
            }

          dbgw::setLastException(e);
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

          delete(dbgw::_Parameter *) hParam;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          ((dbgw::_Parameter *) hParam)->clear();
          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          return ((dbgw::_Parameter *) hParam)->size();
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          if (((dbgw::_Parameter *) hParam)->put(szParamName,
              nParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          if (((dbgw::_Parameter *) hParam)->set(nIndex, nParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, char cParamValue)
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

          if (((dbgw::_Parameter *) hParam)->put(szParamName,
              cParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        char cParamValue)
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

          if (((dbgw::_Parameter *) hParam)->set(nIndex, cParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          if (((dbgw::_Parameter *) hParam)->put(szParamName,
              nParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          if (((dbgw::_Parameter *) hParam)->set(nIndex, nParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          if (((dbgw::_Parameter *) hParam)->put(szParamName,
              fParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          if (((dbgw::_Parameter *) hParam)->set(nIndex, fParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          if (((dbgw::_Parameter *) hParam)->put(szParamName,
              dParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          if (((dbgw::_Parameter *) hParam)->set(nIndex, dParamValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          if (((dbgw::_Parameter *) hParam)->put(szParamName,
              dbgw::DBGW_VAL_TYPE_STRING, (void *) szParamValue,
              szParamValue == NULL, nLen) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          if (((dbgw::_Parameter *) hParam)->set(nIndex,
              dbgw::DBGW_VAL_TYPE_STRING, (void *) szParamValue,
              szParamValue == NULL, nLen) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          if (((dbgw::_Parameter *) hParam)->put(szParamName, nSize,
              pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          if (((dbgw::_Parameter *) hParam)->set(nIndex, nSize, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, ValueType type, struct tm &value)
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

          if (((dbgw::_Parameter *) hParam)->put(szParamName,
              (dbgw::ValueType) type, value) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        ValueType type, struct tm &value)
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

          if (((dbgw::_Parameter *) hParam)->set(nIndex,
              (dbgw::ValueType) type, value) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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
          hParamList = new dbgw::_ParameterList();
          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return hParamList;
        }
      catch (dbgw::Exception &e)
        {
          if (hParamList != NULL)
            {
              delete(dbgw::_ParameterList *) hParamList;
            }

          dbgw::setLastException(e);
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

          delete(dbgw::_ParameterList *) hParamList;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          return ((dbgw::_ParameterList *)hParamList)->size();
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          ((dbgw::_ParameterList *) hParamList)->push_back(
              *((dbgw::_Parameter *) hParam));
          return true;

        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

  }

  namespace ResultSetMeta
  {

    DECLSPECIFIER void __stdcall DestroyHandle(Handle hMeta)
    {
      dbgw::clearException();

      try
        {
          if (hMeta == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetMetaDataSharedPtr *pMeta =
              (dbgw::ClientResultSetMetaDataSharedPtr *) hMeta;
          delete pMeta;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
        }
    }

    DECLSPECIFIER size_t __stdcall GetColumnCount(Handle hMeta)
    {
      dbgw::clearException();

      try
        {
          if (hMeta == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetMetaDataSharedPtr *pMeta =
              (dbgw::ClientResultSetMetaDataSharedPtr *) hMeta;

          return (*pMeta)->getColumnCount();
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return 0;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumnName(Handle hMeta, size_t nIndex,
        const char **szName)
    {
      dbgw::clearException();

      try
        {
          if (hMeta == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetMetaDataSharedPtr *pMeta =
              (dbgw::ClientResultSetMetaDataSharedPtr *) hMeta;

          return (*pMeta)->getColumnName(nIndex, szName);
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumnType(Handle hMeta, size_t nIndex,
        ValueType *pType)
    {
      dbgw::clearException();

      try
        {
          if (hMeta == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetMetaDataSharedPtr *pMeta =
              (dbgw::ClientResultSetMetaDataSharedPtr *) hMeta;

          dbgw::ValueType type;
          if ((*pMeta)->getColumnType(nIndex, &type) == false)
            {
              throw dbgw::getLastException();
            }

          *pType = (ValueType) type;
          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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
          hResult = new dbgw::ClientResultSetSharedPtr();
          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return hResult;
        }
      catch (dbgw::Exception &e)
        {
          if (hResult != NULL)
            {
              delete(dbgw::ClientResultSetSharedPtr *) hResult;
            }

          dbgw::setLastException(e);
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

          delete(dbgw::ClientResultSetSharedPtr *) hResult;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
        }
    }

    DECLSPECIFIER size_t __stdcall GetRowCount(Handle hResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          size_t nRowCount = (*pResult)->getRowCount();
          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return nRowCount;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return -1;
        }
    }

    DECLSPECIFIER bool  __stdcall First(Handle hResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->first() == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool  __stdcall Fetch(Handle hResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->next() == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER size_t __stdcall GetAffectedCount(Handle hResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          size_t nAffectedRow = (*pResult)->getAffectedRow();
          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return nAffectedRow;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return -1;
        }
    }

    DECLSPECIFIER bool __stdcall IsNeedFetch(Handle hResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          return (*pResult)->isNeedFetch();
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER ResultSetMeta::Handle __stdcall GetMetaData(Handle hResult)
    {
      dbgw::clearException();

      dbgw::ClientResultSetMetaDataSharedPtr *pMeta = NULL;
      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          pMeta = new dbgw::ClientResultSetMetaDataSharedPtr();
          *pMeta = (*pResult)->getMetaData();
          if (*pMeta == NULL)
            {
              throw dbgw::getLastException();
            }

          return pMeta;
        }
      catch (dbgw::Exception &e)
        {
          if (pMeta != NULL)
            {
              delete pMeta;
            }

          dbgw::setLastException(e);
          return NULL;
        }
    }

    DECLSPECIFIER bool __stdcall IsNull(Handle hResult, int nIndex,
        bool *pIsNull)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->isNull(nIndex, pIsNull) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall IsNull(Handle hResult, const char *szName,
        bool *pIsNull)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->isNull(szName, pIsNull) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
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
        size_t *pSize, const char **pValue)
    {
      return GetColumn(hResult, nIndex, pSize, pValue);
    }

    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, size_t *pSize, const char **pValue)
    {
      return GetColumn(hResult, szName, pSize, pValue);
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        int *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->getInt(nIndex, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        int *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->getInt(szName, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        int64 *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->getLong(nIndex, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        int64 *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->getLong(szName, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        float *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->getFloat(nIndex, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        float *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->getFloat(szName, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        double *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->getDouble(nIndex, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        double *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->getDouble(szName, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        char *szBuffer, int BufferSize, size_t *pLen)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          const dbgw::Value *pValue = (*pResult)->getValue(nIndex);
          if (pValue == NULL)
            {
              throw dbgw::getLastException();
            }

          int nValueSize = pValue->getLength();

          if (pValue->isNull())
            {
              memset(szBuffer, 0, BufferSize);
            }
          else
            {
              if (nValueSize >= BufferSize)
                {
                  dbgw::NotEnoughBufferException e;
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }

              const char *szValue = NULL;
              pValue->getCString(&szValue);

              memcpy(szBuffer, szValue, nValueSize + 1);
            }

          *pLen = nValueSize;
          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        char *szBuffer, int BufferSize, size_t *pLen)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          const dbgw::Value *pValue = (*pResult)->getValue(szName);
          if (pValue == NULL)
            {
              throw dbgw::getLastException();
            }

          int nValueSize = pValue->getLength();

          if (pValue->isNull())
            {
              memset(szBuffer, 0, BufferSize);
            }
          else
            {
              if (nValueSize >= BufferSize)
                {
                  dbgw::NotEnoughBufferException e;
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }

              const char *szValue = NULL;
              pValue->getCString(&szValue);

              memcpy(szBuffer, szValue, nValueSize + 1);
            }

          *pLen = nValueSize;
          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        size_t *pSize, const char **pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->getBytes(nIndex, pSize, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        size_t *pSize, const char **pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->getBytes(szName, pSize, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        struct tm *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->getDateTime(nIndex, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        struct tm *pValue)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          if ((*pResult)->getDateTime(szName, pValue) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetResultSet(Handle hResult, int nIndex,
        Handle hOutResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL
              || hOutResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          dbgw::ClientResultSetSharedPtr *pOutResult =
              (dbgw::ClientResultSetSharedPtr *) hOutResult;

          *pOutResult = (*pResult)->getClientResultSet(nIndex);
          if (*pOutResult == NULL)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetResultSet(Handle hResult,
        const char *szName, Handle hOutResult)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL
              || hOutResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          dbgw::ClientResultSetSharedPtr *pOutResult =
              (dbgw::ClientResultSetSharedPtr *) hOutResult;

          *pOutResult = (*pResult)->getClientResultSet(szName);
          if (*pOutResult == NULL)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetType(Handle hResult, int nIndex,
        ValueType *pType)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          dbgw::ValueType type;
          if ((*pResult)->getType(nIndex, &type) == false)
            {
              throw dbgw::getLastException();
            }

          *pType = (ValueType) type;

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall GetType(Handle hResult, const char *szName,
        ValueType *pType)
    {
      dbgw::clearException();

      try
        {
          if (hResult == NULL
              || *(dbgw::ClientResultSetSharedPtr *) hResult == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          dbgw::ValueType type;
          if ((*pResult)->getType(szName, &type) == false)
            {
              throw dbgw::getLastException();
            }

          *pType = (ValueType) type;

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

  }

  namespace ResultSetList
  {

    DECLSPECIFIER Handle __stdcall CreateHandle()
    {
      dbgw::clearException();

      Handle hResultSetList = NULL;
      try
        {
          hResultSetList = new dbgw::ClientResultSetSharedPtrList();
          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return hResultSetList;
        }
      catch (dbgw::Exception &e)
        {
          if (hResultSetList != NULL)
            {
              delete(dbgw::ClientResultSetSharedPtrList *) hResultSetList;
            }

          dbgw::setLastException(e);
          return NULL;
        }
    }

    DECLSPECIFIER void  __stdcall DestroyHandle(Handle hResultSetList)
    {
      dbgw::clearException();

      try
        {
          if (hResultSetList == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          delete(dbgw::ClientResultSetSharedPtrList *) hResultSetList;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
        }
    }

    DECLSPECIFIER size_t __stdcall GetSize(Handle hResultSetList)
    {
      dbgw::clearException();

      try
        {
          if (hResultSetList == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtrList *pResultSetList =
              (dbgw::ClientResultSetSharedPtrList *) hResultSetList;
          return pResultSetList->size();
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return 0;
        }
    }

    DECLSPECIFIER ResultSet::Handle __stdcall GetResultSetHandle(
        Handle hResultSetList, size_t nIndex)
    {
      dbgw::clearException();

      try
        {
          if (hResultSetList == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          dbgw::ClientResultSetSharedPtrList *pResultSetList =
              (dbgw::ClientResultSetSharedPtrList *) hResultSetList;

          if (nIndex >= pResultSetList->size())
            {
              dbgw::ArrayIndexOutOfBoundsException e(nIndex, "ResultSetList");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return &(*pResultSetList)[nIndex];
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return NULL;
        }
    }

  }

  namespace Exception
  {

    DECLSPECIFIER int __stdcall GetErrorCode(Handle hExp)
    {
      return ((dbgw::Exception *) hExp)->getErrorCode();
    }

    DECLSPECIFIER const char *__stdcall GetErrorMessage(Handle hExp)
    {
      return ((dbgw::Exception *) hExp)->getErrorMessage();
    }

    DECLSPECIFIER const char *__stdcall GetFormattedErrorMessage(Handle hExp)
    {
      return ((dbgw::Exception *) hExp)->what();
    }

    DECLSPECIFIER int __stdcall GetInterfaceErrorCode(Handle hExp)
    {
      return ((dbgw::Exception *) hExp)->getInterfaceErrorCode();
    }

  }

  namespace Executor
  {

    typedef boost::unordered_map<int, AsyncCallBack, boost::hash<int>,
            dbgw::func::compareInt> AsyncCallbackHashMap;
    typedef boost::unordered_map<int, BatchAsyncCallBack, boost::hash<int>,
            dbgw::func::compareInt> BatchAsyncCallbackHashMap;

    dbgw::system::_Mutex g_asyncCallbackMutex;
    AsyncCallbackHashMap g_asyncCallbackMap;

    dbgw::system::_Mutex g_batchAsyncCallbackMutex;
    BatchAsyncCallbackHashMap g_batchAsyncCallbackMap;

    void defaultAsyncCallback(int nHandleId,
        dbgw::trait<dbgw::ClientResultSet>::sp pResult,
        const dbgw::Exception &e)
    {
      g_asyncCallbackMutex.lock();
      AsyncCallbackHashMap::iterator it = g_asyncCallbackMap.find(nHandleId);
      g_asyncCallbackMutex.unlock();

      if (it == g_asyncCallbackMap.end())
        {
          return;
        }

      if (pResult == NULL)
        {
          (*it->second)(nHandleId, (ResultSet::Handle) NULL,
              (const Exception::Handle) &e);
        }
      else
        {
          (*it->second)(nHandleId, (ResultSet::Handle) &pResult,
              (const Exception::Handle) &e);
        }
    }

    void defaultBatchAsyncCallback(int nHandleId,
        dbgw::trait<dbgw::ClientResultSet>::spvector resultSetList,
        const dbgw::Exception &e)
    {
      g_batchAsyncCallbackMutex.lock();
      BatchAsyncCallbackHashMap::iterator it =
          g_batchAsyncCallbackMap.find(nHandleId);
      g_batchAsyncCallbackMutex.unlock();

      if (it == g_batchAsyncCallbackMap.end())
        {
          return;
        }

      (*it->second)(nHandleId, &resultSetList, (const Exception::Handle) &e);
    }

    DECLSPECIFIER Handle __stdcall CreateHandle(
        DBGW3::Connector::Handle hConnector, const char *szNamespace)
    {
      dbgw::clearException();

      dbgw::Client *hExecutor = NULL;
      try
        {
          if (hConnector == NULL)
            {
              dbgw::InvalidHandleException e;
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          Environment::Handle hEnv = (*hConnector);
          dbgw::Configuration *pConf = (dbgw::Configuration *) hEnv;

          hExecutor = new dbgw::Client(*pConf, szNamespace);
          if (dbgw::getLastErrorCode() != dbgw::DBGW_ER_NO_ERROR)
            {
              throw dbgw::getLastException();
            }

          return hExecutor;
        }
      catch (dbgw::Exception &e)
        {
          if (hExecutor != NULL)
            {
              delete(dbgw::Client *) hExecutor;
            }

          dbgw::setLastException(e);
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

          delete(dbgw::Client *) hExecutor;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
        }
    }

    DECLSPECIFIER bool __stdcall Execute(Handle hExecutor, const char *szMethod,
        DBGW3::ParamSet::Handle hParam, DBGW3::ResultSet::Handle hResult)
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          dbgw::_Parameter *pParam = (dbgw::_Parameter *) hParam;
          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          *pResult = pClient->exec(szMethod, pParam);
          if (*pResult == NULL)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall Execute(Handle hExecutor, const char *szMethod,
        unsigned long ulMilliseconds, DBGW3::ParamSet::Handle hParam,
        DBGW3::ResultSet::Handle hResult)
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          dbgw::_Parameter *pParam = (dbgw::_Parameter *) hParam;
          dbgw::ClientResultSetSharedPtr *pResult =
              (dbgw::ClientResultSetSharedPtr *) hResult;

          *pResult = pClient->exec(szMethod, pParam, ulMilliseconds);
          if (*pResult == NULL)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER int __stdcall ExecuteAsync(Handle hExecutor,
        const char *szMethod, DBGW3::ParamSet::Handle hParam,
        AsyncCallBack pCallBack)
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          dbgw::_Parameter *pParam = (dbgw::_Parameter *) hParam;

          int nHandleId = pClient->execAsync(szMethod, pParam,
              defaultAsyncCallback);
          if (nHandleId < 0)
            {
              throw dbgw::getLastException();
            }

          g_asyncCallbackMutex.lock();
          g_asyncCallbackMap[nHandleId] = pCallBack;
          g_asyncCallbackMutex.unlock();

          return nHandleId;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return -1;
        }
    }

    DECLSPECIFIER int __stdcall ExecuteAsync(Handle hExecutor,
        const char *szMethod, unsigned long ulMilliseconds,
        DBGW3::ParamSet::Handle hParam, AsyncCallBack pCallBack)
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          dbgw::_Parameter *pParam = (dbgw::_Parameter *) hParam;

          int nHandleId = pClient->execAsync(szMethod, pParam,
              defaultAsyncCallback, ulMilliseconds);
          if (nHandleId < 0)
            {
              throw dbgw::getLastException();
            }

          g_asyncCallbackMutex.lock();
          g_asyncCallbackMap[nHandleId] = pCallBack;
          g_asyncCallbackMutex.unlock();

          return nHandleId;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return -1;
        }
    }

    DECLSPECIFIER bool __stdcall ExecuteBatch(Handle hExecutor,
        const char *szMethod, DBGW3::ParamList::Handle hParamList,
        DBGW3::ResultSetList::Handle hResultSetList)
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          dbgw::_ParameterList *pParamList = (dbgw::_ParameterList *) hParamList;
          dbgw::ClientResultSetSharedPtrList *pResultSetList =
              (dbgw::ClientResultSetSharedPtrList *) hResultSetList;

          *pResultSetList = pClient->execBatch(szMethod, *pParamList);
          if (pResultSetList->empty())
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall ExecuteBatch(Handle hExecutor,
        const char *szMethod, unsigned long ulMilliseconds,
        DBGW3::ParamList::Handle hParamList,
        DBGW3::ResultSetList::Handle hResultSetList)
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          dbgw::_ParameterList *pParamList = (dbgw::_ParameterList *) hParamList;
          dbgw::ClientResultSetSharedPtrList *pResultSetList =
              (dbgw::ClientResultSetSharedPtrList *) hResultSetList;

          *pResultSetList = pClient->execBatch(szMethod, *pParamList,
              ulMilliseconds);
          if (pResultSetList->empty())
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER int __stdcall ExecuteBatchAsync(Handle hExecutor,
        const char *szMethod, DBGW3::ParamList::Handle hParamList,
        BatchAsyncCallBack pCallBack)
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          dbgw::_ParameterList *pParamList = (dbgw::_ParameterList *) hParamList;

          int nHandleId = pClient->execBatchAsync(szMethod, *pParamList,
              defaultBatchAsyncCallback);
          if (nHandleId < 0)
            {
              throw dbgw::getLastException();
            }

          g_batchAsyncCallbackMutex.lock();
          g_batchAsyncCallbackMap[nHandleId] = pCallBack;
          g_batchAsyncCallbackMutex.unlock();

          return nHandleId;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return -1;
        }
    }

    DECLSPECIFIER int __stdcall ExecuteBatchAsync(Handle hExecutor,
        const char *szMethod, unsigned long ulMilliseconds,
        DBGW3::ParamList::Handle hParamList, BatchAsyncCallBack pCallBack)
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          dbgw::_ParameterList *pParamList = (dbgw::_ParameterList *) hParamList;

          int nHandleId = pClient->execBatchAsync(szMethod, *pParamList,
              defaultBatchAsyncCallback, ulMilliseconds);
          if (nHandleId < 0)
            {
              throw dbgw::getLastException();
            }

          g_batchAsyncCallbackMutex.lock();
          g_batchAsyncCallbackMap[nHandleId] = pCallBack;
          g_batchAsyncCallbackMutex.unlock();

          return nHandleId;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return -1;
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          if (pClient->setAutocommit(false) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall BeginTransaction(Handle hExecutor,
        unsigned long ulWaitTimeMilSec)
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          if (pClient->setAutocommit(false, ulWaitTimeMilSec) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          if (pClient->commit() == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall CommitTransaction(Handle hExecutor,
        unsigned long ulWaitTimeMilSec)
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          if (pClient->commit(ulWaitTimeMilSec) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          if (pClient->rollback() == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

    DECLSPECIFIER bool __stdcall RollbackTransaction(Handle hExecutor,
        unsigned long ulWaitTimeMilSec)
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

          dbgw::Client *pClient = (dbgw::Client *) hExecutor;
          if (pClient->rollback(ulWaitTimeMilSec) == false)
            {
              throw dbgw::getLastException();
            }

          return true;
        }
      catch (dbgw::Exception &e)
        {
          dbgw::setLastException(e);
          return false;
        }
    }

  }

  namespace Mock
  {

    DECLSPECIFIER Handle __stdcall GetInstance()
    {
      return dbgw::_MockManager::getInstance();
    }

    DECLSPECIFIER void __stdcall AddCCIReturnErrorFault(Handle hMock,
        const char *szFaultFunction, _FAULT_TYPE type,
        int nReturnCode, int nErrorCode, const char *szErrorMessage)
    {
      dbgw::_MockManager *pMock = (dbgw::_MockManager *) hMock;
      pMock->addCCIReturnErrorFault(szFaultFunction, (dbgw::_FAULT_TYPE) type,
          nReturnCode, nErrorCode, szErrorMessage);
    }

    DECLSPECIFIER void __stdcall AddOCIReturnErrorFault(Handle hMock,
        const char *szFaultFunction, _FAULT_TYPE type,
        int nReturnCode, int nErrorCode, const char *szErrorMessage)
    {
      dbgw::_MockManager *pMock = (dbgw::_MockManager *) hMock;
      pMock->addOCIReturnErrorFault(szFaultFunction, (dbgw::_FAULT_TYPE) type,
          nReturnCode, nErrorCode, szErrorMessage);
    }

    DECLSPECIFIER void __stdcall AddCCISleepFault(Handle hMock,
        const char *szFaultFunction, _FAULT_TYPE type,
        unsigned long ulSleepMilSec)
    {
      dbgw::_MockManager *pMock = (dbgw::_MockManager *) hMock;
      pMock->addCCISleepFault(szFaultFunction, (dbgw::_FAULT_TYPE) type,
          ulSleepMilSec);
    }

    DECLSPECIFIER void __stdcall ClearFaultAll(Handle hMock)
    {
      dbgw::_MockManager *pMock = (dbgw::_MockManager *) hMock;
      pMock->clearFaultAll();
    }

  }

}
