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

#ifndef CONNECTION_H_
#define CONNECTION_H_

namespace dbgw
{

  class Lob;

  namespace sql
  {

    class CallableStatement;
    class PreparedStatement;

    class Connection : public boost::enable_shared_from_this<Connection>,
      public _SynchronizedResourceSubject
    {
    public:
      Connection();
      virtual ~ Connection();

      void connect();
      void close();

      virtual trait<CallableStatement>::sp prepareCall(
          const char *szSql) = 0;
      virtual trait<PreparedStatement>::sp prepareStatement(
          const char *szSql) = 0;

      void setTransactionIsolation(TransactionIsolarion isolation);
      void setAutoCommit(bool bAutoCommit);
      void commit();
      void rollback();
      virtual void cancel() = 0;
      virtual trait<Lob>::sp createClob() = 0;
      virtual trait<Lob>::sp createBlob() = 0;

    public:
      bool getAutoCommit() const;
      TransactionIsolarion getTransactionIsolation() const;
      bool isClosed() const;
      virtual void *getNativeHandle() const = 0;

    protected:
      virtual void doConnect() = 0;
      virtual void doClose() = 0;
      virtual void doSetTransactionIsolation(TransactionIsolarion isolation) = 0;
      virtual void doSetAutoCommit(bool bAutoCommit) = 0;
      virtual void doCommit() = 0;
      virtual void doRollback() = 0;

    private:
      bool m_bConnected;
      bool m_bClosed;
      bool m_bAutoCommit;
      TransactionIsolarion m_isolation;
    };

  }

}

#endif
