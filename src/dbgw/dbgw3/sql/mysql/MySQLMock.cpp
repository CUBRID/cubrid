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
#include "dbgw3/client/Mock.h"

#undef mysql_real_connect
#undef mysql_real_query
#undef mysql_autocommit
#undef mysql_commit
#undef mysql_rollback
#undef mysql_stmt_fetch
#undef mysql_stmt_init
#undef mysql_stmt_prepare
#undef mysql_stmt_execute
#undef mysql_stmt_bind_param
#undef mysql_stmt_bind_result
#undef mysql_stmt_store_result
#undef mysql_stmt_close
#undef mysql_stmt_free_result

namespace dbgw
{

  namespace sql
  {

    MYSQL *STDCALL mysql_mock_real_connect(MYSQL *mysql, const char *host,
        const char *user, const char *passwd, const char *db, unsigned int port,
        const char *unix_socket, unsigned long clientflag)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_real_connect");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return NULL;
        }

      MYSQL *pResult = mysql_real_connect(mysql, host, user, passwd, db, port,
          unix_socket, clientflag);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return NULL;
        }

      return pResult;
    }

    int STDCALL mysql_mock_real_query(MYSQL *mysql, const char *q,
        unsigned long length)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_real_query");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = mysql_real_query(mysql, q, length);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

    my_bool STDCALL mysql_mock_autocommit(MYSQL *mysql, my_bool auto_mode)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_autocommit");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      my_bool bResult = mysql_autocommit(mysql, auto_mode);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return bResult;
    }

    my_bool STDCALL mysql_mock_commit(MYSQL *mysql)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_commit");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      my_bool bResult = mysql_commit(mysql);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return bResult;
    }

    my_bool STDCALL mysql_mock_rollback(MYSQL *mysql)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_rollback");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      my_bool bResult = mysql_rollback(mysql);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return bResult;
    }

    int STDCALL mysql_mock_stmt_fetch(MYSQL_STMT *stmt)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_stmt_fetch");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = mysql_stmt_fetch(stmt);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

    MYSQL_STMT *STDCALL mysql_mock_stmt_init(MYSQL *mysql)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_stmt_init");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return NULL;
        }

      MYSQL_STMT *pResult = mysql_stmt_init(mysql);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return NULL;
        }

      return pResult;
    }

    int STDCALL mysql_mock_stmt_prepare(MYSQL_STMT *stmt, const char *query,
        unsigned long length)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_stmt_prepare");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = mysql_stmt_prepare(stmt, query, length);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

    int STDCALL mysql_mock_stmt_execute(MYSQL_STMT *stmt)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_stmt_execute");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = mysql_stmt_execute(stmt);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

    my_bool STDCALL mysql_mock_stmt_bind_param(MYSQL_STMT *stmt,
        MYSQL_BIND *bnd)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_stmt_bind_param");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = mysql_stmt_bind_param(stmt, bnd);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

    my_bool STDCALL mysql_mock_stmt_bind_result(MYSQL_STMT *stmt,
        MYSQL_BIND *bnd)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_stmt_bind_result");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = mysql_stmt_bind_result(stmt, bnd);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

    int STDCALL mysql_mock_stmt_store_result(MYSQL_STMT *stmt)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_stmt_store_result");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = mysql_stmt_store_result(stmt);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

    my_bool STDCALL mysql_mock_stmt_close(MYSQL_STMT *stmt)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_stmt_close");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = mysql_stmt_close(stmt);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

    my_bool STDCALL mysql_mock_stmt_free_result(MYSQL_STMT *stmt)
    {
      _MockManager *pMockManager = _MockManager::getInstance();

      trait<_Fault>::sp pFault = pMockManager->getFault("mysql_stmt_free_result");

      if (pFault != NULL
          && pFault->raiseFaultBeforeExecute())
        {
          return pFault->getReturnCode();
        }

      int nResult = mysql_stmt_free_result(stmt);

      if (pFault != NULL
          && pFault->raiseFaultAfterExecute())
        {
          return pFault->getReturnCode();
        }

      return nResult;
    }

  }

}
