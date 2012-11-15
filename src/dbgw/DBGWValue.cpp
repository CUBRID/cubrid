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
 *  aint64 with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/lexical_cast.hpp>
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWPorting.h"
#include "DBGWLogger.h"
#include "DBGWValue.h"

namespace dbgw
{

  const char *getDBGWValueTypeString(int type)
  {
    switch (type)
      {
      case DBGW_VAL_TYPE_INT:
        return "INT";
      case DBGW_VAL_TYPE_STRING:
        return "STRING";
      case DBGW_VAL_TYPE_LONG:
        return "LONG";
      case DBGW_VAL_TYPE_CHAR:
        return "CHAR";
      case DBGW_VAL_TYPE_FLOAT:
        return "FLOAT";
      case DBGW_VAL_TYPE_DOUBLE:
        return "DOUBLE";
      case DBGW_VAL_TYPE_DATETIME:
        return "DATETIME";
      case DBGW_VAL_TYPE_DATE:
        return "DATE";
      case DBGW_VAL_TYPE_TIME:
        return "TIME";
      case DBGW_VAL_TYPE_BYTES:
        return "BYTES";
#ifdef ENABLE_LOB
      case DBGW_VAL_TYPE_CLOB:
        return "CLOB";
      case DBGW_VAL_TYPE_BLOB:
        return "BLOB";
#endif
      default:
        return "UNDEFINED";
      }
  }

  const int DBGWValue::MAX_BOUNDARY_SIZE = 1024 * 1024 * 1;   // 1 MByte

#ifdef ENABLE_LOB
  DBGWValue::DBGWValue(DBGWValueType type, bool bNull, int nSize) :
    m_type(type), m_bNull(bNull), m_nSize(-1)
  {
    clearException();
    try
      {
        memset(&m_stValue, 0, sizeof(_DBGWRawValue));
        alloc(type, NULL, bNull, nSize, true);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
      }
  }
#endif

