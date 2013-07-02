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

#include "dbgw3/sql/mysql/MySQLCommon.h"
#include "dbgw3/sql/mysql/MySQLException.h"

namespace dbgw
{

  namespace sql
  {

    MySQLException::MySQLException(const _ExceptionContext &context) :
      Exception(context)
    {
      switch (getInterfaceErrorCode())
        {
        case ER_HOST_IS_BLOCKED:
        case ER_ABORTING_CONNECTION:
        case ER_NET_READ_ERROR:
        case ER_NET_READ_ERROR_FROM_PIPE:
        case ER_NEW_ABORTING_CONNECTION:
        case ER_TOO_MANY_USER_CONNECTIONS:
        case ER_ACCESS_DENIED_ERROR:
        case ER_SERVER_SHUTDOWN:
          setConnectionError(true);
          break;
        default:
          setConnectionError(false);
          break;
        }
    }

    MySQLException MySQLExceptionFactory::create(
        const std::string &errorMessage)
    {
      return create(-1, errorMessage);
    }

    MySQLException MySQLExceptionFactory::create(
        int nInterfaceErrorCode, const std::string &errorMessage)
    {
      _ExceptionContext context;
      context.nErrorCode = DBGW_ER_INTERFACE_ERROR;
      context.nInterfaceErrorCode = nInterfaceErrorCode;
      context.errorMessage = errorMessage;
      context.what = errorMessage;
      context.bConnectionError = false;

      std::stringstream buffer;
      buffer << "[" << context.nErrorCode << "]";
      buffer << "[" << context.nInterfaceErrorCode << "]";
      buffer << " " << context.errorMessage;
      context.what = buffer.str();

      return MySQLException(context);
    }

    MySQLException MySQLExceptionFactory::create(int nInterfaceErrorCode,
        MYSQL *pMySQL, const std::string &errorMessage)
    {
      _ExceptionContext context;
      context.nErrorCode = DBGW_ER_INTERFACE_ERROR;
      context.what = errorMessage;
      context.bConnectionError = false;

      if (pMySQL != NULL)
        {
          context.nInterfaceErrorCode = mysql_errno(pMySQL);
          context.errorMessage = mysql_error(pMySQL);

          if (context.nInterfaceErrorCode == 0)
            {
              context.nInterfaceErrorCode = nInterfaceErrorCode;
              context.errorMessage = errorMessage;
            }
        }
      else
        {
          context.nInterfaceErrorCode = nInterfaceErrorCode;
          context.errorMessage = errorMessage;
        }

      std::stringstream buffer;
      buffer << "[" << context.nErrorCode << "]";
      buffer << "[" << context.nInterfaceErrorCode << "]";
      buffer << " " << context.errorMessage;
      context.what = buffer.str();

      return MySQLException(context);
    }

    MySQLException MySQLExceptionFactory::create(
        int nInterfaceErrorCode, MYSQL_STMT *pMySQLStmt,
        const std::string &errorMessage)
    {
      _ExceptionContext context;
      context.nErrorCode = DBGW_ER_INTERFACE_ERROR;
      context.what = errorMessage;
      context.bConnectionError = false;
      context.nInterfaceErrorCode = 0;

      if (pMySQLStmt != NULL)
        {
          context.nInterfaceErrorCode = mysql_stmt_errno(pMySQLStmt);
          context.errorMessage = mysql_stmt_error(pMySQLStmt);

          if (context.nInterfaceErrorCode == 0)
            {
              context.nInterfaceErrorCode = nInterfaceErrorCode;
              context.errorMessage = errorMessage;
            }
        }

      std::stringstream buffer;
      buffer << "[" << context.nErrorCode << "]";
      buffer << "[" << context.nInterfaceErrorCode << "]";
      buffer << " " << context.errorMessage;
      context.what = buffer.str();

      return MySQLException(context);
    }

  }

}
