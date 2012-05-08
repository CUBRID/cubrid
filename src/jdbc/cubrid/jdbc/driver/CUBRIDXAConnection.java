/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

package cubrid.jdbc.driver;

import java.sql.Connection;
import java.sql.SQLException;

import javax.sql.XAConnection;
import javax.transaction.xa.XAResource;

import cubrid.jdbc.jci.UConnection;
import cubrid.jdbc.jci.UJCIManager;

/**
 * Title: CUBRID JDBC Driver Description:
 * 
 * @version 3.0
 */

public class CUBRIDXAConnection extends CUBRIDPooledConnection implements
		XAConnection {
	private String serverName;
	private int portNumber;
	private String databaseName;
	private String username;
	private String passwd;

	private CUBRIDXAResource xares;

	private boolean xa_started;
	private String xacon_key;

	protected CUBRIDXAConnection(CUBRIDXADataSource xads, String serverName,
			int portNumber, String databaseName, String username, String passwd)
			throws SQLException {
		super(null);
		this.serverName = serverName;
		this.portNumber = portNumber;
		this.databaseName = databaseName;
		this.username = username;
		this.passwd = passwd;

		u_con = createUConnection();

		xares = null;

		xa_started = false;
		xacon_key = xads.getDataSourceID(username);
	}

	/*
	 * javax.sql.XAConnection interface
	 */

	synchronized public XAResource getXAResource() throws SQLException {
		if (isClosed) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_connection_closed);
		}

		if (xares == null) {
			xares = new CUBRIDXAResource(this, xacon_key);
		}

		return xares;
	}

	synchronized public Connection getConnection() throws SQLException {
		if (isClosed) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_connection_closed);
		}

		if (curConnection != null)
			curConnection.closeConnection();

		if (u_con == null) {
			u_con = createUConnection();
		}

		curConnection = new CUBRIDConnectionWrapperXA(u_con, null, null, this,
				xa_started);
		return curConnection;
	}

	synchronized void notifyConnectionClosed() {
		super.notifyConnectionClosed();

		if (xa_started == true)
			u_con = null;
	}

	synchronized UConnection xa_end_tran(UConnection u) {
		if (u_con == null) {
			u_con = u;
			return null;
		}
		return u;
	}

	synchronized UConnection xa_start(int flag, UConnection u) {
		if (xa_started == true)
			return null;

		xa_started = true;

		if (flag == XAResource.TMJOIN || flag == XAResource.TMRESUME) {
			if (u_con != null) {
				u_con.close();
			}
			u_con = u;
		}

		if (curConnection != null) {
			if (flag == XAResource.TMNOFLAGS) {
				try {
					curConnection.rollback();
				} catch (SQLException e) {
				}
			}
			((CUBRIDConnectionWrapperXA) curConnection).xa_start(u_con);
		}

		return u_con;
	}

	synchronized boolean xa_end() {
		if (xa_started == false)
			return true;

		try {
			if (u_con != null) {
				u_con = createUConnection();
			}
		} catch (SQLException e) {
			return false;
		}

		if (curConnection != null)
			((CUBRIDConnectionWrapperXA) curConnection).xa_end(u_con);

		xa_started = false;

		return true;
	}

	UConnection createUConnection() throws SQLException {
		return (UJCIManager.connect(serverName, portNumber, databaseName,
				username, passwd, "xa"));
	}
}
