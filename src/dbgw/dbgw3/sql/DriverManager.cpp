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
#else
#include "dbgw3/sql/cubrid/CUBRIDConnection.h"
#endif

namespace dbgw
{

  namespace sql
  {

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

  }

}
