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

#ifndef STATEMENT_H_
#define STATEMENT_H_

namespace dbgw
{

  namespace sql
  {

    class Connection;
    class ResultSet;

    typedef std::vector<sql::StatementType> _QueryTypeList;

    class Statement : public boost::enable_shared_from_this<Statement>,
      public _SynchronizedResourceSubject, public _SynchronizedResource
    {
    public:
      Statement(trait<Connection>::sp pConnection);
      virtual ~Statement();

      void close();

      virtual trait<ResultSet>::sp executeQuery(const char *szSql) = 0;
      virtual int executeUpdate(const char *szSql) = 0;
      virtual std::vector<int> executeBatch() = 0;

    public:
      trait<Connection>::sp getConnection() const;
      virtual void *getNativeHandle() const = 0;

    protected:
      virtual void doClose() = 0;
      virtual void doUnlinkResource();

    private:
      bool m_bClosed;
      trait<Connection>::sp m_pConnection;
    };

  }

}

#endif
