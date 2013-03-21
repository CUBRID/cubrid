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

#include "dbgw3/sql/oracle/OracleCommon.h"
#include "dbgw3/sql/oracle/OracleResultSetMetaData.h"

namespace dbgw
{

  namespace sql
  {

    ValueType getTypeFrom(ub2 nOCIType)
    {
      switch (nOCIType)
        {
        case SQLT_NUM:
        case SQLT_RDD:
        case SQLT_CHR:
        case SQLT_AFC:
        case SQLT_VST:
        case SQLT_LNG:
          return DBGW_VAL_TYPE_STRING;
        case SQLT_BIN:
          return DBGW_VAL_TYPE_BYTES;
        case SQLT_DAT:
          return DBGW_VAL_TYPE_DATE;
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
          return DBGW_VAL_TYPE_DATETIME;
        case SQLT_FLT:
        case SQLT_BFLOAT:
        case SQLT_IBFLOAT:
          return DBGW_VAL_TYPE_FLOAT;
        case SQLT_BDOUBLE:
        case SQLT_IBDOUBLE:
          return DBGW_VAL_TYPE_DOUBLE;
        case SQLT_CLOB:
          return DBGW_VAL_TYPE_CLOB;
        case SQLT_BLOB:
          return DBGW_VAL_TYPE_BLOB;
        default:
          DBGW_LOGF_WARN(
              "%d type is not yet supported. so converted string.", nOCIType);
          return DBGW_VAL_TYPE_STRING;
        }
    }

    _OracleResultSetMetaDataRaw::_OracleResultSetMetaDataRaw() :
      columnType(DBGW_VAL_TYPE_UNDEFINED), ociType(0), ociLen(0), ociPrec(0),
      ociScale(0)
    {
    }

