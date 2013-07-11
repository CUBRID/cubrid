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

#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Lob.h"
#include "dbgw3/Value.h"
#include "dbgw3/ValueSet.h"

namespace dbgw
{

  _ValueSet::_ValueSet()
  {
  }

  _ValueSet::~_ValueSet()
  {
  }

  bool _ValueSet::set(size_t nIndex, int nValue, bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_INT, &nValue, bNull);
  }

  bool _ValueSet::set(size_t nIndex, const char *szValue, bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_STRING, szValue, bNull);
  }

  bool _ValueSet::set(size_t nIndex, int64 lValue, bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_LONG, &lValue, bNull);
  }

  bool _ValueSet::set(size_t nIndex, char cValue, bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_CHAR, &cValue, bNull);
  }

  bool _ValueSet::set(size_t nIndex, float fValue, bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_FLOAT, &fValue, bNull);
  }

  bool _ValueSet::set(size_t nIndex, double dValue, bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_DOUBLE, &dValue, bNull);
  }

  bool _ValueSet::set(size_t nIndex, ValueType type,
      const struct tm &tmValue)
  {
    clearException();

    try
      {
        if (m_valueList.size() <= nIndex)
          {
            m_valueList.resize(nIndex + 1);
          }

        trait<Value>::sp p(new Value(type, tmValue));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        m_valueList[nIndex] = _ValuePair("", p);
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::set(size_t nIndex, ValueType type, const void *pValue,
      bool bNull, int nSize)
  {
    clearException();
    try
      {
        if (m_valueList.size() <= nIndex)
          {
            m_valueList.resize(nIndex + 1);
          }

        trait<Value>::sp p(new Value(type, pValue, bNull, nSize));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        m_valueList[nIndex] = _ValuePair("", p);
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::set(size_t nIndex, size_t nSize, const void *pValue,
      bool bNull)
  {
    return set(nIndex, DBGW_VAL_TYPE_BYTES, pValue, bNull, nSize);
  }

  bool _ValueSet::set(size_t nIndex, ValueType type,
      trait<Lob>::sp pLob, bool bNull)
  {
    clearException();
    try
      {
        if (m_valueList.size() <= nIndex)
          {
            m_valueList.resize(nIndex + 1);
          }

        trait<Value>::sp p(new Value(type, pLob, bNull));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        m_valueList[nIndex] = _ValuePair("", p);
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::set(size_t nIndex, trait<sql::ResultSet>::sp pResultSet)
  {
    clearException();
    try
      {
        if (m_valueList.size() <= nIndex)
          {
            m_valueList.resize(nIndex + 1);
          }

        trait<Value>::sp p(new Value(pResultSet));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        m_valueList[nIndex] = _ValuePair("", p);
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::put(const char *szKey, int nValue, bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_INT, &nValue, bNull);
  }

  bool _ValueSet::put(const char *szKey, const char *szValue, bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_STRING, szValue, bNull);
  }

  bool _ValueSet::put(const char *szKey, int64 lValue, bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_LONG, &lValue, bNull);
  }

  bool _ValueSet::put(const char *szKey, char cValue, bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_CHAR, &cValue, bNull);
  }

  bool _ValueSet::put(const char *szKey, float fValue, bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_FLOAT, &fValue, bNull);
  }

  bool _ValueSet::put(const char *szKey, double dValue, bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_DOUBLE, &dValue, bNull);
  }

  bool _ValueSet::put(const char *szKey, ValueType type,
      const struct tm &tmValue)
  {
    clearException();

    try
      {
        trait<Value>::sp p(new Value(type, tmValue));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        m_valueList.push_back(_ValuePair(szKey, p));
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::put(const char *szKey, ValueType type, const void *pValue,
      bool bNull, int nSize)
  {
    clearException();

    try
      {
        trait<Value>::sp p(new Value(type, pValue, bNull, nSize));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        m_valueList.push_back(_ValuePair(szKey, p));
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::put(const char *szKey, size_t nSize, const void *pValue,
      bool bNull)
  {
    return put(szKey, DBGW_VAL_TYPE_BYTES, pValue, bNull, nSize);
  }

  bool _ValueSet::put(const char *szKey, ValueType type,
      trait<Lob>::sp pLob, bool bNull)
  {
    clearException();

    try
      {
        trait<Value>::sp p(new Value(type, pLob, bNull));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        m_valueList.push_back(_ValuePair(szKey, p));
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::put(const char *szKey, const _ExternelSource &source)
  {
    clearException();

    try
      {
        trait<Value>::sp p(new Value(source));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        m_valueList.push_back(_ValuePair(szKey, p));
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::put(const char *szKey, trait<sql::ResultSet>::sp pResultSet)
  {
    clearException();

    try
      {
        trait<Value>::sp p(new Value(pResultSet));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        m_valueList.push_back(_ValuePair(szKey, p));
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::put(int nValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_INT, &nValue, bNull);
  }

  bool _ValueSet::put(const char *szValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_STRING, szValue, bNull);
  }

  bool _ValueSet::put(int64 lValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_LONG, &lValue, bNull);
  }

  bool _ValueSet::put(char cValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_CHAR, &cValue, bNull);
  }

  bool _ValueSet::put(float fValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_FLOAT, &fValue, bNull);
  }

  bool _ValueSet::put(double dValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_DOUBLE, &dValue, bNull);
  }

  bool _ValueSet::put(ValueType type, const struct tm &tmValue)
  {
    clearException();

    try
      {
        trait<Value>::sp p(new Value(type, tmValue));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        m_valueList.push_back(_ValuePair("", p));
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::put(ValueType type, const void *pValue,
      bool bNull, int nSize)
  {
    clearException();

    try
      {
        trait<Value>::sp p(new Value(type, pValue, bNull, nSize));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        m_valueList.push_back(_ValuePair("", p));
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::put(size_t nSize, const void *pValue, bool bNull)
  {
    return put(DBGW_VAL_TYPE_BYTES, pValue, bNull, nSize);
  }

  bool _ValueSet::put(ValueType type, trait<Lob>::sp pLob, bool bNull)
  {
    clearException();

    try
      {
        trait<Value>::sp p(new Value(type, pLob, bNull));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        m_valueList.push_back(_ValuePair("", p));
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::put(const _ExternelSource &source)
  {
    clearException();

    try
      {
        trait<Value>::sp p(new Value(source));
        if (getLastErrorCode() != DBGW_ER_NO_ERROR)
          {
            throw getLastException();
          }

        m_valueList.push_back(_ValuePair("", p));
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::replace(size_t nIndex, ValueType type, void *pValue,
      bool bNull, int nSize)
  {
    clearException();

    try
      {
        if (m_valueList.size() <= nIndex || m_valueList[nIndex].second == NULL)
          {
            ArrayIndexOutOfBoundsException e(nIndex, "ValueSet");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        m_valueList[nIndex].second->set(type, pValue, bNull, nSize);
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::replace(size_t nIndex, const _ExternelSource &source)
  {
    clearException();

    try
      {
        if (m_valueList.size() <= nIndex || m_valueList[nIndex].second == NULL)
          {
            ArrayIndexOutOfBoundsException e(nIndex, "ValueSet");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        m_valueList[nIndex].second->set(source);
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::replace(size_t nIndex, trait<sql::ResultSet>::sp pResultSet)
  {
    clearException();

    try
      {
        if (m_valueList.size() <= nIndex || m_valueList[nIndex].second == NULL)
          {
            ArrayIndexOutOfBoundsException e(nIndex, "ValueSet");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        m_valueList[nIndex].second->set(pResultSet);
        return true;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  void _ValueSet::clear()
  {
    /**
     * We don't need to clear error because this api will not make error.
     *
     * clearException();
     *
     * try
     * {
     *          blur blur blur;
     * }
     * catch (Exception &e)
     * {
     *          setLastException(e);
     * }
     */
    m_valueList.clear();
  }

  const Value *_ValueSet::getValue(const char *szKey) const
  {
    clearException();

    try
      {
        _ValueList::const_iterator it = m_valueList.begin();
        for (; it != m_valueList.end(); it++)
          {
            if (!strcmp(it->first.c_str(), szKey))
              {
                return it->second.get();
              }
          }

        NotExistKeyException e(szKey, "_ValueSet");
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return NULL;
      }
  }

  const Value *_ValueSet::getValueWithoutException(
      const char *szKey) const
  {
    _ValueList::const_iterator it = m_valueList.begin();
    for (; it != m_valueList.end(); it++)
      {
        if (!strcmp(it->first.c_str(), szKey))
          {
            return it->second.get();
          }
      }

    return NULL;
  }

  bool _ValueSet::getInt(const char *szKey, int *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->toInt(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getCString(const char *szKey, const char **pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getCString(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getLong(const char *szKey, int64 *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->toLong(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getChar(const char *szKey, char *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getChar(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getFloat(const char *szKey, float *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->toFloat(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getDouble(const char *szKey, double *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->toDouble(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getDateTime(const char *szKey, struct tm *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getDateTime(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getBytes(const char *szKey, size_t *pSize,
      const char **pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getBytes(pSize, pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getType(const char *szKey, ValueType *pType) const
  {
    clearException();

    try
      {
        const Value *p = getValue(szKey);
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
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getLength(const char *szKey, int *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            *pValue = p->getLength();
            return true;
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  trait<Lob>::sp _ValueSet::getClob(const char *szKey) const
  {
    clearException();

    try
      {
        const Value *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getClob();
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return trait<Lob>::sp();
      }
  }

  trait<Lob>::sp _ValueSet::getBlob(const char *szKey) const
  {
    clearException();

    try
      {
        const Value *p = getValue(szKey);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getBlob();
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return trait<Lob>::sp();
      }
  }

  bool _ValueSet::isNull(const char *szKey, bool *pNull) const
  {
    clearException();

    try
      {
        const Value *p = getValue(szKey);
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
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  const Value *_ValueSet::getValue(size_t nIndex) const
  {
    clearException();

    try
      {
        if (nIndex >= m_valueList.size() || m_valueList[nIndex].second == NULL)
          {
            ArrayIndexOutOfBoundsException e(nIndex, "_ValueSet");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        return m_valueList[nIndex].second.get();
      }
    catch (Exception &e)
      {
        setLastException(e);
        return NULL;
      }
  }

  bool _ValueSet::getInt(int nIndex, int *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->toInt(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getCString(int nIndex, const char **pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getCString(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getLong(int nIndex, int64 *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->toLong(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getChar(int nIndex, char *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getChar(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getFloat(int nIndex, float *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->toFloat(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getDouble(int nIndex, double *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->toDouble(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getDateTime(int nIndex, struct tm *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getDateTime(pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getBytes(int nIndex, size_t *pSize, const char **pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getBytes(pSize, pValue);
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  trait<Lob>::sp _ValueSet::getClob(int nIndex) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getClob();
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return trait<Lob>::sp();
      }
  }

  trait<Lob>::sp _ValueSet::getBlob(int nIndex) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getBlob();
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return trait<Lob>::sp();
      }
  }

  trait<sql::ResultSet>::sp _ValueSet::getResultSet(int nIndex) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            return p->getResultSet();
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return trait<sql::ResultSet>::sp();
      }
  }

  bool _ValueSet::getType(int nIndex, ValueType *pType) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
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
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::getLength(int nIndex, int *pValue) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
        if (p == NULL)
          {
            throw getLastException();
          }
        else
          {
            *pValue = p->getLength();
            return true;
          }
      }
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  bool _ValueSet::isNull(int nIndex, bool *pNull) const
  {
    clearException();

    try
      {
        const Value *p = getValue(nIndex);
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
    catch (Exception &e)
      {
        setLastException(e);
        return false;
      }
  }

  size_t _ValueSet::size() const
  {
    return m_valueList.size();
  }

  _ValueSet &_ValueSet::operator=(const _ValueSet &valueSet)
  {
    clear();

    m_valueList = valueSet.m_valueList;
    return *this;
  }

  Value *_ValueSet::operator[](size_t nIndex)
  {
    return m_valueList[nIndex].second.get();
  }

  _Parameter::_Parameter()
  {
  }

  _Parameter::~_Parameter()
  {
  }

  const Value *_Parameter::getValue(size_t nIndex) const
  {
    return _ValueSet::getValue(nIndex);
  }

  const Value *_Parameter::getValue(const char *szKey, size_t nPosition) const
  {
    clearException();

    try
      {
        const Value *pValue = getValueWithoutException(szKey);
        if (pValue == NULL)
          {
            pValue = _ValueSet::getValue(nPosition);
            if (pValue == NULL)
              {
                throw getLastException();
              }
          }

        return pValue;
      }
    catch (Exception &e)
      {
        setLastException(e);
        return NULL;
      }
  }

}
