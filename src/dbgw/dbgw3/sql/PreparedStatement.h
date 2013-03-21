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

#ifndef PREPAREDSTATEMENT_H_
#define PREPAREDSTATEMENT_H_

namespace dbgw
{

  namespace sql
  {

    class PreparedStatement : public Statement
    {
    public:
      PreparedStatement(trait<Connection>::sp pConnection);
      virtual ~ PreparedStatement();

      virtual void addBatch() = 0;
      virtual void clearBatch() = 0;
      virtual void clearParameters() = 0;

      virtual trait<ResultSet>::sp executeQuery() = 0;
      virtual int executeUpdate() = 0;
      virtual std::vector<int> executeBatch() = 0;

      virtual void setInt(int nIndex, int nValue) = 0;
      virtual void setLong(int nIndex, int64 lValue) = 0;
      virtual void setChar(int nIndex, char cValue) = 0;
      virtual void setCString(int nIndex, const char *szValue) = 0;
      virtual void setFloat(int nIndex, float fValue) = 0;
      virtual void setDouble(int nIndex, double dValue) = 0;
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

    private:
      /**
       * prepared statement doesn't use these methods.
       */
      virtual trait<ResultSet>::sp executeQuery(const char *szSql);
      virtual int executeUpdate(const char *szSql);
    };

  }

}

#endif
