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

#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/SynchronizedResource.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/sql/Connection.h"

namespace dbgw
{

  namespace sql
  {

    Connection::Connection() :
      m_bConnected(false), m_bClosed(false), m_bAutoCommit(true),
      m_isolation(DBGW_TRAN_UNKNOWN)
    {
    }

    Connection::~ Connection()
    {
    }

    void Connection::connect()
    {
      if (m_bConnected)
        {
          return;
        }

      doConnect();

      m_bConnected = true;
    }

    void Connection::close()
    {
      if (m_bConnected == false || m_bClosed)
        {
          return;
        }

      m_bClosed = true;

      unregisterResourceAll();

      doClose();
    }

    void Connection::setTransactionIsolation(TransactionIsolation isolation)
    {
      doSetTransactionIsolation(isolation);

      m_isolation = isolation;
    }

    void Connection::setAutoCommit(bool bAutoCommit)
    {
      if (m_bAutoCommit == bAutoCommit && bAutoCommit == false)
        {
          AlreadyInTransactionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doSetAutoCommit(bAutoCommit);

      m_bAutoCommit = bAutoCommit;
    }

    void Connection::commit()
    {
      if (m_bAutoCommit)
        {
          NotInTransactionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doCommit();
    }

    void Connection::rollback()
    {
      if (m_bAutoCommit)
        {
          NotInTransactionException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      doRollback();
    }

    void Connection::setContainerKey(const char *szKey)
    {
    }

    bool Connection::getAutoCommit() const
    {
      return m_bAutoCommit;
    }

    TransactionIsolation Connection::getTransactionIsolation() const
    {
      return m_isolation;
    }

    bool Connection::isClosed() const
    {
      return m_bClosed;
    }

  }

}
