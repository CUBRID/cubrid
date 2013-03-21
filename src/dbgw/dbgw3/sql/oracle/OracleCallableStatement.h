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

#ifndef ORACLECALLABLESTATEMENT_H_
#define ORACLECALLABLESTATEMENT_H_

namespace dbgw
{

  namespace sql
  {

    class OracleCallableStatement : public CallableStatement
    {
    public:
      OracleCallableStatement(trait<Connection>::sp pConnection,
          const char *szSql);
      virtual ~ OracleCallableStatement();

      virtual void addBatch();
      virtual void clearBatch();
      virtual void clearParameters();

      virtual trait<ResultSet>::sp executeQuery();
      virtual std::vector<int> executeBatch();
      virtual int executeUpdate();

      virtual void registerOutParameter(size_t nIndex, ValueType type,
          size_t nSize = 0);

      virtual void setInt(int nIndex, int nValue);
      virtual void setLong(int nIndex, int64 lValue);
      virtual void setChar(int nIndex, char cValue);
      virtual void setCString(int nIndex, const char *szValue);
      virtual void setFloat(int nIndex, float fValue);
      virtual void setDouble(int nIndex, double dValue);
      virtual void setDate(int nIndex, const char *szValue);
      virtual void setDate(int nIndex, const struct tm &tmValue);
      virtual void setTime(int nIndex, const char *szValue);
      virtual void setTime(int nIndex, const struct tm &tmValue);
      virtual void setDateTime(int nIndex, const char *szValue);
      virtual void setDateTime(int nIndex, const struct tm &tmValue);
      virtual void setBytes(int nIndex, size_t nSize, const void *pValue);
      virtual void setNull(int nIndex, ValueType type);
      virtual void setClob(int nIndex, trait<Lob>::sp pLob);
      virtual void setBlob(int nIndex, trait<Lob>::sp pLob);

    public:
      virtual ValueType getType(int nIndex) const;
      virtual int getInt(int nIndex) const;
      virtual const char *getCString(int nIndex) const;
      virtual int64 getLong(int nIndex) const;
      virtual char getChar(int nIndex) const;
      virtual float getFloat(int nIndex) const;
      virtual double getDouble(int nIndex) const;
      virtual struct tm getDateTime(int nIndex) const;
      virtual void getBytes(int nIndex, size_t *pSize,
          const char **pValue) const;
      virtual trait<Lob>::sp getClob(int nIndex) const;
      virtual trait<Lob>::sp getBlob(int nIndex) const;
      virtual const Value *getValue(int nIndex) const;
      virtual _ValueSet &getInternalValuSet();

    public:
      virtual void *getNativeHandle() const;

    protected:
      virtual void doClose();

    private:
      _OracleStatementBase m_stmtBase;
      _ValueSet m_resultSet;
    };

  }

}

#endif
