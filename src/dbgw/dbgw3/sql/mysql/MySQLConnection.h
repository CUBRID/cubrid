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

#ifndef MYSQLCONNECTION_H_
#define MYSQLCONNECTION_H_

namespace dbgw
{

  namespace sql
  {

    class MySQLConnection : public Connection
    {
    public:
      MySQLConnection(const char *szUrl, const char *szUser,
          const char *szPassword);
      virtual ~MySQLConnection();

      virtual trait<CallableStatement>::sp prepareCall(
          const char *szSql);
      virtual trait<PreparedStatement>::sp prepareStatement(
          const char *szSql);
      virtual void cancel();
      virtual trait<Lob>::sp createClob();
      virtual trait<Lob>::sp createBlob();

    public:
      virtual void *getNativeHandle() const;

    protected:
      virtual void doConnect();
      virtual void doClose();
      virtual void doSetTransactionIsolation(TransactionIsolarion isolation);
      virtual void doSetAutoCommit(bool bAutoCommit);
      virtual void doCommit();
      virtual void doRollback();

    private:
      MySQLConnection(const MySQLConnection &);
      MySQLConnection &operator=(const MySQLConnection &);

    private:
      class Impl;
      Impl *m_pImpl;
    };

  }

}

#endif
