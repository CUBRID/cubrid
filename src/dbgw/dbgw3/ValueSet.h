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

#ifndef DBGWVALUESET_H_
#define DBGWVALUESET_H_

namespace dbgw
{

  namespace sql
  {
    class ResultSet;
  }

  class Value;

  typedef std::pair<std::string, trait<Value>::sp> _ValuePair;
  typedef std::vector<_ValuePair> _ValueList;

  class _Parameter;
  typedef std::vector<_Parameter> _ParameterList;

  class _ValueSet
  {
  public:
    _ValueSet();
    virtual ~ _ValueSet();

    bool set(size_t nIndex, int nValue, bool bNull = false);
    bool set(size_t nIndex, const char *szValue, bool bNull = false);
    bool set(size_t nIndex, int64 lValue, bool bNull = false);
    bool set(size_t nIndex, char cValue, bool bNull = false);
    bool set(size_t nIndex, float fValue, bool bNull = false);
    bool set(size_t nIndex, double dValue, bool bNull = false);
    bool set(size_t nIndex, ValueType type, const struct tm &tmValue);
    bool set(size_t nIndex, ValueType type, const void *pValue,
        bool bNull = false, int nSize = -1);
    bool set(size_t nIndex, size_t nSize, const void *pValue,
        bool bNull = false);
    bool set(size_t nIndex, ValueType type, trait<Lob>::sp pLob,
        bool bNull = false);
    bool set(size_t nIndex, trait<sql::ResultSet>::sp pResultSet);
    bool put(const char *szKey, int nValue, bool bNull = false);
    bool put(const char *szKey, const char *szValue, bool bNull = false);
    bool put(const char *szKey, int64 lValue, bool bNull = false);
    bool put(const char *szKey, char cValue, bool bNull = false);
    bool put(const char *szKey, float fValue, bool bNull = false);
    bool put(const char *szKey, double dValue, bool bNull = false);
    bool put(const char *szKey, ValueType type, const struct tm &tmValue);
    bool put(const char *szKey, ValueType type, const void *pValue,
        bool bNull = false, int nSize = -1);
    bool put(const char *szKey, size_t nSize, const void *pValue,
        bool bNull = false);
    bool put(const char *szKey, ValueType type, trait<Lob>::sp pLob,
        bool bNull = false);
    bool put(const char *szKey, const _ExternelSource &source);
    bool put(const char *szKey, trait<sql::ResultSet>::sp pResultSet);
    bool put(int nValue, bool bNull = false);
    bool put(const char *szValue, bool bNull = false);
    bool put(int64 lValue, bool bNull = false);
    bool put(char cValue, bool bNull = false);
    bool put(float fValue, bool bNull = false);
    bool put(double dValue, bool bNull = false);
    bool put(ValueType type, const struct tm &tmValue);
    bool put(ValueType type, const void *pValue, bool bNull = false,
        int nSize = -1);
    bool put(size_t nSize, const void *pValue, bool bNull = false);
    bool put(ValueType type, trait<Lob>::sp pLob, bool bNull = false);
    bool put(const _ExternelSource &source);
    bool replace(size_t nIndex, ValueType type, void *pValue, bool bNull,
        int nSize);
    bool replace(size_t nIndex, const _ExternelSource &source);
    bool replace(size_t nIndex, trait<sql::ResultSet>::sp pResultSet);
    virtual void clear();

  public:
    const Value *getValue(const char *szKey) const;
    const Value *getValueWithoutException(const char *szKey) const;
    bool getInt(const char *szKey, int *pValue) const;
    bool getCString(const char *szKey, const char **pValue) const;
    bool getLong(const char *szKey, int64 *pValue) const;
    bool getChar(const char *szKey, char *pValue) const;
    bool getFloat(const char *szKey, float *pValue) const;
    bool getDouble(const char *szKey, double *pValue) const;
    bool getDateTime(const char *szKey, struct tm *pValue) const;
    bool getBytes(const char *szKey, size_t *pSize, const char **pValue) const;
    bool getType(const char *szKey, ValueType *pType) const;
    bool getLength(const char *szKey, int *pValue) const;
    trait<Lob>::sp getClob(const char *szKey) const;
    trait<Lob>::sp getBlob(const char *szKey) const;
    bool isNull(const char *szKey, bool *pNull) const;
    const Value *getValue(size_t nIndex) const;
    bool getInt(int nIndex, int *pValue) const;
    bool getCString(int nIndex, const char **pValue) const;
    bool getLong(int nIndex, int64 *pValue) const;
    bool getChar(int nIndex, char *pValue) const;
    bool getFloat(int nIndex, float *pValue) const;
    bool getDouble(int nIndex, double *pValue) const;
    bool getDateTime(int nIndex, struct tm *pValue) const;
    bool getBytes(int nIndex, size_t *pSize, const char **pValue) const;
    trait<Lob>::sp getClob(int nIndex) const;
    trait<Lob>::sp getBlob(int nIndex) const;
    trait<sql::ResultSet>::sp getResultSet(int nIndex) const;
    bool getType(int nIndex, ValueType *pType) const;
    bool getLength(int nIndex, int *pValue) const;
    bool isNull(int nIndex, bool *pNull) const;
    size_t size() const;

  public:
    _ValueSet &operator=(const _ValueSet &valueSet);
    Value *operator[](size_t nIndex);

  private:
    _ValueList m_valueList;
  };

  class _Parameter: public _ValueSet
  {
  public:
    _Parameter();
    virtual ~ _Parameter();

  public:
    const Value *getValue(size_t nIndex) const;
    const Value *getValue(const char *szKey, size_t nPosition) const;
  };

}

#endif
