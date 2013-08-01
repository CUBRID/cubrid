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

#include "dbgw3/sql/cubrid/CUBRIDCommon.h"
#include "dbgw3/sql/cubrid/CUBRIDResultSetMetaData.h"

namespace dbgw
{

  namespace sql
  {

    _CUBRIDResultSetMetaDataRaw::_CUBRIDResultSetMetaDataRaw() :
      unused(true), columnType(DBGW_VAL_TYPE_UNDEFINED)
    {
    }

    CUBRIDResultSetMetaData::CUBRIDResultSetMetaData(int hCCIRequest) :
      ResultSetMetaData()
    {
      T_CCI_SQLX_CMD cciCmdType;
      int nColNum;
      T_CCI_COL_INFO *pCCIColInfo = cci_get_result_info(hCCIRequest,
          &cciCmdType, &nColNum);
      if (pCCIColInfo == NULL)
        {
          CUBRIDException e = CUBRIDExceptionFactory::create(
              "Cannot get the cci col info.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      T_CCI_U_TYPE utype;
      _CUBRIDResultSetMetaDataRaw metaDataRaw;
      for (size_t i = 0; i < (size_t) nColNum; i++)
        {
          metaDataRaw.unused = false;
          metaDataRaw.columnName = CCI_GET_RESULT_INFO_NAME(pCCIColInfo, i + 1);
          utype = CCI_GET_RESULT_INFO_TYPE(pCCIColInfo, i + 1);
          switch (utype)
            {
            case CCI_U_TYPE_CHAR:
              metaDataRaw.columnType = DBGW_VAL_TYPE_CHAR;
              break;
            case CCI_U_TYPE_INT:
            case CCI_U_TYPE_SHORT:
              metaDataRaw.columnType = DBGW_VAL_TYPE_INT;
              break;
            case CCI_U_TYPE_BIGINT:
              metaDataRaw.columnType = DBGW_VAL_TYPE_LONG;
              break;
            case CCI_U_TYPE_NUMERIC:
            case CCI_U_TYPE_STRING:
            case CCI_U_TYPE_NCHAR:
            case CCI_U_TYPE_VARNCHAR:
              metaDataRaw.columnType = DBGW_VAL_TYPE_STRING;
              break;
            case CCI_U_TYPE_BIT:
            case CCI_U_TYPE_VARBIT:
              metaDataRaw.columnType = DBGW_VAL_TYPE_BYTES;
              break;
            case CCI_U_TYPE_FLOAT:
              metaDataRaw.columnType = DBGW_VAL_TYPE_FLOAT;
              break;
            case CCI_U_TYPE_DOUBLE:
              metaDataRaw.columnType = DBGW_VAL_TYPE_DOUBLE;
              break;
            case CCI_U_TYPE_DATE:
              metaDataRaw.columnType = DBGW_VAL_TYPE_DATE;
              break;
            case CCI_U_TYPE_TIME:
              metaDataRaw.columnType = DBGW_VAL_TYPE_TIME;
              break;
            case CCI_U_TYPE_DATETIME:
            case CCI_U_TYPE_TIMESTAMP:
              metaDataRaw.columnType = DBGW_VAL_TYPE_DATETIME;
              break;
            default:
              DBGW_LOGF_WARN(
                  "%d type is not yet supported. so converted string.",
                  utype);
              metaDataRaw.columnType = DBGW_VAL_TYPE_STRING;
              break;
            }

          m_metaDataRawList.push_back(metaDataRaw);
        }
    }


    CUBRIDResultSetMetaData::CUBRIDResultSetMetaData(
        const trait<_CUBRIDResultSetMetaDataRaw>::vector &metaDataRawList) :
      m_metaDataRawList(metaDataRawList)
    {
    }

    CUBRIDResultSetMetaData::~CUBRIDResultSetMetaData()
    {
    }

    size_t CUBRIDResultSetMetaData::getColumnCount() const
    {
      return m_metaDataRawList.size();
    }

    std::string CUBRIDResultSetMetaData::getColumnName(size_t nIndex) const
    {
      if (m_metaDataRawList.size() < nIndex + 1)
        {
          ArrayIndexOutOfBoundsException e(nIndex,
              "DBGWCUBRIDResultSetMetaData");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_metaDataRawList[nIndex].columnName;
    }

    ValueType CUBRIDResultSetMetaData::getColumnType(size_t nIndex) const
    {
      if (m_metaDataRawList.size() < nIndex + 1)
        {
          ArrayIndexOutOfBoundsException e(nIndex,
              "DBGWCUBRIDResultSetMetaData");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_metaDataRawList[nIndex].columnType;
    }

    const _CUBRIDResultSetMetaDataRaw &CUBRIDResultSetMetaData::getColumnInfo(
        size_t nIndex) const
    {
      /**
       * this method is never called external because it is non-virtual method.
       * so we don't care about exception.
       */

      if (m_metaDataRawList.size() < nIndex + 1)
        {
          ArrayIndexOutOfBoundsException e(nIndex,
              "DBGWCUBRIDResultSetMetaDataRaw");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_metaDataRawList[nIndex];
    }

  }

}
