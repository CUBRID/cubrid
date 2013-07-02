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
#include "dbgw3/sql/oracle/OracleValue.h"
#include "dbgw3/sql/oracle/OracleResultSetMetaData.h"

namespace dbgw
{

  namespace sql
  {

    ub2 getOCITypeFrom(ValueType type)
    {
      switch (type)
        {
        case DBGW_VAL_TYPE_INT:
          return SQLT_INT;
        case DBGW_VAL_TYPE_TIME:
        case DBGW_VAL_TYPE_DATE:
        case DBGW_VAL_TYPE_DATETIME:
          return SQLT_DAT;
        case DBGW_VAL_TYPE_CHAR:
        case DBGW_VAL_TYPE_STRING:
        case DBGW_VAL_TYPE_LONG:
          return SQLT_STR;
        case DBGW_VAL_TYPE_FLOAT:
          return SQLT_BFLOAT;
        case DBGW_VAL_TYPE_DOUBLE:
          return SQLT_BDOUBLE;
        case DBGW_VAL_TYPE_BYTES:
          return SQLT_BIN;
        case DBGW_VAL_TYPE_CLOB:
          return SQLT_CLOB;
        case DBGW_VAL_TYPE_BLOB:
          return SQLT_BLOB;
        default:
          return SQLT_STR;
        }
    }

    _OracleValue::_OracleValue(_OracleContext *pContext, int nType,
        int nLength, int nIsNull) :
      m_pContext(pContext), m_pBuffer(NULL), m_pConvertedBuffer(NULL),
      m_nLength(nLength), m_nType(nType), m_nIsNull(nIsNull), m_nReadLen(0)
    {
      memset(&m_linkedValue, 0, sizeof(_ExternelSource));
    }

    _OracleValue::~_OracleValue()
    {
      finalize();
    }

