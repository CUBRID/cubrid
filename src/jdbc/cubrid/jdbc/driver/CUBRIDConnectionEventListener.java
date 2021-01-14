/*
 * Copyright (C) 2008 Search Solution Corporation. 
 * Copyright (c) 2016 CUBRID Corporation.
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
import java.util.Vector;

import javax.sql.ConnectionEvent;
import javax.sql.ConnectionEventListener;
import javax.sql.PooledConnection;

class CUBRIDConnectionEventListener implements ConnectionEventListener {
	private Vector<PooledConnection> availableConnections;
	private CUBRIDConnectionPoolDataSource cpds;

	CUBRIDConnectionEventListener(CUBRIDConnectionPoolDataSource ds) {
		availableConnections = new Vector<PooledConnection>();
		cpds = ds;
	}

	/*
	 * javax.sql.ConnectionEventListener interface
	 */

	synchronized public void connectionClosed(ConnectionEvent event) {
		PooledConnection pc = (PooledConnection) event.getSource();

		if (pc == null) {
			return;
		}

		availableConnections.add(pc);
	}

	public void connectionErrorOccurred(ConnectionEvent event) {
		PooledConnection pc = (PooledConnection) event.getSource();

		if (pc == null)
			return;

		try {
			pc.close();
		} catch (Exception e) {
		}
	}

	synchronized Connection getConnection(String user, String passwd)
			throws SQLException {
		PooledConnection pc;

		if (availableConnections.size() <= 0) {
			pc = cpds.getPooledConnection(user, passwd);
			pc.addConnectionEventListener(this);
			availableConnections.add(pc);
		}

		pc = (PooledConnection) availableConnections.remove(0);

		return (pc.getConnection());
	}

}