  DBGWValue::DBGWValue(DBGWValueType type, struct tm tmValue) :
    m_type(type), m_bNull(false), m_nSize(-1)
  {
    clearException();
    try
      {
        memset(&m_stValue, 0, sizeof(_DBGWRawValue));
        switch (type)
          {
          case DBGW_VAL_TYPE_DATE:
          case DBGW_VAL_TYPE_TIME:
          case DBGW_VAL_TYPE_DATETIME:
            alloc(type, &tmValue);
            break;
          default:
            InvalidValueTypeException e(type, "TIME|DATE|DATETIME");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
      }
  }

  DBGWValue::DBGWValue(DBGWValueType type, const void *pValue, bool bNull,
      int nSize) :
    m_type(type), m_bNull(bNull), m_nSize(-1)
  {
    clearException();
    try
      {
        memset(&m_stValue, 0, sizeof(_DBGWRawValue));
        alloc(type, pValue, bNull, nSize);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
      }
  }

  DBGWValue::DBGWValue(const DBGWValue &value) :
    m_type(value.m_type), m_bNull(value.m_bNull), m_nSize(value.m_nSize)
  {
    clearException();

    try
      {
        memset(&m_stValue, 0, sizeof(_DBGWRawValue));
        if (isStringBasedValue())
          {
            m_stValue.szValue = (char *) malloc(m_nSize);
            if (m_stValue.szValue == NULL)
              {
                m_bNull = true;
                m_nSize = -1;
                MemoryAllocationFail e(m_nSize);
                DBGW_LOG_ERROR(e.what());
                throw e;
              }

            memcpy(m_stValue.szValue, value.m_stValue.szValue, m_nSize);
          }
        else
          {
            m_stValue = value.m_stValue;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
      }
  }

#if defined (ENABLE_UNUSED_FUNCTION)
  /**
   * This is used only MySQL Interface
   */
  DBGWValue::DBGWValue(DBGWValueType type, size_t length, bool bNull) :
    m_type(type), m_bNull(bNull)
  {
    /**
     * We don't need to clear error because this api will not make error.
     *
     * clearException();
     *
     * try
     * {
     * 		blur blur blur;
     * }
     * catch (DBGWException &e)
     * {
     * 		setLastException(e);
     * }
     */
    if (m_type == DBGW_VAL_TYPE_STRING && length > 0)
      {
        m_stValue.szValue = (char *) malloc(sizeof(char) * length);
      }
  }
#endif

  DBGWValue::~DBGWValue()
  {
    /**
     * We don't need to clear error because this api will not make error.
     *
     * clearException();
     *
     * try
     * {
     * 		blur blur blur;
     * }
     * catch (DBGWException &e)
     * {
     * 		setLastException(e);
     * }
     */
    if (isStringBasedValue() && m_stValue.szValue != NULL)
      {
        free(m_stValue.szValue);
      }
  }

#ifdef ENABLE_LOB
  bool DBGWValue::set(const DBGWValueType type, bool bNull, int nSize)
  {
    clearException();

    try
      {
        init(type, NULL, bNull, true);
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }
#endif

  bool DBGWValue::set(DBGWValueType type, void *pValue, bool bNull, int nSize)
  {
    clearException();

    try
      {
        alloc(type, pValue, bNull, nSize);
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::getInt(int *pValue) const
  {
    clearException();

    try
      {
        if (m_type != DBGW_VAL_TYPE_INT)
          {
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_INT);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        *pValue = m_stValue.nValue;
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::getCString(char **pValue) const
  {
    clearException();

    try
      {
        if (isStringBasedValue() == false)
          {
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_STRING);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        if (isNull())
          {
            *pValue = NULL;
          }
        else
          {
            *pValue = m_stValue.szValue;
          }

        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::getLong(int64 *pValue) const
  {
    clearException();

    try
      {
        if (m_type != DBGW_VAL_TYPE_LONG)
          {
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_LONG);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        *pValue = m_stValue.lValue;
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::getChar(char *pValue) const
  {
    clearException();

    try
      {
        if (m_type != DBGW_VAL_TYPE_CHAR)
          {
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_CHAR);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        if (isNull())
          {
            *pValue = 0;
          }
        else
          {
            *pValue = *m_stValue.szValue;
          }
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::getDateTime(struct tm *pValue) const
  {
    clearException();

    try
      {
        memset(pValue, 0, sizeof(struct tm));

        if (m_type != DBGW_VAL_TYPE_DATETIME && m_type != DBGW_VAL_TYPE_DATE &&
            m_type != DBGW_VAL_TYPE_TIME)
          {
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_DATETIME);
            DBGW_LOG_WARN(e.what());
            throw e;
          }

        if (isNull())
          {
            return true;
          }

        *pValue = toTm();
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::getFloat(float *pValue) const
  {
    clearException();

    try
      {
        if (m_type != DBGW_VAL_TYPE_FLOAT)
          {
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_FLOAT);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        *pValue = m_stValue.fValue;
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::getDouble(double *pValue) const
  {
    clearException();

    try
      {
        if (m_type != DBGW_VAL_TYPE_DOUBLE)
          {
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_DOUBLE);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        *pValue = m_stValue.dValue;
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::getBytes(size_t *pSize, char **pValue) const
  {
    clearException();

    try
      {
        if (m_type != DBGW_VAL_TYPE_BYTES)
          {
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_BYTES);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        *pSize = m_nSize;
        *pValue = m_stValue.szValue;
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  DBGWValueType DBGWValue::getType() const
  {
    /**
     * We don't need to clear error because this api will not make error.
     *
     * clearException();
     *
     * try
     * {
     * 		blur blur blur;
     * }
     * catch (DBGWException &e)
     * {
     * 		setLastException(e);
     * }
     */
    return m_type;
  }

  void *DBGWValue::getVoidPtr() const
  {
    /**
     * We don't need to clear error because this api will not make error.
     *
     * clearException();
     *
     * try
     * {
     * 		blur blur blur;
     * }
     * catch (DBGWException &e)
     * {
     * 		setLastException(e);
     * }
     */
    if (isStringBasedValue())
      {
        return (void *) m_stValue.szValue;
      }
    else
      {
        return (void *) &m_stValue;
      }
  }

  int DBGWValue::getLength() const
  {
    clearException();

    try
      {
        switch (m_type)
          {
          case DBGW_VAL_TYPE_BYTES:
            return m_nSize;
          case DBGW_VAL_TYPE_STRING:
          case DBGW_VAL_TYPE_DATE:
          case DBGW_VAL_TYPE_TIME:
          case DBGW_VAL_TYPE_DATETIME:
            if (m_stValue.szValue == NULL)
              {
                return 0;
              }
            return strlen(m_stValue.szValue);
#ifdef ENABLE_LOB
          case DBGW_VAL_TYPE_CLOB:
          case DBGW_VAL_TYPE_BLOB:
            return m_nSize;
#endif
          case DBGW_VAL_TYPE_CHAR:
            return sizeof(char);
          case DBGW_VAL_TYPE_INT:
            return sizeof(int);
          case DBGW_VAL_TYPE_LONG:
            return sizeof(int64);
          case DBGW_VAL_TYPE_FLOAT:
            return sizeof(float);
          case DBGW_VAL_TYPE_DOUBLE:
            return sizeof(double);
          default:
            InvalidValueTypeException e(m_type);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return -1;
      }
  }

  bool DBGWValue::toInt(int *pValue) const
  {
    clearException();

    try
      {
        switch (m_type)
          {
          case DBGW_VAL_TYPE_STRING:
          case DBGW_VAL_TYPE_CHAR:
            *pValue = boost::lexical_cast<int>(m_stValue.szValue);
            return true;
          case DBGW_VAL_TYPE_INT:
            return getInt(pValue);
          case DBGW_VAL_TYPE_LONG:
            *pValue = (int) m_stValue.lValue;
            return true;
          case DBGW_VAL_TYPE_FLOAT:
            *pValue = (int) m_stValue.fValue;
            return true;
          case DBGW_VAL_TYPE_DOUBLE:
            *pValue = (int) m_stValue.dValue;
            return true;
          case DBGW_VAL_TYPE_TIME:
          case DBGW_VAL_TYPE_DATE:
          case DBGW_VAL_TYPE_DATETIME:
          case DBGW_VAL_TYPE_BYTES:
          default:
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_INT);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
    catch (boost::bad_lexical_cast &)
      {
        MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_INT);
        DBGW_LOG_ERROR(e.what());
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::toLong(int64 *pValue) const
  {
    clearException();

    try
      {
        switch (m_type)
          {
          case DBGW_VAL_TYPE_STRING:
          case DBGW_VAL_TYPE_CHAR:
            *pValue = boost::lexical_cast<int64>(m_stValue.szValue);
            return true;
          case DBGW_VAL_TYPE_INT:
            *pValue = (int64) m_stValue.nValue;
            return true;
          case DBGW_VAL_TYPE_LONG:
            return getLong(pValue);
          case DBGW_VAL_TYPE_FLOAT:
            *pValue = (int64) m_stValue.fValue;
            return true;
          case DBGW_VAL_TYPE_DOUBLE:
            *pValue = (int64) m_stValue.dValue;
            return true;
          case DBGW_VAL_TYPE_TIME:
          case DBGW_VAL_TYPE_DATE:
          case DBGW_VAL_TYPE_DATETIME:
          case DBGW_VAL_TYPE_BYTES:
          default:
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_LONG);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
    catch (boost::bad_lexical_cast &)
      {
        MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_LONG);
        DBGW_LOG_ERROR(e.what());
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::toFloat(float *pValue) const
  {
    clearException();

    try
      {
        switch (m_type)
          {
          case DBGW_VAL_TYPE_STRING:
          case DBGW_VAL_TYPE_CHAR:
            *pValue = boost::lexical_cast<float>(m_stValue.szValue);
            return true;
          case DBGW_VAL_TYPE_INT:
            *pValue = (float) m_stValue.nValue;
            return true;
          case DBGW_VAL_TYPE_LONG:
            *pValue = (float) m_stValue.lValue;
            return true;
          case DBGW_VAL_TYPE_FLOAT:
            return getFloat(pValue);
          case DBGW_VAL_TYPE_DOUBLE:
            *pValue = (float) m_stValue.dValue;
            return true;
          case DBGW_VAL_TYPE_TIME:
          case DBGW_VAL_TYPE_DATE:
          case DBGW_VAL_TYPE_DATETIME:
          case DBGW_VAL_TYPE_BYTES:
          default:
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_FLOAT);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
    catch (boost::bad_lexical_cast &)
      {
        MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_FLOAT);
        DBGW_LOG_ERROR(e.what());
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::toDouble(double *pValue) const
  {
    clearException();

    try
      {
        switch (m_type)
          {
          case DBGW_VAL_TYPE_STRING:
          case DBGW_VAL_TYPE_CHAR:
            *pValue = boost::lexical_cast<double>(m_stValue.szValue);
            return true;
          case DBGW_VAL_TYPE_INT:
            *pValue = (double) m_stValue.nValue;
            return true;
          case DBGW_VAL_TYPE_LONG:
            *pValue = (double) m_stValue.lValue;
            return true;
          case DBGW_VAL_TYPE_FLOAT:
            *pValue = (double) m_stValue.fValue;
            return true;
          case DBGW_VAL_TYPE_DOUBLE:
            return getDouble(pValue);
          case DBGW_VAL_TYPE_TIME:
          case DBGW_VAL_TYPE_DATE:
          case DBGW_VAL_TYPE_DATETIME:
          case DBGW_VAL_TYPE_BYTES:
          default:
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_DOUBLE);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
    catch (boost::bad_lexical_cast &)
      {
        MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_DOUBLE);
        DBGW_LOG_ERROR(e.what());
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::toChar(char *pValue) const
  {
    clearException();

    try
      {
        switch (m_type)
          {
          case DBGW_VAL_TYPE_INT:
            *pValue = (char) m_stValue.nValue;
            return true;
          case DBGW_VAL_TYPE_LONG:
            *pValue = (char) m_stValue.lValue;
            return true;
          case DBGW_VAL_TYPE_FLOAT:
            *pValue = (char) m_stValue.fValue;
            return true;
          case DBGW_VAL_TYPE_DOUBLE:
            *pValue = (char) m_stValue.dValue;
            return true;
          case DBGW_VAL_TYPE_CHAR:
            return getChar(pValue);
          case DBGW_VAL_TYPE_STRING:
            *pValue = boost::lexical_cast<char>(m_stValue.szValue);
            return true;
          case DBGW_VAL_TYPE_TIME:
          case DBGW_VAL_TYPE_DATE:
          case DBGW_VAL_TYPE_DATETIME:
          case DBGW_VAL_TYPE_BYTES:
          default:
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_CHAR);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
    catch (boost::bad_lexical_cast &)
      {
        MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_CHAR);
        DBGW_LOG_ERROR(e.what());
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::toTime(char **pValue) const
  {
    clearException();

    try
      {
        switch (m_type)
          {
          case DBGW_VAL_TYPE_STRING:
          case DBGW_VAL_TYPE_TIME:
            return getCString(pValue);
          case DBGW_VAL_TYPE_INT:
          case DBGW_VAL_TYPE_LONG:
          case DBGW_VAL_TYPE_FLOAT:
          case DBGW_VAL_TYPE_DOUBLE:
          case DBGW_VAL_TYPE_CHAR:
          case DBGW_VAL_TYPE_DATE:
          case DBGW_VAL_TYPE_DATETIME:
          case DBGW_VAL_TYPE_BYTES:
          default:
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_TIME);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::toDate(char **pValue) const
  {
    clearException();

    try
      {
        switch (m_type)
          {
          case DBGW_VAL_TYPE_STRING:
          case DBGW_VAL_TYPE_DATE:
            return getCString(pValue);
          case DBGW_VAL_TYPE_INT:
          case DBGW_VAL_TYPE_LONG:
          case DBGW_VAL_TYPE_FLOAT:
          case DBGW_VAL_TYPE_DOUBLE:
          case DBGW_VAL_TYPE_CHAR:
          case DBGW_VAL_TYPE_TIME:
          case DBGW_VAL_TYPE_DATETIME:
          case DBGW_VAL_TYPE_BYTES:
          default:
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_DATE);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::toDateTime(char **pValue) const
  {
    clearException();

    try
      {
        switch (m_type)
          {
          case DBGW_VAL_TYPE_STRING:
          case DBGW_VAL_TYPE_DATETIME:
            return getCString(pValue);
          case DBGW_VAL_TYPE_INT:
          case DBGW_VAL_TYPE_LONG:
          case DBGW_VAL_TYPE_FLOAT:
          case DBGW_VAL_TYPE_DOUBLE:
          case DBGW_VAL_TYPE_CHAR:
          case DBGW_VAL_TYPE_TIME:
          case DBGW_VAL_TYPE_DATE:
          case DBGW_VAL_TYPE_BYTES:
          default:
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_DATETIME);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::toBytes(size_t *pSize, char **pValue) const
  {
    clearException();

    try
      {
        if (m_type == DBGW_VAL_TYPE_BYTES)
          {
            return getBytes(pSize, pValue);
          }
        else
          {
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_DATETIME);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  string DBGWValue::toString() const
  {
    clearException();

    try
      {
        if (isNull())
          {
            return "NULL";
          }

        switch (m_type)
          {
          case DBGW_VAL_TYPE_STRING:
          case DBGW_VAL_TYPE_CHAR:
          case DBGW_VAL_TYPE_DATE:
          case DBGW_VAL_TYPE_TIME:
          case DBGW_VAL_TYPE_DATETIME:
            return m_stValue.szValue;
          case DBGW_VAL_TYPE_INT:
            return boost::lexical_cast<string>(m_stValue.nValue);
          case DBGW_VAL_TYPE_LONG:
            return boost::lexical_cast<string>(m_stValue.lValue);
          case DBGW_VAL_TYPE_FLOAT:
            return boost::lexical_cast<string>(m_stValue.fValue);
          case DBGW_VAL_TYPE_DOUBLE:
            return boost::lexical_cast<string>(m_stValue.dValue);
          case DBGW_VAL_TYPE_BYTES:
            return toHexDecimalString();
#ifdef ENABLE_LOB
          case DBGW_VAL_TYPE_CLOB:
            return "(CLOB)";
          case DBGW_VAL_TYPE_BLOB:
            return "(BLOB)";
#endif
          default:
            InvalidValueTypeException e(m_type);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return "";
      }
    catch (std::exception &e)
      {
        setLastException(e);
        return "";
      }
  }

  bool DBGWValue::isNull() const
  {
    if (m_bNull == true)
      {
        return true;
      }
    else if (m_type == DBGW_VAL_TYPE_CHAR || m_type == DBGW_VAL_TYPE_STRING
        || m_type == DBGW_VAL_TYPE_DATETIME)
      {
        return m_stValue.szValue == NULL;
      }
    else
      {
        return false;
      }
  }

  int DBGWValue::size() const
  {
    return m_nSize;
  }

  bool DBGWValue::operator==(const DBGWValue &value) const
  {
    clearException();

    try
      {
        if (getType() != value.getType())
          {
            return toString() == value.toString();
          }

        if (isNull() != value.isNull())
          {
            return false;
          }

        switch (getType())
          {
          case DBGW_VAL_TYPE_INT:
            return m_stValue.nValue == value.m_stValue.nValue;
          case DBGW_VAL_TYPE_LONG:
            return m_stValue.lValue == value.m_stValue.lValue;
          case DBGW_VAL_TYPE_FLOAT:
            return m_stValue.fValue == value.m_stValue.fValue;
          case DBGW_VAL_TYPE_DOUBLE:
            return m_stValue.dValue == value.m_stValue.dValue;
          case DBGW_VAL_TYPE_CHAR:
          case DBGW_VAL_TYPE_STRING:
          case DBGW_VAL_TYPE_DATE:
          case DBGW_VAL_TYPE_TIME:
          case DBGW_VAL_TYPE_DATETIME:
            return toString() == value.toString();
          case DBGW_VAL_TYPE_BYTES:
            if (getLength() != value.getLength())
              {
                return false;
              }

            for (int i = 0, size = getLength(); i < size; i++)
              {
                if (m_stValue.szValue[i] != value.m_stValue.szValue[i])
                  {
                    return false;
                  }
              }

            return true;
          default:
            InvalidValueTypeException e(m_type);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool DBGWValue::operator!=(const DBGWValue &value) const
  {
    return !(operator ==(value));
  }

  void DBGWValue::init(DBGWValueType type, const void *pValue, bool bNull)
  {
    if (type == DBGW_VAL_TYPE_UNDEFINED)
      {
        /**
         * in DBGWCallableStatement,
         * we can't define parameter type.
         * so make undefined value type.
         */
      }

    if (m_type != type)
      {
        MismatchValueTypeException e(m_type, type);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    if (pValue == NULL || bNull)
      {
        m_bNull = true;
        return;
      }

    m_bNull = bNull;
  }

  void DBGWValue::alloc(DBGWValueType type, const void *pValue, bool bNull,
      int nSize)
  {
    init(type, pValue, bNull);

    if (m_bNull)
      {
        return;
      }

    if (isStringBasedValue())
      {
        char *p = (char *) pValue;
        nSize = resize(type, p, nSize);
        memcpy(m_stValue.szValue, p, nSize);

        if (m_type != DBGW_VAL_TYPE_BYTES)
          {
            m_stValue.szValue[nSize] = '\0';
          }
        return;
      }
    else if (m_type == DBGW_VAL_TYPE_INT)
      {
        m_stValue.nValue = *(int *) pValue;
      }
    else if (m_type == DBGW_VAL_TYPE_LONG)
      {
        m_stValue.lValue = *(int64 *) pValue;
      }
    else if (m_type == DBGW_VAL_TYPE_FLOAT)
      {
        m_stValue.fValue = *(float *) pValue;
      }
    else if (m_type == DBGW_VAL_TYPE_DOUBLE)
      {
        m_stValue.dValue = *(double *) pValue;
      }
  }

#ifdef ENABLE_LOB
  void DBGWValue::init(DBGWValueType type, const void *pValue, bool bNull,
      bool bLateBinding)
  {
    if (type == DBGW_VAL_TYPE_UNDEFINED)
      {
        InvalidValueTypeException e(type);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    if (m_type != type)
      {
        MismatchValueTypeException e(m_type, type);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    if ((pValue == NULL && bLateBinding == false) || bNull)
      {
        m_bNull = true;
        return;
      }

    m_bNull = bNull;
  }

  void DBGWValue::alloc(DBGWValueType type, const void *pValue, bool bNull,
      int nSize, bool bLateBinding)
  {
    init(type, pValue, bNull, bLateBinding);

    if (m_bNull)
      {
        return;
      }

    if (isStringBasedValue())
      {
        if (bLateBinding && nSize <= 0)
          {
            InvalidValueSizeException e(nSize);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        if (m_type == DBGW_VAL_TYPE_CHAR)
          {
            nSize = 1;
          }

        char *p = (char *) pValue;

        if (nSize <= 0)
          {
            nSize = strlen(p);
          }

        if (m_nSize <= nSize)
          {
            if (m_nSize == -1 || nSize > MAX_BOUNDARY_SIZE)
              {
                m_nSize = nSize + 1;
              }
            else
              {
                m_nSize = nSize * 2;
              }

            if (m_stValue.szValue != NULL)
              {
                free(m_stValue.szValue);
              }

            m_stValue.szValue = (char *) malloc(m_nSize);
            if (m_stValue.szValue == NULL)
              {
                m_bNull = true;
                m_nSize = -1;
                MemoryAllocationFail e(m_nSize);
                DBGW_LOG_ERROR(e.what());
                throw e;
              }
          }

        if (bLateBinding)
          {
            return;
          }

        memcpy(m_stValue.szValue, p, nSize);
        m_stValue.szValue[nSize] = '\0';
        return;
      }
    else if (bLateBinding)
      {
        return;
      }

    if (m_type == DBGW_VAL_TYPE_INT)
      {
        m_stValue.nValue = *(int *) pValue;
      }
    else if (m_type == DBGW_VAL_TYPE_LONG)
      {
        m_stValue.lValue = *(int64 *) pValue;
      }
    else if (m_type == DBGW_VAL_TYPE_FLOAT)
      {
        m_stValue.fValue = *(float *) pValue;
      }
    else if (m_type == DBGW_VAL_TYPE_DOUBLE)
      {
        m_stValue.dValue = *(double *) pValue;
      }
  }
#endif

  void DBGWValue::alloc(DBGWValueType type, const struct tm *pValue)
  {
    init(type, pValue, false);

    if (m_bNull)
      {
        return;
      }

    if (m_stValue.szValue == NULL)
      {
        if (type == DBGW_VAL_TYPE_DATE)
          {
            // yyyy-mm-dd
            m_nSize = 11;
          }
        else if (type == DBGW_VAL_TYPE_TIME)
          {
            // HH:MM:SS
            m_nSize = 9;
          }
        else
          {
            // yyyy-mm-dd HH:MM:SS
            m_nSize = 20;
          }

        m_stValue.szValue = (char *) malloc(m_nSize);
        if (m_stValue.szValue == NULL)
          {
            m_bNull = true;
            m_nSize = -1;
            MemoryAllocationFail e(m_nSize);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

    char datetime[20];
    if (type == DBGW_VAL_TYPE_TIME)
      {
        snprintf(datetime, 16, "%02d:%02d:%02d", pValue->tm_hour,
            pValue->tm_min, pValue->tm_sec);
      }
    else if (type == DBGW_VAL_TYPE_DATE)
      {
        snprintf(datetime, 16, "%04d-%02d-%02d", 1900 + pValue->tm_year,
            pValue->tm_mon + 1, pValue->tm_mday);
      }
    else
      {
        snprintf(datetime, 16, "%04d-%02d-%02d %02d:%02d:%02d",
            1900 + pValue->tm_year, pValue->tm_mon + 1, pValue->tm_mday,
            pValue->tm_hour, pValue->tm_min, pValue->tm_sec);
      }

    memcpy(m_stValue.szValue, datetime, m_nSize - 1);
    m_stValue.szValue[m_nSize - 1] = '\0';
  }

  int DBGWValue::resize(DBGWValueType type, const char *pValue, int nSize)
  {
    if (m_type == DBGW_VAL_TYPE_CHAR)
      {
        nSize = 1;
      }

    if (nSize <= 0)
      {
        if (m_type == DBGW_VAL_TYPE_BYTES)
          {
            InvalidValueSizeException e(nSize);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        nSize = strlen(pValue);
      }

    if (m_nSize <= nSize)
      {
        if (m_type == DBGW_VAL_TYPE_BYTES)
          {
            m_nSize = nSize;
          }
        else if (m_nSize == -1 || nSize > MAX_BOUNDARY_SIZE)
          {
            m_nSize = nSize + 1;
          }
        else
          {
            m_nSize = nSize * 2;
          }

        if (m_stValue.szValue != NULL)
          {
            free(m_stValue.szValue);
          }

        m_stValue.szValue = (char *) malloc(m_nSize);
        if (m_stValue.szValue == NULL)
          {
            m_bNull = true;
            m_nSize = -1;
            MemoryAllocationFail e(m_nSize);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

    return nSize;
  }

  bool DBGWValue::isStringBasedValue() const
  {
    switch (m_type)
      {
      case DBGW_VAL_TYPE_CHAR:
      case DBGW_VAL_TYPE_DATE:
      case DBGW_VAL_TYPE_TIME:
      case DBGW_VAL_TYPE_DATETIME:
      case DBGW_VAL_TYPE_STRING:
      case DBGW_VAL_TYPE_BYTES:
        return true;
#ifdef ENABLE_LOB
      case DBGW_VAL_TYPE_CLOB:
      case DBGW_VAL_TYPE_BLOB:
        return true;
#endif
      default:
        return false;
      }
  }

  struct tm DBGWValue::toTm() const
  {
    int nYear = 0, nMonth = 0, nDay = 0;
    int nHour = 0, nMin = 0, nSec = 0, nMilSec = 0;

    try
      {
        if (m_type == DBGW_VAL_TYPE_DATE)
          {
            sscanf(m_stValue.szValue, "%d-%d-%d", &nYear, &nMonth, &nDay);

            return boost::posix_time::to_tm(
                boost::posix_time::ptime(
                    boost::gregorian::date(nYear, nMonth, nDay),
                    boost::posix_time::time_duration(nHour, nMin, nSec,
                        nMilSec)));
          }
        else if (m_type == DBGW_VAL_TYPE_TIME)
          {
            sscanf(m_stValue.szValue, "%d:%d:%d.%d", &nHour, &nMin, &nSec,
                &nMilSec);

            return boost::posix_time::to_tm(
                boost::posix_time::time_duration(nHour, nMin, nSec, nMilSec));
          }
        else
          {
            sscanf(m_stValue.szValue, "%d-%d-%d %d:%d:%d.%d", &nYear, &nMonth,
                &nDay, &nHour, &nMin, &nSec, &nMilSec);

            return boost::posix_time::to_tm(
                boost::posix_time::ptime(
                    boost::gregorian::date(nYear, nMonth, nDay),
                    boost::posix_time::time_duration(nHour, nMin, nSec,
                        nMilSec)));
          }
      }
    catch (...)
      {
        InvalidValueFormatException e("DateTime", m_stValue.szValue);
        DBGW_LOG_WARN(e.what());
        throw e;
      }
  }

  string DBGWValue::toHexDecimalString() const
  {
    const char *szSourceHex = "0123456789ABCDEF";
    char szHex[3];
    szHex[2] = '\0';
    stringstream sstream;

#define ADD_HEX_TO_STREAM(i)\
  do {\
      char ch = m_stValue.szValue[i];\
      szHex[0] = szSourceHex[ch >> 4];\
      szHex[1] = szSourceHex[ch & 0xF];\
      sstream << szHex;\
  } while(0)

    sstream << "0x";

    int nHex = 0;

    if (m_nSize < 20)
      {
        for (int i = 0; i < m_nSize; i++)
          {
            if (i % 2 == 0)
              {
                sstream << " ";
              }

            ADD_HEX_TO_STREAM(i);
          }
      }
    else
      {
        sstream << " ";
        ADD_HEX_TO_STREAM(0);
        ADD_HEX_TO_STREAM(1);
        sstream << " ";
        ADD_HEX_TO_STREAM(2);
        ADD_HEX_TO_STREAM(3);
        sstream << " ... ";
        ADD_HEX_TO_STREAM(m_nSize - 4);
        ADD_HEX_TO_STREAM(m_nSize - 3);
        sstream << " ";
        ADD_HEX_TO_STREAM(m_nSize - 2);
        ADD_HEX_TO_STREAM(m_nSize - 1);
      }

    return sstream.str();
  }

  _DBGWValueSet::_DBGWValueSet()
  {
  }

  _DBGWValueSet::~_DBGWValueSet()
  {
    clear();
  }

  bool _DBGWValueSet::set(size_t nIndex, int nValue, bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_INT, &nValue, bNull);
  }

  bool _DBGWValueSet::set(size_t nIndex, const char *szValue, bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_STRING, szValue, bNull);
  }

  bool _DBGWValueSet::set(size_t nIndex, int64 lValue, bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_LONG, &lValue, bNull);
  }

  bool _DBGWValueSet::set(size_t nIndex, char cValue, bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_CHAR, &cValue, bNull);
  }

  bool _DBGWValueSet::set(size_t nIndex, float fValue, bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_FLOAT, &fValue, bNull);
  }

  bool _DBGWValueSet::set(size_t nIndex, double dValue, bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_DOUBLE, &dValue, bNull);
  }

  bool _DBGWValueSet::set(size_t nIndex, DBGWValueType type,
      const struct tm &tmValue)
  {
    clearException();

    try
      {
        if (m_valueList.size() <= nIndex)
          {
            m_valueList.resize(nIndex + 1);
          }

        DBGWValueSharedPtr p(new DBGWValue(type, tmValue));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        _DBGWValuePair pair;
        pair.value = p;

        m_valueList[nIndex] = pair;
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::set(size_t nIndex, DBGWValueType type, const void *pValue,
      bool bNull, int nSize)
  {
    clearException();
    try
      {
        if (m_valueList.size() <= nIndex)
          {
            m_valueList.resize(nIndex + 1);
          }

        DBGWValueSharedPtr p(new DBGWValue(type, pValue, bNull, nSize));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        _DBGWValuePair pair;
        pair.value = p;

        m_valueList[nIndex] = pair;
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::set(size_t nIndex, size_t nSize, const void *pValue,
      bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_BYTES, pValue, bNull, nSize);
  }

  bool _DBGWValueSet::put(const char *szKey, int nValue, bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_INT, &nValue, bNull);
  }

  bool _DBGWValueSet::put(const char *szKey, const char *szValue, bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_STRING, szValue, bNull);
  }

  bool _DBGWValueSet::put(const char *szKey, int64 lValue, bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_LONG, &lValue, bNull);
  }

  bool _DBGWValueSet::put(const char *szKey, char cValue, bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_CHAR, &cValue, bNull);
  }

  bool _DBGWValueSet::put(const char *szKey, float fValue, bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_FLOAT, &fValue, bNull);
  }

  bool _DBGWValueSet::put(const char *szKey, double dValue, bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_DOUBLE, &dValue, bNull);
  }

  bool _DBGWValueSet::put(const char *szKey, DBGWValueType type,
      const struct tm &tmValue)
  {
    clearException();

    try
      {
        DBGWValueSharedPtr p(new DBGWValue(type, tmValue));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        _DBGWValuePair pair;
        pair.key = szKey;
        pair.value = p;

        m_valueList.push_back(pair);
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::put(const char *szKey, DBGWValueType type, const void *pValue,
      bool bNull, int nSize)
  {
    clearException();

    try
      {
        DBGWValueSharedPtr p(new DBGWValue(type, pValue, bNull, nSize));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        _DBGWValuePair pair;
        pair.key = szKey;
        pair.value = p;

        m_valueList.push_back(pair);
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::put(const char *szKey, size_t nSize, const void *pValue,
      bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_BYTES, pValue, bNull, nSize);
  }

  bool _DBGWValueSet::put(int nValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_INT, &nValue, bNull);
  }

  bool _DBGWValueSet::put(const char *szValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_STRING, szValue, bNull);
  }

  bool _DBGWValueSet::put(int64 lValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_LONG, &lValue, bNull);
  }

  bool _DBGWValueSet::put(char cValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_CHAR, &cValue, bNull);
  }

  bool _DBGWValueSet::put(float fValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_FLOAT, &fValue, bNull);
  }

  bool _DBGWValueSet::put(double dValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_DOUBLE, &dValue, bNull);
  }

  bool _DBGWValueSet::put(DBGWValueType type, const struct tm &tmValue)
  {
    clearException();

    try
      {
        DBGWValueSharedPtr p(new DBGWValue(type, tmValue));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }
        _DBGWValuePair pair;
        pair.value = p;

        m_valueList.push_back(pair);
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::put(DBGWValueType type, const void *pValue,
      bool bNull, int nSize)
  {
    clearException();

    try
      {
        DBGWValueSharedPtr p(new DBGWValue(type, pValue, bNull, nSize));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }
        _DBGWValuePair pair;
        pair.value = p;

        m_valueList.push_back(pair);
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::put(size_t nSize, const void *pValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_BYTES, pValue, bNull, nSize);
  }

#ifdef ENABLE_LOB
  bool _DBGWValueSet::replace(size_t nIndex, DBGWValueType type, bool bNull,
      int nSize)
  {
    clearException();

    try
      {
        if (m_valueList.size() <= nIndex || m_valueList[nIndex] == NULL)
          {
            NotExistSetException e(nIndex);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        m_valueList[nIndex]->set(type, bNull, nSize);
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }
#endif

  bool _DBGWValueSet::replace(size_t nIndex, DBGWValueType type, void *pValue,
      bool bNull, int nSize)
  {
    clearException();

    try
      {
        if (m_valueList.size() <= nIndex || m_valueList[nIndex].value == NULL)
          {
            ArrayIndexOutOfBoundsException e(nIndex, "DBGWValueSet");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        m_valueList[nIndex].value->set(type, pValue, bNull, nSize);
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  void _DBGWValueSet::clear()
  {
    /**
     * We don't need to clear error because this api will not make error.
     *
     * clearException();
     *
     * try
     * {
     * 		blur blur blur;
     * }
     * catch (DBGWException &e)
     * {
     * 		setLastException(e);
     * }
     */
    m_valueList.clear();
  }

  const DBGWValue *_DBGWValueSet::getValue(const char *szKey) const
  {
    clearException();

    try
      {
        DBGWValueList::const_iterator it = m_valueList.begin();
        for (; it != m_valueList.end(); it++)
          {
            if (!strcmp(it->key.c_str(), szKey))
              {
                return it->value.get();
              }
          }

        NotExistKeyException e(szKey, "_DBGWValueSet");
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return NULL;
      }
  }

  const DBGWValue *_DBGWValueSet::getValueWithoutException(
      const char *szKey) const
  {
    DBGWValueList::const_iterator it = m_valueList.begin();
    for (; it != m_valueList.end(); it++)
      {
        if (!strcmp(it->key.c_str(), szKey))
          {
            return it->value.get();
          }
      }

    return NULL;
  }

  bool _DBGWValueSet::getInt(const char *szKey, int *pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getInt(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getCString(const char *szKey, char **pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getCString(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getLong(const char *szKey, int64 *pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getLong(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getChar(const char *szKey, char *pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getChar(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getFloat(const char *szKey, float *pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getFloat(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getDouble(const char *szKey, double *pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getDouble(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getDateTime(const char *szKey, struct tm *pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getDateTime(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getBytes(const char *szKey, size_t *pSize, char **pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getBytes(pSize, pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getType(const char *szKey, DBGWValueType *pType) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            *pType = p->getType();
            return true;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::isNull(const char *szKey, bool *pNull) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            *pNull = p->isNull();
            return true;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  const DBGWValue *_DBGWValueSet::getValue(size_t nIndex) const
  {
    clearException();

    try
      {
        if (nIndex >= m_valueList.size() || m_valueList[nIndex].value == NULL)
          {
            ArrayIndexOutOfBoundsException e(nIndex, "_DBGWValueSet");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        return m_valueList[nIndex].value.get();
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return NULL;
      }
  }

  bool _DBGWValueSet::getInt(int nIndex, int *pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getInt(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getCString(int nIndex, char **pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getCString(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getLong(int nIndex, int64 *pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getLong(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getChar(int nIndex, char *pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getChar(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getFloat(int nIndex, float *pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getFloat(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getDouble(int nIndex, double *pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getDouble(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getDateTime(int nIndex, struct tm *pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getDateTime(pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getBytes(int nIndex, size_t *pSize, char **pValue) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getBytes(pSize, pValue);
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::getType(int nIndex, DBGWValueType *pType) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            *pType = p->getType();
            return true;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _DBGWValueSet::isNull(int nIndex, bool *pNull) const
  {
    clearException();

    try
      {
        const DBGWValue *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            *pNull = p->isNull();
            return true;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  size_t _DBGWValueSet::size() const
  {
    /**
     * We don't need to clear error because this api will not make error.
     *
     * clearException();
     *
     * try
     * {
     * 		blur blur blur;
     * }
     * catch (DBGWException &e)
     * {
     * 		setLastException(e);
     * }
     */
    return m_valueList.size();
  }

  _DBGWValueSet &_DBGWValueSet::operator=(const _DBGWValueSet &valueSet)
  {
    /**
     * We don't need to clear error because this api will not make error.
     *
     * clearException();
     *
     * try
     * {
     * 		blur blur blur;
     * }
     * catch (DBGWException &e)
     * {
     * 		setLastException(e);
     * }
     */
    clear();

    m_valueList = DBGWValueList(valueSet.m_valueList.begin(),
        valueSet.m_valueList.end());
    return *this;
  }

  _DBGWParameter::_DBGWParameter()
  {
  }

  _DBGWParameter::~_DBGWParameter()
  {
  }

  const DBGWValue *_DBGWParameter::getValue(size_t nIndex) const
  {
    return _DBGWValueSet::getValue(nIndex);
  }

  const DBGWValue *_DBGWParameter::getValue(const char *szKey, size_t nPosition) const
  {
    clearException();

    try
      {
        const DBGWValue *pValue = getValueWithoutException(szKey);
        if (pValue == NULL)
          {
            pValue = _DBGWValueSet::getValue(nPosition);
            if (pValue == NULL)
              {
                throw getLastException();
              }
          }

        return pValue;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return NULL;
      }
  }

}
