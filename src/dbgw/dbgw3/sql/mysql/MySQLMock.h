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

#ifndef MYSQLMOCK_H_
#define MYSQLMOCK_H_

#ifdef BUILD_MOCK
#define mysql_real_connect mysql_mock_real_connect
#define mysql_real_query mysql_mock_real_query
#define mysql_autocommit mysql_mock_autocommit
#define mysql_commit mysql_mock_commit
#define mysql_rollback mysql_mock_rollback
#define mysql_stmt_fetch mysql_mock_stmt_fetch
#define mysql_stmt_init mysql_mock_stmt_init
#define mysql_stmt_prepare mysql_mock_stmt_prepare
#define mysql_stmt_execute mysql_mock_stmt_execute
#define mysql_stmt_bind_param mysql_mock_stmt_bind_param
#define mysql_stmt_bind_result mysql_mock_stmt_bind_result
#define mysql_stmt_store_result mysql_mock_stmt_store_result
#define mysql_stmt_close mysql_mock_stmt_close
#define mysql_stmt_free_result mysql_mock_stmt_free_result
#endif

namespace dbgw
{

  namespace sql
  {

    MYSQL *STDCALL mysql_mock_real_connect(MYSQL *mysql, const char *host,
        const char *user, const char *passwd, const char *db, unsigned int port,
        const char *unix_socket, unsigned long clientflag);
    int STDCALL mysql_mock_real_query(MYSQL *mysql, const char *q,
        unsigned long length);
    my_bool STDCALL mysql_mock_autocommit(MYSQL *mysql, my_bool auto_mode);
    my_bool STDCALL mysql_mock_commit(MYSQL *mysql);
    my_bool STDCALL mysql_mock_rollback(MYSQL *mysql);
    int STDCALL mysql_mock_stmt_fetch(MYSQL_STMT *stmt);
    MYSQL_STMT *STDCALL mysql_mock_stmt_init(MYSQL *mysql);
    int STDCALL mysql_mock_stmt_prepare(MYSQL_STMT *stmt, const char *query,
        unsigned long length);
    int STDCALL mysql_mock_stmt_execute(MYSQL_STMT *stmt);
    my_bool STDCALL mysql_mock_stmt_bind_param(MYSQL_STMT *stmt,
        MYSQL_BIND *bnd);
    my_bool STDCALL mysql_mock_stmt_bind_result(MYSQL_STMT *stmt,
        MYSQL_BIND *bnd);
    int STDCALL mysql_mock_stmt_store_result(MYSQL_STMT *stmt);
    my_bool STDCALL mysql_mock_stmt_close(MYSQL_STMT *stmt);
    my_bool STDCALL mysql_mock_stmt_free_result(MYSQL_STMT *stmt);

  }

}

#endif
