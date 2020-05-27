/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation
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

import java.sql.SQLException;
import java.sql.Savepoint;

import cubrid.jdbc.jci.UConnection;

public class CUBRIDConnectionWrapperXA extends CUBRIDConnection {
	private CUBRIDXAConnection xacon;
	private boolean xa_started;

	protected CUBRIDConnectionWrapperXA(UConnection u, String r, String s,
			CUBRIDXAConnection c, boolean xa_start) {
		super(u, r, s);
		xacon = c;
		xa_started = xa_start;
		if (xa_start == true)
			auto_commit = false;
	}

	/*
	 * java.sql.Connection interface
	 */

	synchronized public void close() throws SQLException {
		if (is_closed)
			return;

		this.closeConnection();
		xacon.notifyConnectionClosed();
	}

	public synchronized void setAutoCommit(boolean autoCommit)
			throws SQLException {
		if (xa_started) {
			if (autoCommit == true) {
				throw new CUBRIDException(
						CUBRIDJDBCErrorCode.xa_illegal_operation);
			}
		} else {
			super.setAutoCommit(autoCommit);
		}
	}

	public synchronized void commit() throws SQLException {
		if (xa_started) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_illegal_operation);
		} else {
			super.commit();
		}
	}

	public synchronized void rollback() throws SQLException {
		if (xa_started) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_illegal_operation);
		} else {
			super.rollback();
		}
	}

	public synchronized void rollback(Savepoint savepoint) throws SQLException {
		if (xa_started) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_illegal_operation);
		} else {
			super.rollback(savepoint);
		}
	}

	public synchronized void releaseSavepoint(Savepoint savepoint)
			throws SQLException {
		if (xa_started) {
		} else {
			super.releaseSavepoint(savepoint);
		}
	}

	public synchronized Savepoint setSavepoint() throws SQLException {
		if (xa_started) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_illegal_operation);
		} else {
			return (super.setSavepoint());
		}
	}

	public synchronized Savepoint setSavepoint(String name) throws SQLException {
		if (xa_started) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_illegal_operation);
		} else {
			return (super.setSavepoint(name));
		}
	}

	protected void autoCommit() throws SQLException {
		if (xa_started == false) {
			super.autoCommit();
		}
	}

	void xa_start(UConnection u) {
		if (xa_started == true)
			return;

		auto_commit = false;
		xa_started = true;
		if (u != null) {
			u_con = u;
			u_con.setCUBRIDConnection(this);
		}
	}

	void xa_end(UConnection u) {
		if (xa_started == false)
			return;

		xa_started = false;
		if (u != null)
			u_con = u;
		auto_commit = true;
	}

}
