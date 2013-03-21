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

#ifndef RESULTSET_H_
#define RESULTSET_H_

namespace dbgw
{

  namespace sql
  {

    class Statement;
    class ResultSetMetaData;

    class ResultSet : public _SynchronizedResource
    {
    public:
      ResultSet(trait<Statement>::sp pStatement);
      virtual ~ResultSet() {}

      void close();
      virtual bool isFirst() = 0;
      virtual bool first() = 0;
      virtual bool next() = 0;

    public:
      virtual int getRowCount() const = 0;
      virtual ValueType getType(int nIndex) const = 0;
      virtual int getInt(int nIndex) const = 0;
      virtual const char *getCString(int nIndex) const = 0;
      virtual int64 getLong(int nIndex) const = 0;
      virtual char getChar(int nIndex) const = 0;
      virtual float getFloat(int nIndex) const = 0;
      virtual double getDouble(int nIndex) const = 0;
      virtual struct tm getDateTime(int nIndex) const = 0;
      virtual void getBytes(int nIndex, size_t *pSize,
          const char **pValue) const = 0;
      virtual const Value *getValue(int nIndex) const = 0;
      virtual trait<Lob>::sp getClob(int nIndex) const = 0;
      virtual trait<Lob>::sp getBlob(int nIndex) const = 0;
      virtual trait<ResultSetMetaData>::sp getMetaData() const = 0;
      virtual _ValueSet &getInternalValuSet() = 0;

    protected:
      virtual void doClose() = 0;
      virtual void doUnlinkResource();

    private:
      bool m_bClosed;
      trait<Statement>::sp m_pStatement;
    };

  }

}

#endif