    const _ExternelSource &_OracleValue::getExternalSource(
        ValueType type)
    {
      int nResult;

      if (type == DBGW_VAL_TYPE_LONG)
        {
          /**
           * in oci, int64 is not exist.
           * so we will treat it as string.
           */
          type = DBGW_VAL_TYPE_STRING;
        }

      m_linkedValue.type = type;
      m_linkedValue.isNull = m_nIsNull < 0 ? true : false;
      m_linkedValue.length = m_nLength;

      if (m_linkedValue.isNull)
        {
          return m_linkedValue;
        }

      switch (m_nType)
        {
        case SQLT_VNU:
        {
          text szFormat[] = "FM99999999999999999999D99999999999999999999";
          ub4 nBufferLen = oci::MAX_LEN_NUMBER;

          nResult = OCINumberToText(m_pContext->pOCIErr,
              (const OCINumber *) m_pBuffer, szFormat, 43, 0, 0, &nBufferLen,
              (text *) m_pConvertedBuffer);
          if (nResult != OCI_SUCCESS)
            {
              Exception e = OracleExceptionFactory::create(nResult,
                  m_pContext->pOCIErr, "Failed to fetch resultset.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          m_linkedValue.length = nBufferLen;
          m_pConvertedBuffer[nBufferLen - 1] = '\0';
          m_linkedValue.value.p = m_pConvertedBuffer;
          break;
        }
        case SQLT_DAT:
          if (type == DBGW_VAL_TYPE_DATE)
            {
              sb2 yy;
              ub1 mm, dd;
              oci::getOCIDate(m_pBuffer, &yy, &mm, &dd);
              sprintf(m_pConvertedBuffer, "%4d-%02d-%02d", yy, mm, dd);

              m_linkedValue.length = 11;
              m_linkedValue.value.p = m_pConvertedBuffer;
            }
          else if (type == DBGW_VAL_TYPE_TIME)
            {
              ub1 hh, mi, ss;
              oci::getOCITime(m_pBuffer, &hh, &mi, &ss);
              sprintf(m_pConvertedBuffer, "%02d:%02d:%02d", hh, mi, ss);

              m_linkedValue.length = 9;
              m_linkedValue.value.p = m_pConvertedBuffer;
            }
          else if (type == DBGW_VAL_TYPE_DATETIME)
            {
              sb2 yy;
              ub1 mm, dd, hh, mi, ss;
              oci::getOCIDate(m_pBuffer, &yy, &mm, &dd);
              oci::getOCITime(m_pBuffer, &hh, &mi, &ss);
              sprintf(m_pConvertedBuffer, "%4d-%02d-%02d %02d:%02d:%02d", yy, mm,
                  dd, hh, mi, ss);

              m_linkedValue.length = 21;
              m_linkedValue.value.p = m_pConvertedBuffer;
            }
          break;
        case SQLT_TIMESTAMP:
        {
          sb2 yy;
          ub1 mm, dd, hh, mi, ss;
          ub4 fs;

          if (type == DBGW_VAL_TYPE_DATE || type == DBGW_VAL_TYPE_DATETIME)
            {
              nResult = OCIDateTimeGetDate(m_pContext->pOCIEnv,
                  m_pContext->pOCIErr, (OCIDateTime *) m_pBuffer, &yy, &mm,
                  &dd);
              if (nResult != OCI_SUCCESS)
                {
                  Exception e = OracleExceptionFactory::create(nResult,
                      m_pContext->pOCIErr, "Failed to fetch resultset.");
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
            }

          if (type == DBGW_VAL_TYPE_TIME || type == DBGW_VAL_TYPE_DATETIME)
            {
              nResult = OCIDateTimeGetTime(m_pContext->pOCIEnv,
                  m_pContext->pOCIErr, (OCIDateTime *) m_pBuffer, &hh, &mi, &ss,
                  &fs);
              if (nResult != OCI_SUCCESS)
                {
                  Exception e = OracleExceptionFactory::create(nResult,
                      m_pContext->pOCIErr, "Failed to fetch resultset.");
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
            }

          if (type == DBGW_VAL_TYPE_DATE)
            {
              sprintf(m_pConvertedBuffer, "%04d-%02d-%02d", yy, mm, dd);
            }
          else if (type == DBGW_VAL_TYPE_TIME)
            {
              sprintf(m_pConvertedBuffer, "%02d:%02d:%02d", hh, mi, ss);
            }
          else if (type == DBGW_VAL_TYPE_DATETIME)
            {
              sprintf(m_pConvertedBuffer, "%04d-%02d-%02d %02d:%02d:%02d", yy,
                  mm, dd, hh, mi, ss);
            }

          m_linkedValue.length = 20;
          m_linkedValue.value.p = m_pConvertedBuffer;
          break;
        }
        case SQLT_RDD:
          m_linkedValue.length = oci::MAX_LEN_ROWID;

          nResult = OCIRowidToChar((OCIRowid *) m_pBuffer,
              (OraText *) m_pConvertedBuffer, (ub2 *) &m_linkedValue.length,
              m_pContext->pOCIErr);
          if (nResult != OCI_SUCCESS)
            {
              Exception e = OracleExceptionFactory::create(nResult,
                  m_pContext->pOCIErr, "Failed to fetch resultset.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          m_pConvertedBuffer[m_linkedValue.length] = '\0';
          m_linkedValue.value.p = (char *) m_pConvertedBuffer;
          break;
        case SQLT_STR:
          m_linkedValue.value.p = m_pBuffer;
          break;
        case SQLT_AFC:
        case SQLT_CHR:
          m_pBuffer[m_nReadLen] = '\0';
          m_linkedValue.value.p = m_pBuffer;
          break;
        case SQLT_LNG:
          m_linkedValue.value.p = m_pBuffer;
          break;
        case SQLT_BFLOAT:
        case SQLT_BDOUBLE:
        case SQLT_FLT:
          m_linkedValue.value.d = *(double *) m_pBuffer;
          break;
        case SQLT_INT:
          m_linkedValue.value.n = *(int *) m_pBuffer;
          break;
        case SQLT_CLOB:
          m_linkedValue.lob = trait<Lob>::sp(
              new OracleLob(m_pContext, DBGW_VAL_TYPE_CLOB,
                  (OCILobLocator *) m_pBuffer));
          break;
        case SQLT_BLOB:
          m_linkedValue.lob = trait<Lob>::sp(
              new OracleLob(m_pContext, DBGW_VAL_TYPE_BLOB,
                  (OCILobLocator *) m_pBuffer));
          break;
        case SQLT_BIN:
          m_linkedValue.length = m_nReadLen;
          /* no break */
        default:
          m_linkedValue.value.p = m_pBuffer;
          break;
        }

      return m_linkedValue;
    }

    bool _OracleValue::isNull() const
    {
      return m_nIsNull == -1;
    }

    void _OracleValue::finalize()
    {
      switch (m_nType)
        {
        case SQLT_RDD:
          if (m_pBuffer != NULL)
            {
              m_pBuffer = NULL;
            }
          break;
        case SQLT_TIMESTAMP:
          if (m_pBuffer != NULL)
            {
              m_pBuffer = NULL;
            }
          break;
        case SQLT_VST:
          if (m_pBuffer != NULL)
            {
              OCIStringResize(m_pContext->pOCIEnv, m_pContext->pOCIErr, 0,
                  (OCIString **) &m_pBuffer);
              m_pBuffer = NULL;
            }
          break;
        case SQLT_CLOB:
        case SQLT_BLOB:
          m_pBuffer = NULL;
          break;
        default:
          break;
        }

      if (m_pBuffer != NULL)
        {
          delete[] m_pBuffer;
          m_pBuffer = NULL;
        }

      if (m_pConvertedBuffer != NULL)
        {
          delete[] m_pConvertedBuffer;
          m_pConvertedBuffer = NULL;
        }
    }

    _OracleBind::_OracleBind(_OracleContext *pContext,
        OCIStmt *pOCIStmt, size_t nIndex, const Value *pValue,
        int nSize) :
      _OracleValue(pContext, getOCITypeFrom(pValue->getType()), 0,
          pValue->isNull() ? -1 : 0),
      m_pOCIBind(NULL), m_dbgwType(pValue->getType()), m_nReservedSize(nSize)
    {
      try
        {
          /**
           * out bind parameter will be bind null.
           * but oci will fill value to bind buffer.
           * so we have to make buffer despite of null value.
           */
          switch (m_dbgwType)
            {
            case DBGW_VAL_TYPE_INT:
              bindInt(pValue);
              break;
            case DBGW_VAL_TYPE_STRING:
              bindString(pValue);
              break;
            case DBGW_VAL_TYPE_CHAR:
              bindChar(pValue);
              break;
            case DBGW_VAL_TYPE_LONG:
              bindLong(pValue);
              break;
            case DBGW_VAL_TYPE_FLOAT:
              bindFloat(pValue);
              break;
            case DBGW_VAL_TYPE_DOUBLE:
              bindDouble(pValue);
              break;
            case DBGW_VAL_TYPE_DATETIME:
              bindDateTime(pValue);
              break;
            case DBGW_VAL_TYPE_DATE:
              bindDate(pValue);
              break;
            case DBGW_VAL_TYPE_TIME:
              bindTime(pValue);
              break;
            case DBGW_VAL_TYPE_BYTES:
              bindBytes(pValue);
              break;
            case DBGW_VAL_TYPE_CLOB:
              bindClob(pValue);
              break;
            case DBGW_VAL_TYPE_BLOB:
              bindBlob(pValue);
              break;
            default:
              break;
            }

          void **p = NULL;
          if (m_dbgwType == DBGW_VAL_TYPE_CLOB
              || m_dbgwType == DBGW_VAL_TYPE_BLOB)
            {
              p = (void **) &m_pBuffer;
            }
          else
            {
              p = (void **) m_pBuffer;
            }

          sword nResult = OCIBindByPos(pOCIStmt, &m_pOCIBind, pContext->pOCIErr,
              nIndex + 1, p, m_nLength, m_nType, &m_nIsNull, NULL, 0, 0, NULL,
              OCI_DEFAULT);
          if (nResult != OCI_SUCCESS)
            {
              Exception e = OracleExceptionFactory::create(nResult,
                  pContext->pOCIErr, "Failed to bind parameter.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
        }
      catch (...)
        {
          finalize();
          throw;
        }
    }

    ValueType _OracleBind::getType() const
    {
      return m_dbgwType;
    }

    void _OracleBind::bindInt(const Value *pValue)
    {
      m_nLength = pValue->getLength();
      m_pBuffer = new char[m_nLength];

      if (isNull() == false)
        {
          memcpy(m_pBuffer, pValue->getVoidPtr(), m_nLength);
        }
    }

    void _OracleBind::bindString(const Value *pValue)
    {
      m_nLength = m_nReservedSize;
      if (m_nLength == 0)
        {
          m_nLength = oci::MAX_LEN_OUT_BIND_STRING;
        }

      m_pBuffer = new char[m_nLength];
      if (isNull() == false)
        {
          const char *szValue = NULL;
          if (pValue->getCString(&szValue) == false)
            {
              throw getLastException();
            }

          memcpy(m_pBuffer, szValue, pValue->getLength() + 1);
        }
    }

    void _OracleBind::bindChar(const Value *pValue)
    {
      m_nLength = 2;
      m_pBuffer = new char[m_nLength];
      if (isNull() == false)
        {
          const char *szValue = NULL;
          if (pValue->getCString(&szValue) == false)
            {
              throw getLastException();
            }

          if (szValue == NULL)
            {
              /**
               *  assert (szValue == NULL)
               *  we can't use assert.
               */
              return;
            }

          memcpy(m_pBuffer, szValue, 2);
        }
    }

    void _OracleBind::bindLong(const Value *pValue)
    {
      /**
       * in oci, int64 is not exist.
       * so we will treat it as string.
       */
      if (isNull())
        {
          m_nLength = oci::MAX_LEN_INT64;
          m_pBuffer = new char[m_nLength];
        }
      else
        {
          std::string value = pValue->toString();
          m_nLength = value.length() + 1;
          m_pBuffer = new char[m_nLength];
          memcpy(m_pBuffer, value.c_str(), m_nLength - 1);
          m_pBuffer[m_nLength - 1] = '\0';
        }
    }

    void _OracleBind::bindFloat(const Value *pValue)
    {
      bindInt(pValue);
    }

    void _OracleBind::bindDouble(const Value *pValue)
    {
      bindInt(pValue);
    }

    void _OracleBind::bindDateTime(const Value *pValue)
    {
      m_nLength = oci::SIZE_OCIDATE;
      m_pBuffer = new char[oci::SIZE_OCIDATE];
      /* yy-mm-dd hh:mi:ss*/
      m_pConvertedBuffer = new char[21];

      if (isNull() == false)
        {
          const char *szValue = NULL;
          if (pValue->getCString(&szValue) == false)
            {
              throw getLastException();
            }

          if (szValue == NULL)
            {
              /**
               *  assert (szValue == NULL)
               *  we can't use assert.
               */
              return;
            }

          int yy, mm, dd, hh, mi, ss;
          /* yy-mm-dd hh:mi:ss*/
          sscanf(szValue, "%d-%d-%d %d:%d:%d", &yy, &mm, &dd, &hh, &mi, &ss);
          oci::setOCIDateTime(m_pBuffer, yy, mm, dd, hh, mi, ss);
        }
    }

    void _OracleBind::bindDate(const Value *pValue)
    {
      m_nLength = oci::SIZE_OCIDATE;
      m_pBuffer = new char[oci::SIZE_OCIDATE];
      /* yy-mm-dd */
      m_pConvertedBuffer = new char[11];

      if (isNull() == false)
        {
          const char *szValue = NULL;
          if (pValue->getCString(&szValue) == false)
            {
              throw getLastException();
            }

          if (szValue == NULL)
            {
              /**
               *  assert (szValue == NULL)
               *  we can't use assert.
               */
              return;
            }

          int yy, mm, dd;
          sscanf(szValue, "%d-%d-%d", &yy, &mm, &dd);
          oci::setOCIDateTime(m_pBuffer, yy, mm, dd, 0, 0, 0);
        }
    }

    void _OracleBind::bindTime(const Value *pValue)
    {
      m_nLength = oci::SIZE_OCIDATE;
      m_pBuffer = new char[oci::SIZE_OCIDATE];
      /* hh:mi:ss*/
      m_pConvertedBuffer = new char[9];

      if (isNull() == false)
        {
          const char *szValue = NULL;
          if (pValue->getCString(&szValue) == false)
            {
              throw getLastException();
            }

          if (szValue == NULL)
            {
              /**
               *  assert (szValue == NULL)
               *  we can't use assert.
               */
              return;
            }

          int hh, mi, ss;
          sscanf(szValue, "%d:%d:%d", &hh, &mi, &ss);
          oci::setOCIDateTime(m_pBuffer, 0, 0, 0, hh, mi, ss);
        }
    }

    void _OracleBind::bindBytes(const Value *pValue)
    {
      m_nLength = m_nReservedSize;
      if (m_nLength == 0)
        {
          m_nLength = oci::MAX_LEN_OUT_BIND_BYTES;
        }

      m_pBuffer = new char[m_nLength];
      if (isNull() == false)
        {
          const void *pData = pValue->getVoidPtr();
          if (pData == NULL)
            {
              /**
               *  assert (pData == NULL)
               *  we can't use assert.
               */
              return;
            }

          memcpy(m_pBuffer, pData, pValue->getLength());
        }
    }

    void _OracleBind::bindClob(const Value *pValue)
    {
      trait<Lob>::sp pLob = pValue->getClob();
      if (pLob == NULL)
        {
          throw getLastException();
        }

      m_pBuffer = (char *) pLob->getNativeHandle();
    }

    void _OracleBind::bindBlob(const Value *pValue)
    {
      trait<Lob>::sp pLob = pValue->getBlob();
      if (pLob == NULL)
        {
          throw getLastException();
        }

      m_pBuffer = (char *) pLob->getNativeHandle();
    }

    _OracleDefine::_OracleDefine(_OracleContext *pContext,
        OCIStmt *pOCIStmt, size_t nIndex,
        const _OracleResultSetMetaDataRaw &md) :
      _OracleValue(pContext, md.ociType, md.ociLen, 0),
      m_pContext(pContext), m_pOCIDefine(NULL)
    {
      try
        {
          sword nResult = OCI_SUCCESS;
          void **p = NULL;

          switch (m_nType)
            {
            case SQLT_RDD:
              m_nLength = sizeof(OCIRowid *);
              m_pConvertedBuffer = new char[oci::MAX_LEN_ROWID];
              m_pBuffer = (char *) m_ociDesc.alloc(m_pContext->pOCIEnv,
                  OCI_DTYPE_ROWID);
              p = (void **) &m_pBuffer;
              break;
            case SQLT_VNU:
              m_nLength = sizeof(OCINumber);
              m_pBuffer = new char[m_nLength];
              m_pConvertedBuffer = new char[oci::MAX_LEN_NUMBER];
              p = (void **) m_pBuffer;
              break;
            case SQLT_FLT:
            case SQLT_LNG:
              if (m_nType == SQLT_FLT)
                {
                  m_nLength = sizeof(double);
                }
              else if (m_nType == SQLT_LNG)
                {
                  m_nLength = oci::MAX_LEN_LONG + 1;
                }
              /* no break */
            case SQLT_BIN:
              m_pBuffer = new char[m_nLength];
              p = (void **) m_pBuffer;
              m_nReadLen = m_nLength;
              break;
            case SQLT_CHR:
            case SQLT_AFC:
              m_nLength = md.ociLen + 1;
              m_pBuffer = new char[m_nLength];
              m_nReadLen = m_nLength;
              p = (void **) m_pBuffer;
              break;
            case SQLT_VST:
              nResult = OCIStringResize(m_pContext->pOCIEnv, m_pContext->pOCIErr,
                  m_nLength + 1, (OCIString **) &m_pBuffer);
              if (nResult != OCI_SUCCESS)
                {
                  Exception e = OracleExceptionFactory::create(nResult,
                      m_pContext->pOCIErr, "Failed to fetch resultset.");
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }

              m_nLength = sizeof(OCIString *);
              p = (void **) &m_pBuffer;
              break;
            case SQLT_TIMESTAMP:
              m_nLength = sizeof(OCIDateTime *);
              m_pConvertedBuffer = new char[20];
              m_pBuffer = (char *) m_ociDesc.alloc(m_pContext->pOCIEnv,
                  OCI_DTYPE_TIMESTAMP);
              p = (void **) &m_pBuffer;
              break;
            case SQLT_CLOB:
            case SQLT_BLOB:
              m_nLength = -1;
              m_pBuffer = (char *) m_ociDesc.alloc(m_pContext->pOCIEnv,
                  OCI_DTYPE_LOB);
              p = (void **) &m_pBuffer;
              break;
            default:
              break;
            }

          nResult = OCIDefineByPos(pOCIStmt, &m_pOCIDefine, m_pContext->pOCIErr,
              nIndex + 1, p, m_nLength, m_nType, &m_nIsNull,
              m_nReadLen > 0 ? (ub2 *) &m_nReadLen : NULL, 0, OCI_DEFAULT);
          if (nResult != OCI_SUCCESS)
            {
              Exception e = OracleExceptionFactory::create(nResult,
                  m_pContext->pOCIErr, "Failed to fetch resultset.");
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
        }
      catch (...)
        {
          finalize();
          throw;
        }
    }

  }

}
