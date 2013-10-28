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

#ifndef CALLABLESTATEMENT_H_
#define CALLABLESTATEMENT_H_

namespace dbgw
{

  namespace sql
  {

    class CallableStatement : public PreparedStatement
    {
    public:
      CallableStatement(trait<Connection>::sp pConnection);
      virtual ~CallableStatement() {}

      virtual trait<ResultSet>::sp executeQuery() = 0;
      virtual int executeUpdate() = 0;

      virtual void registerOutParameter(size_t nIndex, ValueType type,
          size_t nSize = 0) = 0;

      virtual void setInt(int nIndex, int nValue) = 0;
      virtual void setLong(int nIndex, int64 lValue) = 0;
      virtual void setChar(int nIndex, char cValue) = 0;
      virtual void setCString(int nIndex, const char *szValue) = 0;
      virtual void setFloat(int nIndex, float fValue) = 0;
      virtual void setDouble(int nIndex, double dValue) = 0;
      virtual void setBool(int nIndex, bool bValue) = 0;
      virtual void setDate(int nIndex, const char *szValue) = 0;
      virtual void setDate(int nIndex, const struct tm &tmValue) = 0;
      virtual void setTime(int nIndex, const char *szValue) = 0;
      virtual void setTime(int nIndex, const struct tm &tmValue) = 0;
      virtual void setDateTime(int nIndex, const char *szValue) = 0;
      virtual void setDateTime(int nIndex, const struct tm &tmValue) = 0;
      virtual void setBytes(int nIndex, size_t nSize, const void *pValue) = 0;
      virtual void setNull(int nIndex, ValueType type) = 0;
      virtual void setClob(int nIndex, trait<Lob>::sp pLob) = 0;
      virtual void setBlob(int nIndex, trait<Lob>::sp pLob) = 0;

    public:
      virtual ValueType getType(int nIndex) const = 0;
      virtual int getInt(int nIndex) const = 0;
      virtual const char *getCString(int nIndex) const = 0;
      virtual int64 getLong(int nIndex) const = 0;
      virtual char getChar(int nIndex) const = 0;
      virtual float getFloat(int nIndex) const = 0;
      virtual double getDouble(int nIndex) const = 0;
      virtual bool getBool(int nIndex) const = 0;
      virtual struct tm getDateTime(int nIndex) const = 0;
      virtual void getBytes(int nIndex, size_t *pSize,
          const char **pValue) const = 0;
      virtual trait<Lob>::sp getClob(int nIndex) const = 0;
      virtual trait<Lob>::sp getBlob(int nIndex) const = 0;
      virtual trait<ResultSet>::sp getResultSet(int nIndex) const = 0;
      virtual const Value *getValue(int nIndex) const = 0;
      virtual _ValueSet &getInternalValuSet() = 0;
    };

  }

}

#endif
