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
#include "dbgw3/sql/mysql/MySQLResultSetMetaData.h"

const int MAX_BYTES_SIZE = 16 * 1024 * 1024;

namespace dbgw
{

  namespace sql
  {

    MySQLResultSetMetaData::MySQLResultSetMetaData(MYSQL_STMT *pMySQLStmt)
    {
      MYSQL_RES *pMySQLResult = mysql_stmt_result_metadata(pMySQLStmt);
      if (pMySQLResult == NULL)
        {
          MySQLException e = MySQLExceptionFactory::create(-1, pMySQLStmt,
              "Failed to get resultset metadata.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      int nColumnCount = mysql_num_fields(pMySQLResult);

      MYSQL_FIELD *pMySQLField = NULL;
      for (int i = 0; i < nColumnCount; i++)
        {
          pMySQLField = mysql_fetch_field(pMySQLResult);

          _MySQLResultSetMetaDataRaw md;
          md.name = pMySQLField->name;
          md.mysqlType = pMySQLField->type;

          switch (md.mysqlType)
            {
            case MYSQL_TYPE_INT24:
            case MYSQL_TYPE_TINY:
            case MYSQL_TYPE_SHORT:
            case MYSQL_TYPE_LONG:
              md.type = DBGW_VAL_TYPE_INT;
              md.length = sizeof(int);
              break;
            case MYSQL_TYPE_FLOAT:
              md.type = DBGW_VAL_TYPE_FLOAT;
              md.length = sizeof(float);
              break;
            case MYSQL_TYPE_DOUBLE:
              md.type = DBGW_VAL_TYPE_DOUBLE;
              md.length = sizeof(double);
              break;
            case MYSQL_TYPE_LONGLONG:
              md.type = DBGW_VAL_TYPE_LONG;
              md.length = sizeof(int64);
              break;
            case MYSQL_TYPE_DATE:
              md.type = DBGW_VAL_TYPE_DATE;
              md.length = sizeof(MYSQL_TIME);
              break;
            case MYSQL_TYPE_TIME:
              md.type = DBGW_VAL_TYPE_TIME;
              md.length = sizeof(MYSQL_TIME);
              break;
            case MYSQL_TYPE_DATETIME:
            case MYSQL_TYPE_TIMESTAMP:
              md.type = DBGW_VAL_TYPE_DATETIME;
              md.length = sizeof(MYSQL_TIME);
              break;
            case MYSQL_TYPE_BIT:
              md.type = DBGW_VAL_TYPE_BYTES;
              md.length = pMySQLField->length / 8;
              break;
            case MYSQL_TYPE_TINY_BLOB:
            case MYSQL_TYPE_BLOB:
            case MYSQL_TYPE_MEDIUM_BLOB:
            case MYSQL_TYPE_LONG_BLOB:
              md.type = DBGW_VAL_TYPE_BYTES;
              md.length = pMySQLField->length;
              if (md.length < 0 || md.length > MAX_BYTES_SIZE)
                {
                  md.length = MAX_BYTES_SIZE;
                }
              break;
            case MYSQL_TYPE_DECIMAL:
            case MYSQL_TYPE_VARCHAR:
            case MYSQL_TYPE_NULL:
            case MYSQL_TYPE_ENUM:
            case MYSQL_TYPE_VAR_STRING:
              md.type = DBGW_VAL_TYPE_STRING;
              md.length = pMySQLField->length + 1;
              break;
            case MYSQL_TYPE_STRING:
              md.type = DBGW_VAL_TYPE_CHAR;
              md.length = 2;
              break;
            case MYSQL_TYPE_YEAR:
            case MYSQL_TYPE_NEWDATE:
            case MYSQL_TYPE_NEWDECIMAL:
            case MYSQL_TYPE_SET:
            case MYSQL_TYPE_GEOMETRY:
            default:
              DBGW_LOGF_WARN(
                  "%d type is not yet supported. so converted string.",
                  md.mysqlType);
              md.type = DBGW_VAL_TYPE_STRING;
              md.length = pMySQLField->length + 1;
              break;
            }

          m_metaList.push_back(md);
        }

      mysql_free_result(pMySQLResult);
    }

    const _MySQLResultSetMetaDataRaw &MySQLResultSetMetaData::getMetaData(
        size_t nIndex) const
    {
      return m_metaList[nIndex];
    }

    size_t MySQLResultSetMetaData::getColumnCount() const
    {
      return m_metaList.size();
    }

    std::string MySQLResultSetMetaData::getColumnName(size_t nIndex) const
    {
      if (m_metaList.size() < nIndex + 1)
        {
          ArrayIndexOutOfBoundsException e(nIndex,
              "MySQLResultSetMetaData");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_metaList[nIndex].name;
    }

    ValueType MySQLResultSetMetaData::getColumnType(size_t nIndex) const
    {
      if (m_metaList.size() < nIndex + 1)
        {
          ArrayIndexOutOfBoundsException e(nIndex,
              "MySQLResultSetMetaData");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_metaList[nIndex].type;
    }

  }

}
