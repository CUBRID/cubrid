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

#ifndef DATABASEINTERFACE_H_
#define DATABASEINTERFACE_H_

#include "dbgw3/Value.h"
#include "dbgw3/ValueSet.h"
#include "dbgw3/SynchronizedResource.h"

namespace dbgw
{

  namespace sql
  {

    enum ParameterMode
    {
      DBGW_PARAM_MODE_NONE = 0,
      DBGW_PARAM_MODE_OUT,
      DBGW_PARAM_MODE_IN,
      DBGW_PARAM_MODE_INOUT
    };

    enum StatementType
    {
      DBGW_STMT_TYPE_UNDEFINED = -1,
      DBGW_STMT_TYPE_SELECT = 0,
      DBGW_STMT_TYPE_PROCEDURE,
      DBGW_STMT_TYPE_UPDATE,
      DBGW_STMT_TYPE_SIZE
    };

    enum TransactionIsolarion
    {
      DBGW_TRAN_UNKNOWN = 0,
      DBGW_TRAN_READ_UNCOMMITED,
      DBGW_TRAN_READ_COMMITED,
      DBGW_TRAN_REPEATABLE_READ,
      DBGW_TRAN_SERIALIZABLE
    };

    class Connection;
    typedef trait<Connection>::sp ConnectionSharedPtr;

    class PreparedStatement;
    typedef trait<PreparedStatement>::sp PreparedStatementSharedPtr;

    class CallableStatement;
    typedef trait<CallableStatement>::sp CallableStatementSharedPtr;

    class ResultSet;
    typedef trait<ResultSet>::sp ResultSetSharedPtr;

    class ResultSetMetaData;
    typedef trait<ResultSetMetaData>::sp ResultSetMetaDataSharedPtr;

    class DriverManger;

  }

}

#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/sql/Connection.h"
#include "dbgw3/sql/Statement.h"
#include "dbgw3/sql/PreparedStatement.h"
#include "dbgw3/sql/CallableStatement.h"
#include "dbgw3/sql/ResultSet.h"
#include "dbgw3/sql/ResultSetMetaData.h"
#include "dbgw3/sql/DriverManager.h"

#endif