    OracleResultSetMetaData::OracleResultSetMetaData(OCIStmt *pOCIStmt,
        OCIError *pOCIErr)
    {
      int nColumnCount = 0;
      sword nResult = OCIAttrGet((void *) pOCIStmt, OCI_HTYPE_STMT,
          (void *) &nColumnCount, (ub4 *) 0, OCI_ATTR_PARAM_COUNT, pOCIErr);
      if (nResult != OCI_SUCCESS)
        {
          Exception e = OracleExceptionFactory::create(nResult, pOCIErr,
              "Failed to fetch resultset.");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      OCIParam *pOCIParam = NULL;
      char *szColumnName;
      ub4 nLength;
      ub1 bIsCharSemantics;
      _OracleResultSetMetaDataRaw md;
      for (int i = 0; i < nColumnCount; i++)
        {
          nResult = OCIParamGet(pOCIStmt, OCI_HTYPE_STMT, pOCIErr,
              (void **) &pOCIParam, i + 1);
          if (nResult != OCI_SUCCESS)
            {
              Exception e = OracleExceptionFactory::create(nResult,
                  pOCIErr, "Failed to fetch resultset.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          nResult = OCIAttrGet(pOCIParam, OCI_DTYPE_PARAM,
              (void *) &szColumnName, &nLength, OCI_ATTR_NAME, pOCIErr);
          if (nResult != OCI_SUCCESS)
            {
              Exception e = OracleExceptionFactory::create(nResult,
                  pOCIErr, "Failed to fetch resultset.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          nResult = OCIAttrGet(pOCIParam, OCI_DTYPE_PARAM, &md.ociType, 0,
              OCI_ATTR_DATA_TYPE, pOCIErr);
          if (nResult != OCI_SUCCESS)
            {
              Exception e = OracleExceptionFactory::create(nResult,
                  pOCIErr, "Failed to fetch resultset.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          nResult = OCIAttrGet(pOCIParam, OCI_DTYPE_PARAM, &bIsCharSemantics, 0,
              OCI_ATTR_CHAR_USED, pOCIErr);
          if (nResult != OCI_SUCCESS)
            {
              Exception e = OracleExceptionFactory::create(nResult,
                  pOCIErr, "Failed to fetch resultset.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          nResult = OCIAttrGet(pOCIParam, OCI_DTYPE_PARAM, &md.ociPrec, 0,
              OCI_ATTR_PRECISION, pOCIErr);
          if (nResult != OCI_SUCCESS)
            {
              Exception e = OracleExceptionFactory::create(nResult,
                  pOCIErr, "Failed to fetch resultset.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          nResult = OCIAttrGet(pOCIParam, OCI_DTYPE_PARAM, &md.ociScale, 0,
              OCI_ATTR_SCALE, pOCIErr);
          if (nResult != OCI_SUCCESS)
            {
              Exception e = OracleExceptionFactory::create(nResult,
                  pOCIErr, "Failed to fetch resultset.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (bIsCharSemantics == 1)
            {
              nResult = OCIAttrGet(pOCIParam, OCI_DTYPE_PARAM, &md.ociLen, 0,
                  OCI_ATTR_CHAR_SIZE, pOCIErr);
              if (nResult != OCI_SUCCESS)
                {
                  Exception e = OracleExceptionFactory::create(nResult,
                      pOCIErr, "Failed to fetch resultset.");
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
            }
          else
            {
              nResult = OCIAttrGet(pOCIParam, OCI_DTYPE_PARAM, &md.ociLen, 0,
                  OCI_ATTR_DATA_SIZE, pOCIErr);
              if (nResult != OCI_SUCCESS)
                {
                  Exception e = OracleExceptionFactory::create(nResult,
                      pOCIErr, "Failed to fetch resultset.");
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
            }

          md.columnName = szColumnName;
          md.columnType = getTypeFrom(md.ociType);

          switch (md.ociType)
            {
            case SQLT_NUM:
              if (md.ociScale != -127 || md.ociPrec == 0)
                {
                  md.ociType = SQLT_VNU;
                  md.columnType = DBGW_VAL_TYPE_STRING;
                  break;
                }
              /* no break */
            case SQLT_FLT:
            case SQLT_BFLOAT:
            case SQLT_IBFLOAT:
            case SQLT_BDOUBLE:
            case SQLT_IBDOUBLE:
              md.columnType = DBGW_VAL_TYPE_DOUBLE;
              md.ociType = SQLT_FLT;
              break;
            case SQLT_DAT:
            case SQLT_TIMESTAMP:
            case SQLT_TIMESTAMP_TZ:
            case SQLT_TIMESTAMP_LTZ:
              md.ociType = SQLT_TIMESTAMP;
              break;
            default:
              break;
            }

          m_metaDataRawList.push_back(md);
        }
    }

    size_t OracleResultSetMetaData::getColumnCount() const
    {
      return m_metaDataRawList.size();
    }

    std::string OracleResultSetMetaData::getColumnName(size_t nIndex) const
    {
      if (m_metaDataRawList.size() < nIndex + 1)
        {
          ArrayIndexOutOfBoundsException e(nIndex,
              "OracleResultSetMetaData");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_metaDataRawList[nIndex].columnName;
    }

    ValueType OracleResultSetMetaData::getColumnType(size_t nIndex) const
    {
      if (m_metaDataRawList.size() < nIndex + 1)
        {
          ArrayIndexOutOfBoundsException e(nIndex,
              "OracleResultSetMetaData");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_metaDataRawList[nIndex].columnType;
    }

    const _OracleResultSetMetaDataRaw &
    OracleResultSetMetaData::getColumnInfo(size_t nIndex) const
    {
      /**
       * this method is never called external because it is non-virtual method.
       * so we don't care about exception.
       */

      if (m_metaDataRawList.size() < nIndex + 1)
        {
          ArrayIndexOutOfBoundsException e(nIndex,
              "OracleResultSetMetaData");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return m_metaDataRawList[nIndex];
    }

  }

}
