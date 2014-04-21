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

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/lexical_cast.hpp>
#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Lob.h"
#include "dbgw3/Value.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "porting.h"

namespace dbgw
{

  const char *getValueTypeString(int type)
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
      case DBGW_VAL_TYPE_CLOB:
        return "CLOB";
      case DBGW_VAL_TYPE_BLOB:
        return "BLOB";
      case DBGW_VAL_TYPE_RESULTSET:
        return "RESULTSET";
      case DBGW_VAL_TYPE_BOOL:
        return "BOOL";
      default:
        return "UNDEFINED";
      }
  }

  class Value::Impl
  {
  public:
    Impl(ValueType type, struct tm tmValue) :
      m_type(type), m_bIsNull(false), m_nBufferSize(-1),
      m_nValueSize(-1), m_bIsLinked(false)
    {
      clearException();
      try
        {
          memset(&m_stValue, 0, sizeof(_RawValue));
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
      catch (Exception &e)
        {
          setLastException(e);
        }
    }

    Impl(ValueType type, const void *pValue, bool bIsNull,
        int nSize) :
      m_type(type), m_bIsNull(bIsNull), m_nBufferSize(-1),
      m_nValueSize(-1), m_bIsLinked(false)
    {
      clearException();
      try
        {
          memset(&m_stValue, 0, sizeof(_RawValue));
          alloc(type, pValue, bIsNull, nSize);
        }
      catch (Exception &e)
        {
          setLastException(e);
        }
    }

    Impl(ValueType type, trait<Lob>::sp pLob, bool bIsNull) :
      m_type(type), m_pLob(pLob), m_bIsNull(bIsNull), m_nBufferSize(-1),
      m_nValueSize(-1), m_bIsLinked(false)
    {
      memset(&m_stValue, 0, sizeof(_RawValue));

      if (m_type != DBGW_VAL_TYPE_CLOB && m_type != DBGW_VAL_TYPE_BLOB)
        {
          InvalidValueTypeException e(m_type);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      if (m_bIsNull == false)
        {
          m_nBufferSize = pLob->length();
          m_nValueSize = m_nBufferSize;
        }
    }

    Impl(trait<sql::ResultSet>::sp pResultSet) :
      m_type(DBGW_VAL_TYPE_RESULTSET), m_pResultSet(pResultSet),
      m_bIsNull(pResultSet == NULL), m_nBufferSize(-1), m_nValueSize(-1),
      m_bIsLinked(false)
    {
    }

    Impl(const _ExternelSource &source) :
      m_type(source.type), m_bIsNull(source.isNull),
      m_nBufferSize(source.length), m_nValueSize(source.length),
      m_bIsLinked(true)
    {
      m_stValue = source.value;
      m_pLob = source.lob;
      m_pResultSet = source.resultSet;
    }

    Impl(const Impl *pImpl) :
      m_type(pImpl->m_type), m_pLob(pImpl->m_pLob),
      m_bIsNull(pImpl->m_bIsNull), m_nBufferSize(pImpl->m_nBufferSize),
      m_nValueSize(pImpl->m_nValueSize), m_bIsLinked(pImpl->m_bIsLinked)
    {
      clearException();

      try
        {
          if (m_bIsLinked)
            {
              m_stValue = pImpl->m_stValue;
              return;
            }

          memset(&m_stValue, 0, sizeof(_RawValue));
          if (isStringBasedValue() && m_bIsNull == false)
            {
              m_stValue.p = new char[m_nBufferSize];
              memcpy(m_stValue.p, pImpl->m_stValue.p, m_nBufferSize);
            }
          else
            {
              m_stValue = pImpl->m_stValue;
            }
        }
      catch (Exception &e)
        {
          setLastException(e);
        }
    }

    ~Impl()
    {
      clear();
    }

    void clear()
    {
      if (m_bIsLinked)
        {
          return;
        }

      if (isStringBasedValue() && m_stValue.p != NULL)
        {
          delete[] m_stValue.p;
          m_nBufferSize = -1;
          m_nValueSize = -1;
        }
    }

    bool set(ValueType type, void *pValue, bool bIsNull, int nSize)
    {
      clearException();

      try
        {
          if (m_bIsLinked)
            {
              return true;
            }

          if (m_type != type)
            {
              clear();
            }

          alloc(type, pValue, bIsNull, nSize);
          return true;
        }
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }
    }

    void set(trait<sql::ResultSet>::sp pResultSet)
    {
      if (m_bIsLinked)
        {
          return;
        }

      if (m_type != DBGW_VAL_TYPE_RESULTSET)
        {
          clear();
        }

      m_type = DBGW_VAL_TYPE_RESULTSET;
      m_bIsNull = pResultSet == NULL;
      m_nBufferSize = -1;
      m_nValueSize = -1;
    }

    void set(const _ExternelSource &source)
    {
      if (m_bIsLinked == false)
        {
          return;
        }

      m_type = source.type;
      m_bIsNull = source.isNull;
      m_nBufferSize = source.length;
      m_nValueSize = source.length;
      m_stValue = source.value;
      m_pLob = source.lob;
    }

    bool getCString(const char **pValue) const
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
              *pValue = m_stValue.p;
            }

          return true;
        }
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool getChar(char *pValue) const
    {
      clearException();

      try
        {
          if ((m_type == DBGW_VAL_TYPE_STRING && m_nValueSize > 1)
              || (m_type != DBGW_VAL_TYPE_STRING && m_type != DBGW_VAL_TYPE_CHAR))
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
              *pValue = *m_stValue.p;
            }
          return true;
        }
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool getDateTime(struct tm *pValue) const
    {
      clearException();

      try
        {
          memset(pValue, 0, sizeof(struct tm));

          if (m_type != DBGW_VAL_TYPE_DATETIME && m_type != DBGW_VAL_TYPE_DATE
              && m_type != DBGW_VAL_TYPE_TIME)
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
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool getBytes(size_t *pSize, const char **pValue) const
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

          *pSize = m_nBufferSize;
          *pValue = m_stValue.p;
          return true;
        }
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }
    }

    trait<Lob>::sp getClob() const
    {
      clearException();

      try
        {
          if (m_type != DBGW_VAL_TYPE_CLOB)
            {
              MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_CLOB);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return m_pLob;
        }
      catch (Exception &e)
        {
          setLastException(e);
          return trait<Lob>::sp();
        }
    }

    trait<Lob>::sp getBlob() const
    {
      clearException();

      try
        {
          if (m_type != DBGW_VAL_TYPE_BLOB)
            {
              MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_BLOB);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return m_pLob;
        }
      catch (Exception &e)
        {
          setLastException(e);
          return trait<Lob>::sp();
        }
    }

    trait<sql::ResultSet>::sp getResultSet() const
    {
      clearException();

      try
        {
          if (m_type != DBGW_VAL_TYPE_RESULTSET)
            {
              MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_RESULTSET);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return m_pResultSet;
        }
      catch (Exception &e)
        {
          setLastException(e);
          return trait<sql::ResultSet>::sp();
        }
    }

    bool getBool(bool *pValue) const
    {
      clearException();

      try
        {
          if (m_type != DBGW_VAL_TYPE_BOOL)
            {
              MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_BOOL);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          if (m_stValue.n != 0)
            {
              *pValue = true;
            }
          else
            {
              *pValue = false;
            }

          return true;
        }
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }
    }

    ValueType getType() const
    {
      return m_type;
    }

    void *getVoidPtr() const
    {
      if (isStringBasedValue())
        {
          return (void *) m_stValue.p;
        }
      else
        {
          return (void *) &m_stValue;
        }
    }

    int getLength() const
    {
      clearException();

      try
        {
          if (m_type == DBGW_VAL_TYPE_UNDEFINED ||
              m_type == DBGW_VAL_TYPE_RESULTSET)
            {
              InvalidValueTypeException e(m_type);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return m_nValueSize;
        }
      catch (Exception &e)
        {
          setLastException(e);
          return -1;
        }
    }

    bool toInt(int *pValue) const
    {
      clearException();

      try
        {
          switch (m_type)
            {
            case DBGW_VAL_TYPE_STRING:
            case DBGW_VAL_TYPE_CHAR:
              *pValue = boost::lexical_cast<int>(m_stValue.p);
              return true;
            case DBGW_VAL_TYPE_INT:
              *pValue = m_stValue.n;
              return true;
            case DBGW_VAL_TYPE_LONG:
              *pValue = (int) m_stValue.l;
              return true;
            case DBGW_VAL_TYPE_FLOAT:
              *pValue = (int) m_stValue.f;
              return true;
            case DBGW_VAL_TYPE_DOUBLE:
              *pValue = (int) m_stValue.d;
              return true;
            case DBGW_VAL_TYPE_TIME:
            case DBGW_VAL_TYPE_DATE:
            case DBGW_VAL_TYPE_DATETIME:
            case DBGW_VAL_TYPE_BYTES:
            case DBGW_VAL_TYPE_CLOB:
            case DBGW_VAL_TYPE_BLOB:
            case DBGW_VAL_TYPE_RESULTSET:
            default:
              MismatchValueTypeException e(m_type, toString(),
                  DBGW_VAL_TYPE_INT);
              DBGW_LOG_WARN(e.what());
              throw e;
            }
        }
      catch (Exception &e)
        {
          *pValue = 0;
          setLastException(e);
          return false;
        }
      catch (boost::bad_lexical_cast &)
        {
          *pValue = 0;
          MismatchValueTypeException e(m_type, toString(), DBGW_VAL_TYPE_INT);
          DBGW_LOG_WARN(e.what());
          setLastException(e);
          return false;
        }
    }

    bool toLong(int64 *pValue) const
    {
      clearException();

      try
        {
          switch (m_type)
            {
            case DBGW_VAL_TYPE_STRING:
            case DBGW_VAL_TYPE_CHAR:
              *pValue = boost::lexical_cast<int64>(m_stValue.p);
              return true;
            case DBGW_VAL_TYPE_INT:
              *pValue = (int64) m_stValue.n;
              return true;
            case DBGW_VAL_TYPE_LONG:
              *pValue = m_stValue.l;
              return true;
            case DBGW_VAL_TYPE_FLOAT:
              *pValue = (int64) m_stValue.f;
              return true;
            case DBGW_VAL_TYPE_DOUBLE:
              *pValue = (int64) m_stValue.d;
              return true;
            case DBGW_VAL_TYPE_TIME:
            case DBGW_VAL_TYPE_DATE:
            case DBGW_VAL_TYPE_DATETIME:
            case DBGW_VAL_TYPE_BYTES:
            case DBGW_VAL_TYPE_CLOB:
            case DBGW_VAL_TYPE_BLOB:
            case DBGW_VAL_TYPE_RESULTSET:
            default:
              MismatchValueTypeException e(m_type, toString(),
                  DBGW_VAL_TYPE_LONG);
              DBGW_LOG_WARN(e.what());
              throw e;
            }
        }
      catch (Exception &e)
        {
          *pValue = 0;
          setLastException(e);
          return false;
        }
      catch (boost::bad_lexical_cast &)
        {
          *pValue = 0;
          MismatchValueTypeException e(m_type, toString(), DBGW_VAL_TYPE_LONG);
          DBGW_LOG_WARN(e.what());
          setLastException(e);
          return false;
        }
    }

    bool toFloat(float *pValue) const
    {
      clearException();

      try
        {
          switch (m_type)
            {
            case DBGW_VAL_TYPE_STRING:
            case DBGW_VAL_TYPE_CHAR:
              *pValue = boost::lexical_cast<float>(m_stValue.p);
              return true;
            case DBGW_VAL_TYPE_INT:
              *pValue = (float) m_stValue.n;
              return true;
            case DBGW_VAL_TYPE_LONG:
              *pValue = (float) m_stValue.l;
              return true;
            case DBGW_VAL_TYPE_FLOAT:
              *pValue = m_stValue.f;
              return true;
            case DBGW_VAL_TYPE_DOUBLE:
              *pValue = (float) m_stValue.d;
              return true;
            case DBGW_VAL_TYPE_TIME:
            case DBGW_VAL_TYPE_DATE:
            case DBGW_VAL_TYPE_DATETIME:
            case DBGW_VAL_TYPE_BYTES:
            case DBGW_VAL_TYPE_CLOB:
            case DBGW_VAL_TYPE_BLOB:
            case DBGW_VAL_TYPE_RESULTSET:
            default:
              MismatchValueTypeException e(m_type, toString(),
                  DBGW_VAL_TYPE_FLOAT);
              DBGW_LOG_WARN(e.what());
              throw e;
            }
        }
      catch (Exception &e)
        {
          *pValue = 0.0f;
          setLastException(e);
          return false;
        }
      catch (boost::bad_lexical_cast &)
        {
          *pValue = 0.0f;
          MismatchValueTypeException e(m_type, toString(), DBGW_VAL_TYPE_FLOAT);
          DBGW_LOG_WARN(e.what());
          setLastException(e);
          return false;
        }
    }

    bool toDouble(double *pValue) const
    {
      clearException();

      try
        {
          switch (m_type)
            {
            case DBGW_VAL_TYPE_STRING:
            case DBGW_VAL_TYPE_CHAR:
              *pValue = boost::lexical_cast<double>(m_stValue.p);
              return true;
            case DBGW_VAL_TYPE_INT:
              *pValue = (double) m_stValue.n;
              return true;
            case DBGW_VAL_TYPE_LONG:
              *pValue = (double) m_stValue.l;
              return true;
            case DBGW_VAL_TYPE_FLOAT:
              *pValue = (double) m_stValue.f;
              return true;
            case DBGW_VAL_TYPE_DOUBLE:
              *pValue = m_stValue.d;
              return true;
            case DBGW_VAL_TYPE_TIME:
            case DBGW_VAL_TYPE_DATE:
            case DBGW_VAL_TYPE_DATETIME:
            case DBGW_VAL_TYPE_BYTES:
            case DBGW_VAL_TYPE_CLOB:
            case DBGW_VAL_TYPE_BLOB:
            case DBGW_VAL_TYPE_RESULTSET:
            default:
              MismatchValueTypeException e(m_type, toString(),
                  DBGW_VAL_TYPE_DOUBLE);
              DBGW_LOG_WARN(e.what());
              throw e;
            }
        }
      catch (Exception &e)
        {
          *pValue = 0.0f;
          setLastException(e);
          return false;
        }
      catch (boost::bad_lexical_cast &)
        {
          *pValue = 0.0f;
          MismatchValueTypeException e(m_type, toString(),
              DBGW_VAL_TYPE_DOUBLE);
          DBGW_LOG_WARN(e.what());
          setLastException(e);
          return false;
        }
    }

    bool toChar(char *pValue) const
    {
      clearException();

      try
        {
          switch (m_type)
            {
            case DBGW_VAL_TYPE_INT:
              *pValue = (char) m_stValue.n;
              return true;
            case DBGW_VAL_TYPE_LONG:
              *pValue = (char) m_stValue.l;
              return true;
            case DBGW_VAL_TYPE_FLOAT:
              *pValue = (char) m_stValue.f;
              return true;
            case DBGW_VAL_TYPE_DOUBLE:
              *pValue = (char) m_stValue.d;
              return true;
            case DBGW_VAL_TYPE_CHAR:
              return getChar(pValue);
            case DBGW_VAL_TYPE_STRING:
              *pValue = boost::lexical_cast<char>(m_stValue.p);
              return true;
            case DBGW_VAL_TYPE_TIME:
            case DBGW_VAL_TYPE_DATE:
            case DBGW_VAL_TYPE_DATETIME:
            case DBGW_VAL_TYPE_BYTES:
            case DBGW_VAL_TYPE_CLOB:
            case DBGW_VAL_TYPE_BLOB:
            case DBGW_VAL_TYPE_RESULTSET:
            default:
              MismatchValueTypeException e(m_type, toString(),
                  DBGW_VAL_TYPE_CHAR);
              DBGW_LOG_WARN(e.what());
              throw e;
            }
        }
      catch (Exception &e)
        {
          *pValue = 0;
          setLastException(e);
          return false;
        }
      catch (boost::bad_lexical_cast &)
        {
          *pValue = 0;
          MismatchValueTypeException e(m_type, toString(), DBGW_VAL_TYPE_CHAR);
          DBGW_LOG_WARN(e.what());
          setLastException(e);
          return false;
        }
    }

    bool toTime(const char **pValue) const
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
            case DBGW_VAL_TYPE_CLOB:
            case DBGW_VAL_TYPE_BLOB:
            case DBGW_VAL_TYPE_RESULTSET:
            default:
              MismatchValueTypeException e(m_type, toString(),
                  DBGW_VAL_TYPE_TIME);
              DBGW_LOG_WARN(e.what());
              throw e;
            }
        }
      catch (Exception &e)
        {
          *pValue = "00:00:00";
          setLastException(e);
          return false;
        }
    }

    bool toDate(const char **pValue) const
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
            case DBGW_VAL_TYPE_CLOB:
            case DBGW_VAL_TYPE_BLOB:
            case DBGW_VAL_TYPE_RESULTSET:
            default:
              MismatchValueTypeException e(m_type, toString(),
                  DBGW_VAL_TYPE_DATE);
              DBGW_LOG_WARN(e.what());
              throw e;
            }
        }
      catch (Exception &e)
        {
          *pValue = "1970-01-01";
          setLastException(e);
          return false;
        }
    }

    bool toDateTime(const char **pValue) const
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
            case DBGW_VAL_TYPE_CLOB:
            case DBGW_VAL_TYPE_BLOB:
            case DBGW_VAL_TYPE_RESULTSET:
            default:
              MismatchValueTypeException e(m_type, toString(),
                  DBGW_VAL_TYPE_DATETIME);
              DBGW_LOG_WARN(e.what());
              throw e;
            }
        }
      catch (Exception &e)
        {
          *pValue = "1970-01-01 00:00:00";
          setLastException(e);
          return false;
        }
    }

    bool toBytes(size_t *pSize, const char **pValue) const
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
              MismatchValueTypeException e(m_type, toString(),
                  DBGW_VAL_TYPE_DATETIME);
              DBGW_LOG_WARN(e.what());
              throw e;
            }
        }
      catch (Exception &e)
        {
          *pSize = 0;
          *pValue = (char *) "";
          setLastException(e);
          return false;
        }
    }

    bool toBool(bool *pValue) const
    {
      clearException();

      try
        {
          switch (m_type)
            {
            case DBGW_VAL_TYPE_BOOL:
              return getBool(pValue);
            case DBGW_VAL_TYPE_INT:
              return m_stValue.n != 0 ? *pValue = true : *pValue = false;
            case DBGW_VAL_TYPE_LONG:
              return m_stValue.l != 0 ? *pValue = true : *pValue = false;
            case DBGW_VAL_TYPE_FLOAT:
              return m_stValue.f != 0 ? *pValue = true : *pValue = false;
            case DBGW_VAL_TYPE_DOUBLE:
              return m_stValue.d != 0 ? *pValue = true : *pValue = false;
            case DBGW_VAL_TYPE_STRING:
              if (strcasecmp(m_stValue.p, "true") == 0)
                {
                  return true;
                }
              else if (strcasecmp(m_stValue.p, "false") == 0)
                {
                  return false;
                }
              else
                {
                  MismatchValueTypeException e(m_type, toString(),
                      DBGW_VAL_TYPE_DATE);
                  DBGW_LOG_WARN(e.what());
                  throw e;
                }
            case DBGW_VAL_TYPE_DATE:
            case DBGW_VAL_TYPE_CHAR:
            case DBGW_VAL_TYPE_TIME:
            case DBGW_VAL_TYPE_DATETIME:
            case DBGW_VAL_TYPE_BYTES:
            case DBGW_VAL_TYPE_CLOB:
            case DBGW_VAL_TYPE_BLOB:
            case DBGW_VAL_TYPE_RESULTSET:
            default:
              MismatchValueTypeException e(m_type, toString(),
                  DBGW_VAL_TYPE_DATE);
              DBGW_LOG_WARN(e.what());
              throw e;
            }
        }
      catch (Exception &e)
        {
          *pValue = false;
          setLastException(e);
          return false;
        }
    }

    std::string toString() const
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
            case DBGW_VAL_TYPE_BOOL:
              return m_stValue.n != 0 ? "TRUE" : "FALSE";
            case DBGW_VAL_TYPE_STRING:
            case DBGW_VAL_TYPE_CHAR:
            case DBGW_VAL_TYPE_DATE:
            case DBGW_VAL_TYPE_TIME:
            case DBGW_VAL_TYPE_DATETIME:
              return m_stValue.p;
            case DBGW_VAL_TYPE_INT:
              return boost::lexical_cast<std::string>(m_stValue.n);
            case DBGW_VAL_TYPE_LONG:
              return boost::lexical_cast<std::string>(m_stValue.l);
            case DBGW_VAL_TYPE_FLOAT:
              return boost::lexical_cast<std::string>(m_stValue.f);
            case DBGW_VAL_TYPE_DOUBLE:
              return boost::lexical_cast<std::string>(m_stValue.d);
            case DBGW_VAL_TYPE_BYTES:
              return toHexDecimalString();
            case DBGW_VAL_TYPE_CLOB:
              return "(CLOB)";
            case DBGW_VAL_TYPE_BLOB:
              return "(BLOB)";
            case DBGW_VAL_TYPE_RESULTSET:
              return "(RESULTSET)";
            default:
              InvalidValueTypeException e(m_type);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
        }
      catch (Exception &e)
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

    bool isNull() const
    {
      if (m_bIsNull == true)
        {
          return true;
        }
      else if (m_type == DBGW_VAL_TYPE_CHAR || m_type == DBGW_VAL_TYPE_STRING
          || m_type == DBGW_VAL_TYPE_DATETIME)
        {
          return m_stValue.p == NULL;
        }
      else
        {
          return false;
        }
    }

    int size() const
    {
      return m_nBufferSize;
    }

    bool operator==(const Impl *pImpl) const
    {
      clearException();

      try
        {
          if (getType() != pImpl->getType())
            {
              return toString() == pImpl->toString();
            }

          if (isNull() != pImpl->isNull())
            {
              return false;
            }

          switch (getType())
            {
            case DBGW_VAL_TYPE_INT:
              return m_stValue.n == pImpl->m_stValue.n;
            case DBGW_VAL_TYPE_LONG:
              return m_stValue.l == pImpl->m_stValue.l;
            case DBGW_VAL_TYPE_FLOAT:
              return m_stValue.f == pImpl->m_stValue.f;
            case DBGW_VAL_TYPE_DOUBLE:
              return m_stValue.d == pImpl->m_stValue.d;
            case DBGW_VAL_TYPE_CHAR:
            case DBGW_VAL_TYPE_STRING:
            case DBGW_VAL_TYPE_DATE:
            case DBGW_VAL_TYPE_TIME:
            case DBGW_VAL_TYPE_DATETIME:
              return toString() == pImpl->toString();
            case DBGW_VAL_TYPE_BYTES:
              if (getLength() != pImpl->getLength())
                {
                  return false;
                }

              for (int i = 0, size = getLength(); i < size; i++)
                {
                  if (m_stValue.p[i] != pImpl->m_stValue.p[i])
                    {
                      return false;
                    }
                }

              return true;
            case DBGW_VAL_TYPE_CLOB:
            case DBGW_VAL_TYPE_BLOB:
              return true;
            case DBGW_VAL_TYPE_RESULTSET:
            default:
              InvalidValueTypeException e(m_type);
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
        }
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool operator!=(const Impl *pImpl) const
    {
      return !(operator ==(pImpl));
    }

    void init(ValueType type, const void *pValue, bool bIsNull)
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

      if (pValue == NULL || bIsNull)
        {
          m_bIsNull = true;
          return;
        }

      m_bIsNull = bIsNull;
    }

    void calcValueSize(const void *pValue, int nSize)
    {
      if (isStringBasedValue())
        {
          if (m_type == DBGW_VAL_TYPE_CHAR)
            {
              m_nValueSize = nSize = 1;
            }

          if (m_bIsNull)
            {
              return;
            }

          if (nSize <= 0)
            {
              if (m_type == DBGW_VAL_TYPE_BYTES)
                {
                  InvalidValueSizeException e(nSize);
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
              else if (m_type == DBGW_VAL_TYPE_DATE)
                {
                  // yyyy-mm-dd
                  nSize = 10;
                }
              else if (m_type == DBGW_VAL_TYPE_TIME)
                {
                  // HH:MM:SS
                  nSize = 8;
                }
              else if (m_type == DBGW_VAL_TYPE_DATETIME)
                {
                  // yyyy-mm-dd HH:MM:SS
                  nSize = 19;
                }
              else if (m_type == DBGW_VAL_TYPE_STRING)
                {
                  if (pValue != NULL)
                    {
                      nSize = strlen((char *) pValue);
                    }
                }
            }

          m_nValueSize = nSize;
        }
      else if (m_type == DBGW_VAL_TYPE_INT || m_type == DBGW_VAL_TYPE_BOOL)
        {
          m_nValueSize = sizeof(int);
        }
      else if (m_type == DBGW_VAL_TYPE_LONG)
        {
          m_nValueSize = sizeof(int64);
        }
      else if (m_type == DBGW_VAL_TYPE_FLOAT)
        {
          m_nValueSize = sizeof(float);
        }
      else if (m_type == DBGW_VAL_TYPE_DOUBLE)
        {
          m_nValueSize = sizeof(double);
        }
    }

    void alloc(ValueType type, const void *pValue, bool bIsNull,
        int nSize)
    {
      init(type, pValue, bIsNull);
      calcValueSize(pValue, nSize);

      if (m_bIsNull)
        {
          return;
        }

      if (isStringBasedValue())
        {
          char *p = (char *) pValue;
          resize(type, p, m_nValueSize);
          memcpy(m_stValue.p, p, m_nValueSize);

          if (m_type != DBGW_VAL_TYPE_BYTES)
            {
              m_stValue.p[m_nValueSize] = '\0';
            }
          return;
        }
      else if (m_type == DBGW_VAL_TYPE_INT)
        {
          m_stValue.n = *(int *) pValue;
        }
      else if (m_type == DBGW_VAL_TYPE_LONG)
        {
          m_stValue.l = *(int64 *) pValue;
        }
      else if (m_type == DBGW_VAL_TYPE_FLOAT)
        {
          m_stValue.f = *(float *) pValue;
        }
      else if (m_type == DBGW_VAL_TYPE_DOUBLE)
        {
          m_stValue.d = *(double *) pValue;
        }
      else if (m_type == DBGW_VAL_TYPE_BOOL)
        {
          m_stValue.n = *(bool *) pValue;
        }
    }

    void alloc(ValueType type, const struct tm *pValue)
    {
      init(type, pValue, false);
      calcValueSize(pValue, -1);

      if (m_bIsNull)
        {
          return;
        }

      if (m_stValue.p == NULL)
        {
          m_nBufferSize = m_nValueSize + 1;

          m_stValue.p = new char[m_nBufferSize];
        }

      char datetime[20];
      if (type == DBGW_VAL_TYPE_TIME)
        {
          snprintf(datetime, m_nBufferSize, "%02d:%02d:%02d", pValue->tm_hour,
              pValue->tm_min, pValue->tm_sec);
        }
      else if (type == DBGW_VAL_TYPE_DATE)
        {
          snprintf(datetime, m_nBufferSize, "%04d-%02d-%02d",
              1900 + pValue->tm_year, pValue->tm_mon + 1, pValue->tm_mday);
        }
      else
        {
          snprintf(datetime, m_nBufferSize, "%04d-%02d-%02d %02d:%02d:%02d",
              1900 + pValue->tm_year, pValue->tm_mon + 1, pValue->tm_mday,
              pValue->tm_hour, pValue->tm_min, pValue->tm_sec);
        }

      memcpy(m_stValue.p, datetime, m_nValueSize);
      m_stValue.p[m_nValueSize] = '\0';
    }

    void resize(ValueType type, const char *pValue, int nSize)
    {
      if (m_nBufferSize <= nSize)
        {
          if (m_type == DBGW_VAL_TYPE_BYTES)
            {
              m_nBufferSize = nSize;
            }
          else if (m_nBufferSize == -1 || nSize > MAX_BOUNDARY_SIZE())
            {
              m_nBufferSize = nSize + 1;
            }
          else
            {
              m_nBufferSize = nSize * 2;
            }

          if (m_stValue.p != NULL)
            {
              delete[] m_stValue.p;
            }

          m_stValue.p = new char[m_nBufferSize];
        }
    }

    bool isStringBasedValue() const
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
        case DBGW_VAL_TYPE_CLOB:
        case DBGW_VAL_TYPE_BLOB:
        default:
          return false;
        }
    }

    struct tm toTm() const
    {
      int nYear = 0, nMonth = 0, nDay = 0;
      int nHour = 0, nMin = 0, nSec = 0, nMilSec = 0;

      try
        {
          if (m_type == DBGW_VAL_TYPE_DATE)
            {
              sscanf(m_stValue.p, "%d-%d-%d", &nYear, &nMonth, &nDay);

              return boost::posix_time::to_tm(
                  boost::posix_time::ptime(
                      boost::gregorian::date(nYear, nMonth, nDay),
                      boost::posix_time::time_duration(nHour, nMin, nSec,
                          nMilSec)));
            }
          else if (m_type == DBGW_VAL_TYPE_TIME)
            {
              sscanf(m_stValue.p, "%d:%d:%d.%d", &nHour, &nMin, &nSec,
                  &nMilSec);

              return boost::posix_time::to_tm(
                  boost::posix_time::time_duration(nHour, nMin, nSec, nMilSec));
            }
          else
            {
              sscanf(m_stValue.p, "%d-%d-%d %d:%d:%d.%d", &nYear, &nMonth,
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
          InvalidValueFormatException e("DateTime", m_stValue.p);
          DBGW_LOG_WARN(e.what());
          throw e;
        }
    }

    std::string toHexDecimalString() const
    {
      const char *szSourceHex = "0123456789ABCDEF";
      char szHex[3];
      szHex[2] = '\0';
      std::stringstream sstream;

#define ADD_HEX_TO_STREAM(i)\
    do {\
        char ch = m_stValue.p[i];\
        szHex[0] = szSourceHex[ch >> 4];\
        szHex[1] = szSourceHex[ch & 0xF];\
        sstream << szHex;\
    } while(0)

      sstream << "0x";

      int nHex = 0;

      if (m_nBufferSize < 20)
        {
          for (int i = 0; i < m_nBufferSize; i++)
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
          ADD_HEX_TO_STREAM(m_nBufferSize - 4);
          ADD_HEX_TO_STREAM(m_nBufferSize - 3);
          sstream << " ";
          ADD_HEX_TO_STREAM(m_nBufferSize - 2);
          ADD_HEX_TO_STREAM(m_nBufferSize - 1);
        }

      return sstream.str();
    }

  private:
    ValueType m_type;
    _RawValue m_stValue;
    trait<Lob>::sp m_pLob;
    trait<sql::ResultSet>::sp m_pResultSet;
    bool m_bIsNull;
    int m_nBufferSize;
    int m_nValueSize;
    bool m_bIsLinked;
  };

  int Value::MAX_BOUNDARY_SIZE()
  {
    return 1024 * 1024 * 1;   // 1 MByte
  }

  int Value::MAX_TEMP_SIZE()
  {
    return 1024 * 4;
  }

  Value::Value(ValueType type, struct tm tmValue) :
    m_pImpl(new Impl(type, tmValue))
  {
  }

  Value::Value(ValueType type, const void *pValue, bool bIsNull,
      int nSize) :
    m_pImpl(new Impl(type, pValue, bIsNull, nSize))
  {
  }

  Value::Value(ValueType type, trait<Lob>::sp pLob, bool bIsNull) :
    m_pImpl(new Impl(type, pLob, bIsNull))
  {
  }

  Value::Value(trait<sql::ResultSet>::sp pResultSet) :
    m_pImpl(new Impl(pResultSet))
  {
  }

  Value::Value(const _ExternelSource &source) :
    m_pImpl(new Impl(source))
  {
  }

  Value::Value(const Value &value) :
    m_pImpl(new Impl(value.m_pImpl))
  {
  }

  Value::~Value()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  bool Value::set(ValueType type, void *pValue, bool bIsNull, int nSize)
  {
    return m_pImpl->set(type, pValue, bIsNull, nSize);
  }

  void Value::set(trait<sql::ResultSet>::sp pResultSet)
  {
    m_pImpl->set(pResultSet);
  }

  void Value::set(const _ExternelSource &source)
  {
    m_pImpl->set(source);
  }

  bool Value::getCString(const char **pValue) const
  {
    return m_pImpl->getCString(pValue);
  }

  bool Value::getChar(char *pValue) const
  {
    return m_pImpl->getChar(pValue);
  }

  bool Value::getDateTime(struct tm *pValue) const
  {
    return m_pImpl->getDateTime(pValue);
  }

  bool Value::getBytes(size_t *pSize, const char **pValue) const
  {
    return m_pImpl->getBytes(pSize, pValue);
  }

  trait<Lob>::sp Value::getClob() const
  {
    return m_pImpl->getClob();
  }

  trait<Lob>::sp Value::getBlob() const
  {
    return m_pImpl->getBlob();
  }

  trait<sql::ResultSet>::sp Value::getResultSet() const
  {
    return m_pImpl->getResultSet();
  }

  bool Value::getBool(bool *pValue) const
  {
    return m_pImpl->getBool(pValue);
  }

  ValueType Value::getType() const
  {
    return m_pImpl->getType();
  }

  void *Value::getVoidPtr() const
  {
    return m_pImpl->getVoidPtr();
  }

  int Value::getLength() const
  {
    return m_pImpl->getLength();
  }

  bool Value::toInt(int *pValue) const
  {
    return m_pImpl->toInt(pValue);
  }

  bool Value::toLong(int64 *pValue) const
  {
    return m_pImpl->toLong(pValue);
  }

  bool Value::toFloat(float *pValue) const
  {
    return m_pImpl->toFloat(pValue);
  }

  bool Value::toDouble(double *pValue) const
  {
    return m_pImpl->toDouble(pValue);
  }

  bool Value::toChar(char *pValue) const
  {
    return m_pImpl->toChar(pValue);
  }

  bool Value::toTime(const char **pValue) const
  {
    return m_pImpl->toTime(pValue);
  }

  bool Value::toDate(const char **pValue) const
  {
    return m_pImpl->toDate(pValue);
  }

  bool Value::toDateTime(const char **pValue) const
  {
    return m_pImpl->toDateTime(pValue);
  }

  bool Value::toBytes(size_t *pSize, const char **pValue) const
  {
    return m_pImpl->toBytes(pSize, pValue);
  }

  bool Value::toBool(bool *pValue) const
  {
    return m_pImpl->toBool(pValue);
  }

  std::string Value::toString() const
  {
    return m_pImpl->toString();
  }

  bool Value::isNull() const
  {
    return m_pImpl->isNull();
  }

  int Value::size() const
  {
    return m_pImpl->size();
  }

  bool Value::operator==(const Value &value) const
  {
    return *m_pImpl == value.m_pImpl;
  }

  bool Value::operator!=(const Value &value) const
  {
    return !(operator ==(value));
  }

}
