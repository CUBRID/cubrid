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
#include "dbgw3/SynchronizedResource.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/sql/Connection.h"
#include "dbgw3/sql/DriverManager.h"
#ifdef DBGW_ORACLE
#include "dbgw3/sql/oracle/OracleConnection.h"
#elif DBGW_MYSQL
#include "dbgw3/sql/mysql/MySQLConnection.h"
#elif DBGW_NBASE_T
#include <nbase.h>
#include "dbgw3/sql/cubrid/CUBRIDConnection.h"
#include "dbgw3/sql/nbase_t/NBaseTConnection.h"
#elif DBGW_ALL
#include "dbgw3/sql/oracle/OracleConnection.h"
#include "dbgw3/sql/mysql/MySQLConnection.h"
#include "dbgw3/sql/cubrid/CUBRIDConnection.h"
#else
#include "dbgw3/sql/cubrid/CUBRIDConnection.h"
#endif

namespace dbgw
{

  namespace sql
  {

    const char *getDbTypeString(DataBaseType dbType)
    {
      switch (dbType)
        {
        case DBGW_DB_TYPE_MYSQL:
          return "MySQL";
        case DBGW_DB_TYPE_ORACLE:
          return "Oracle";
        case DBGW_DB_TYPE_NBASE_T:
          return "NBASE-T";
        case DBGW_DB_TYPE_CUBRID:
        default:
          return "CUBRID";
        }
    }

    trait<Connection>::sp DriverManager::getConnection(const char *szUrl,
        DataBaseType dbType)
    {
      return getConnection(szUrl, NULL, NULL, dbType);
    }

    trait<Connection>::sp DriverManager::getConnection(const char *szUrl,
        const char *szUser, const char *szPassword, DataBaseType dbType)
    {
      clearException();

      try
        {
          trait<Connection>::sp pConnection;

#ifdef DBGW_ORACLE
          pConnection = trait<Connection>::sp(
              new OracleConnection(szUrl, szUser, szPassword));
#elif DBGW_MYSQL
          pConnection = trait<Connection>::sp(
              new MySQLConnection(szUrl, szUser, szPassword));
#elif DBGW_NBASE_T
          if (dbType == DBGW_DB_TYPE_NBASE_T)
            {
              pConnection = trait<Connection>::sp(
                  new NBaseTConnection(szUrl));
            }
          else
            {
              pConnection = trait<Connection>::sp(
                  new CUBRIDConnection(szUrl, szUser, szPassword));
            }
#elif DBGW_ALL
          if (dbType == DBGW_DB_TYPE_ORACLE)
            {
              pConnection = trait<Connection>::sp(
                  new OracleConnection(szUrl, szUser, szPassword));
            }
          else if (dbType == DBGW_DB_TYPE_MYSQL)
            {
              pConnection = trait<Connection>::sp(
                  new MySQLConnection(szUrl, szUser, szPassword));
            }
          else
            {
              pConnection = trait<Connection>::sp(
                  new CUBRIDConnection(szUrl, szUser, szPassword));
            }
#else
          pConnection = trait<Connection>::sp(
              new CUBRIDConnection(szUrl, szUser, szPassword));
#endif
          if (getLastErrorCode() != DBGW_ER_NO_ERROR)
            {
              throw getLastException();
            }

          return pConnection;
        }
      catch (Exception &e)
        {
          setLastException(e);
          return trait<Connection>::sp();
        }
    }

    std::string DriverUtil::escapeSingleQuote(const std::string &value)
    {
#ifdef DBGW_NBASE_T
      const static int DEFAULT_BUF_SIZE = 1024;
      char szDefaultBuffer[DEFAULT_BUF_SIZE];
      char *szExtraBuffer = NULL;
      char *p = NULL;
      int nValueSize = value.length();
      int nMaxValueSize = nValueSize * 2;

      if (nMaxValueSize + 1 > DEFAULT_BUF_SIZE)
        {
          szExtraBuffer = new char[nMaxValueSize + 1];
          p = szExtraBuffer;
        }
      else
        {
          p = szDefaultBuffer;
        }

      memset(p, 0, nMaxValueSize + 1);

      nbase_escape_sq_str((char *) value.c_str(), nValueSize, p,
          nMaxValueSize);

      std::string escapeValue(p);
      if (szExtraBuffer)
        {
          delete[] szExtraBuffer;
        }

      return escapeValue;
#else
      return value;
#endif
    }

  }

}
