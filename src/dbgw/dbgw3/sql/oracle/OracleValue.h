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

#ifndef ORACLEVALUE_H_
#define ORACLEVALUE_H_

namespace dbgw
{

  namespace sql
  {

    class _OracleResultSetMetaDataRaw;

    class _OracleValue
    {
    public:
      _OracleValue(_OracleContext *pContext, int nType, int nLength,
          int nIsNull);
      virtual ~_OracleValue();

      const _ExternelSource &getExternalSource(ValueType type);

    protected:
      bool isNull() const;
      void finalize();

    private:
      _OracleValue(const _OracleValue &);
      _OracleValue &operator=(const _OracleValue &);

    protected:
      _OracleContext *m_pContext;
      char *m_pBuffer;
      char *m_pConvertedBuffer;
      sb4 m_nLength;
      ub2 m_nType;
      sb2 m_nIsNull;
      ub2 m_nReadLen;
      _ExternelSource m_linkedValue;
    };

    class _OracleBind : public _OracleValue
    {
    public:
      _OracleBind(_OracleContext *pContext, OCIStmt *pOCIStmt,
          size_t nIndex, const Value *pValue, int nSize);
      virtual ~_OracleBind() {}

    public:
      ValueType getType() const;

    private:
      void bindInt(const Value *pValue);
      void bindString(const Value *pValue);
      void bindChar(const Value *pValue);
      void bindLong(const Value *pValue);
      void bindFloat(const Value *pValue);
      void bindDouble(const Value *pValue);
      void bindDateTime(const Value *pValue);
      void bindDate(const Value *pValue);
      void bindTime(const Value *pValue);
      void bindBytes(const Value *pValue);
      void bindClob(const Value *pValue);
      void bindBlob(const Value *pValue);
      void bindResultSet(const Value *pValue);

    private:
      OCIBind *m_pOCIBind;
      ValueType m_dbgwType;
      int m_nReservedSize;
    };

    class _OracleDefine : public _OracleValue
    {
    public:
      _OracleDefine(_OracleContext *pContext, OCIStmt *pOCIStmt,
          size_t nIndex, const _OracleResultSetMetaDataRaw &md);
      virtual ~_OracleDefine() {}

    private:
      _OracleContext *m_pContext;
      _OracleDesciptor m_ociDesc;
      OCIDefine *m_pOCIDefine;
    };

  }

}

#endif
