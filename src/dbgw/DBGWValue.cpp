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
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWValue.h"
#include "DBGWLogger.h"

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
      default:
        return "UNDEFINED";
      }
  }

  DBGWValue::DBGWValue() :
    m_type(DBGW_VAL_TYPE_UNDEFINED), m_bNull(false)
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
    m_stValue.szValue = NULL;
  }

  DBGWValue::DBGWValue(DBGWValueType type, void *pValue) :
    m_type(type)
  {
    clearException();
    try
      {
        if (pValue == NULL)
          {
            m_stValue.szValue = NULL;
            m_bNull = true;
          }
        else
          {
            switch (m_type)
              {
              case DBGW_VAL_TYPE_INT:
                m_stValue.nValue = *(int *) pValue;
                m_bNull = false;
                break;
              case DBGW_VAL_TYPE_LONG:
                m_stValue.lValue = *(int64 *) pValue;
                m_bNull = false;
                break;
              case DBGW_VAL_TYPE_STRING:
                if (*(char **) pValue == NULL)
                  {
                    m_stValue.szValue = NULL;
                    m_bNull = true;
                  }
                else
                  {
                    m_stValue.szValue = strdup((char *) pValue);
                    m_bNull = false;
                  }
                break;
              case DBGW_VAL_TYPE_CHAR:
                m_stValue.szValue = (char *) malloc(sizeof(char) * 2);
                m_stValue.szValue[0] = *(char *) pValue;
                m_stValue.szValue[1] = '\0';
                m_bNull = false;
                break;
              default:
                InvalidValueTypeException e(type);
                DBGW_LOG_ERROR(e.what());
                throw e;
              }
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
      }
  }

  DBGWValue::DBGWValue(int nValue, bool bNull) :
    m_type(DBGW_VAL_TYPE_INT), m_bNull(bNull)
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
    m_stValue.nValue = nValue;
  }

  DBGWValue::DBGWValue(const char *szValue, bool bNull) :
    m_type(DBGW_VAL_TYPE_STRING), m_bNull(bNull)
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
    if (szValue != NULL)
      {
        m_stValue.szValue = strdup(szValue);
      }
    else
      {
        m_stValue.szValue = NULL;
      }
  }

  DBGWValue::DBGWValue(const DBGWRawValue &rawValue, DBGWValueType type,
      bool bNull) :
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
    switch (type)
      {
      case DBGW_VAL_TYPE_STRING:
      case DBGW_VAL_TYPE_CHAR:
        if (rawValue.szValue != NULL)
          {
            m_stValue.szValue = strdup(rawValue.szValue);
          }
        else
          {
            m_stValue.szValue = NULL;
          }
        break;
      default:
        m_stValue = rawValue;
        break;
      }
  }

  DBGWValue::DBGWValue(const DBGWValue &value) :
    m_type(value.m_type), m_bNull(value.m_bNull)
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
    if (m_type == DBGW_VAL_TYPE_STRING)
      {
        if (!value.m_stValue.szValue)
          {
            m_stValue.szValue = strdup(value.m_stValue.szValue);
          }
        else
          {
            m_stValue.szValue = NULL;
          }
      }
    else
      {
        m_stValue = value.m_stValue;
      }
  }

  DBGWValue::DBGWValue(int64 lValue, bool bNull) :
    m_type(DBGW_VAL_TYPE_LONG), m_bNull(bNull)
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
    m_stValue.lValue = lValue;
  }

  DBGWValue::DBGWValue(char cValue, bool bNull) :
    m_type(DBGW_VAL_TYPE_CHAR), m_bNull(bNull)
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
    m_stValue.szValue = (char *) malloc(sizeof(char) * 2);
    m_stValue.szValue[0] = cValue;
    m_stValue.szValue[1] = '\0';
  }

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
    if (m_type == DBGW_VAL_TYPE_STRING || m_type == DBGW_VAL_TYPE_CHAR)
      {
        if (m_stValue.szValue != NULL)
          {
            free(m_stValue.szValue);
          }
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
        if (m_type != DBGW_VAL_TYPE_STRING && m_type != DBGW_VAL_TYPE_CHAR)
          {
            MismatchValueTypeException e(m_type, DBGW_VAL_TYPE_STRING);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        *pValue = m_stValue.szValue;
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

        if (m_stValue.szValue == NULL)
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
    if (m_type == DBGW_VAL_TYPE_STRING || m_type == DBGW_VAL_TYPE_CHAR)
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
          case DBGW_VAL_TYPE_STRING:
            if (m_stValue.szValue == NULL)
              {
                return 0;
              }
            return strlen(m_stValue.szValue);
          case DBGW_VAL_TYPE_CHAR:
            return sizeof(char);
          case DBGW_VAL_TYPE_INT:
            return sizeof(int);
          case DBGW_VAL_TYPE_LONG:
            return sizeof(int64);
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

  string DBGWValue::toString() const
  {
    clearException();

    try
      {
        if (m_bNull)
          {
            return "NULL";
          }

        switch (m_type)
          {
          case DBGW_VAL_TYPE_STRING:
          case DBGW_VAL_TYPE_CHAR:
            if (m_stValue.szValue == NULL)
              {
                return "";
              }
            return m_stValue.szValue;
          case DBGW_VAL_TYPE_INT:
            return boost::lexical_cast<string>(m_stValue.nValue);
          case DBGW_VAL_TYPE_LONG:
            return boost::lexical_cast<string>(m_stValue.lValue);
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
  }

  bool DBGWValue::isNull() const
  {
    return m_bNull;
  }

  bool DBGWValue::operator==(const DBGWValue &value) const
  {
    clearException();

    try
      {
        if (getType() != value.getType())
          {
            return false;
          }

        switch (getType())
          {
          case DBGW_VAL_TYPE_INT:
            return m_stValue.nValue == value.m_stValue.nValue;
          case DBGW_VAL_TYPE_LONG:
            return m_stValue.lValue == value.m_stValue.lValue;
          case DBGW_VAL_TYPE_CHAR:
            return *m_stValue.szValue == *value.m_stValue.szValue;
          case DBGW_VAL_TYPE_STRING:
            return strcmp(m_stValue.szValue, m_stValue.szValue) == 0;
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

  DBGWValueSet::DBGWValueSet()
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
  }

  DBGWValueSet::~DBGWValueSet()
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
  }

  void DBGWValueSet::set(size_t nIndex, DBGWValueSharedPtr pValue)
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
    if (m_valueList.size() <= nIndex)
      {
        m_valueList.resize(nIndex + 1);
      }

    if (m_valueList[nIndex] != NULL)
      {
        removeIndexMap(nIndex);
      }

    m_valueList[nIndex] = pValue;
  }

  void DBGWValueSet::set(size_t nIndex, int nValue, bool bNull)
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
    if (m_valueList.size() <= nIndex)
      {
        m_valueList.resize(nIndex + 1);
      }

    if (m_valueList[nIndex] != NULL)
      {
        removeIndexMap(nIndex);
      }

    DBGWValueSharedPtr p(new DBGWValue(nValue, bNull));
    m_valueList[nIndex] = p;
  }

  void DBGWValueSet::set(size_t nIndex, const char *szValue, bool bNull)
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
    if (m_valueList.size() <= nIndex)
      {
        m_valueList.resize(nIndex + 1);
      }

    if (m_valueList[nIndex] != NULL)
      {
        removeIndexMap(nIndex);
      }

    DBGWValueSharedPtr p(new DBGWValue(szValue, bNull));
    m_valueList[nIndex] = p;
  }

  void DBGWValueSet::set(size_t nIndex, int64 lValue, bool bNull)
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
    if (m_valueList.size() <= nIndex)
      {
        m_valueList.resize(nIndex + 1);
      }

    if (m_valueList[nIndex] != NULL)
      {
        removeIndexMap(nIndex);
      }

    DBGWValueSharedPtr p(new DBGWValue(lValue, bNull));
    m_valueList[nIndex] = p;
  }

  void DBGWValueSet::set(size_t nIndex, char cValue, bool bNull)
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
    if (m_valueList.size() <= nIndex)
      {
        m_valueList.resize(nIndex + 1);
      }

    if (m_valueList[nIndex] != NULL)
      {
        removeIndexMap(nIndex);
      }

    DBGWValueSharedPtr p(new DBGWValue(cValue, bNull));
    m_valueList[nIndex] = p;
  }

  bool DBGWValueSet::set(size_t nIndex, DBGWValueType type, void *pValue)
  {
    clearException();
    try
      {
        if (m_valueList.size() <= nIndex)
          {
            m_valueList.resize(nIndex + 1);
          }

        if (m_valueList[nIndex]->getType() != DBGW_VAL_TYPE_UNDEFINED)
          {
            removeIndexMap(nIndex);
          }

        DBGWValueSharedPtr p(new DBGWValue(type, pValue));
        if (getLastErrorCode() != DBGWErrorCode::NO_ERROR)
          {
            throw getLastException();
          }
        m_valueList[nIndex] = p;
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  void DBGWValueSet::put(const char *szKey, DBGWValueSharedPtr pValue)
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
    m_indexMap[szKey] = m_valueList.size();
    m_valueList.push_back(pValue);
  }

  void DBGWValueSet::put(const char *szKey, int nValue, bool bNull)
  {
    DBGWValueSharedPtr p(new DBGWValue(nValue, bNull));
    m_indexMap[szKey] = m_valueList.size();
    m_valueList.push_back(p);
  }

  void DBGWValueSet::put(const char *szKey, const char *szValue, bool bNull)
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
    DBGWValueSharedPtr p(new DBGWValue(szValue, bNull));
    m_indexMap[szKey] = m_valueList.size();
    m_valueList.push_back(p);
  }

  void DBGWValueSet::put(const char *szKey, int64 lValue, bool bNull)
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
    DBGWValueSharedPtr p(new DBGWValue(lValue, bNull));
    m_indexMap[szKey] = m_valueList.size();
    m_valueList.push_back(p);
  }

  void DBGWValueSet::put(const char *szKey, char cValue, bool bNull)
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
    DBGWValueSharedPtr p(new DBGWValue(cValue, bNull));
    m_indexMap[szKey] = m_valueList.size();
    m_valueList.push_back(p);
  }

  bool DBGWValueSet::put(const char *szKey, DBGWValueType type, void *pValue)
  {
    clearException();
    try
      {
        DBGWValueSharedPtr p(new DBGWValue(type, pValue));
        if (getLastErrorCode() != DBGWErrorCode::NO_ERROR)
          {
            throw getLastException();
          }
        m_indexMap[szKey] = m_valueList.size();
        m_valueList.push_back(p);
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  void DBGWValueSet::put(DBGWValueSharedPtr pValue)
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
    m_valueList.push_back(pValue);
  }

  void DBGWValueSet::put(int nValue, bool bNull)
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
    DBGWValueSharedPtr p(new DBGWValue(nValue, bNull));
    m_valueList.push_back(p);
  }

  void DBGWValueSet::put(const char *szValue, bool bNull)
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
    DBGWValueSharedPtr p(new DBGWValue(szValue, bNull));
    m_valueList.push_back(p);
  }

  void DBGWValueSet::put(int64 lValue, bool bNull)
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
    DBGWValueSharedPtr p(new DBGWValue(lValue, bNull));
    m_valueList.push_back(p);
  }

  void DBGWValueSet::put(char cValue, bool bNull)
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
    DBGWValueSharedPtr p(new DBGWValue(cValue, bNull));
    m_valueList.push_back(p);
  }

  bool DBGWValueSet::put(DBGWValueType type, void *pValue)
  {
    clearException();
    try
      {
        DBGWValueSharedPtr p(new DBGWValue(type, pValue));
        if (getLastErrorCode() != DBGWErrorCode::NO_ERROR)
          {
            throw getLastException();
          }
        m_valueList.push_back(p);
        return true;
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return false;
      }
  }

  void DBGWValueSet::clear()
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
    m_indexMap.clear();
  }

  const DBGWValue *DBGWValueSet::getValue(const char *szKey) const
  {
    clearException();

    try
      {
        DBGWValueIndexMap::const_iterator it = m_indexMap.find(szKey);
        if (it != m_indexMap.end())
          {
            return m_valueList[it->second].get();
          }
        else
          {
            NotExistSetException e(szKey);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return NULL;
      }
  }

  const DBGWValue *DBGWValueSet::getValueWithoutException(const char *szKey) const
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
    DBGWValueIndexMap::const_iterator it = m_indexMap.find(szKey);
    if (it != m_indexMap.end())
      {
        return m_valueList[it->second].get();
      }
    else
      {
        return NULL;
      }
  }

  bool DBGWValueSet::getInt(const char *szKey, int *pValue) const
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

  bool DBGWValueSet::getCString(const char *szKey, char **pValue) const
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

  bool DBGWValueSet::getLong(const char *szKey, int64 *pValue) const
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

  bool DBGWValueSet::getChar(const char *szKey, char *pValue) const
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

  const DBGWValue *DBGWValueSet::getValue(size_t nIndex) const
  {
    clearException();

    try
      {
        if (nIndex >= m_valueList.size())
          {
            NotExistSetException e(nIndex);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        return m_valueList[nIndex].get();
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        return NULL;
      }
  }

  bool DBGWValueSet::getInt(int nIndex, int *pValue) const
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

  bool DBGWValueSet::getCString(int nIndex, char **pValue) const
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

  bool DBGWValueSet::getLong(int nIndex, int64 *pValue) const
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

  bool DBGWValueSet::getChar(int nIndex, char *pValue) const
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

  size_t DBGWValueSet::size() const
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

  DBGWValueSet &DBGWValueSet::operator=(const DBGWValueSet &valueSet)
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
    m_indexMap = DBGWValueIndexMap(valueSet.m_indexMap.begin(),
        valueSet.m_indexMap.end());
    return *this;
  }

  void DBGWValueSet::removeIndexMap(int nIndex)
  {
    for (DBGWValueIndexMap::iterator it = m_indexMap.begin(); it
        != m_indexMap.end(); it++)
      {
        if (it->second == nIndex)
          {
            m_indexMap.erase(it);
            return;
          }
      }
  }

  DBGWParameter::DBGWParameter()
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
  }

  DBGWParameter::~DBGWParameter()
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
  }

  const DBGWValue *DBGWParameter::getValue(const char *szKey, size_t nPosition) const
  {
    clearException();

    try
      {
        const DBGWValue *pValue = getValueWithoutException(szKey);
        if (pValue == NULL)
          {
            pValue = DBGWValueSet::getValue(nPosition);
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

