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
import java.util.Properties;
import java.util.logging.Logger;

import javax.naming.NamingException;
import javax.naming.Reference;

import cubrid.jdbc.jci.UConnection;
import cubrid.jdbc.jci.UJCIManager;

/**
 * Title: CUBRID JDBC Driver Description:
 * 
 * @version 3.0
 */

public class CUBRIDDataSource extends CUBRIDDataSourceBase implements
		javax.sql.DataSource, javax.naming.Referenceable,
		java.io.Serializable {
	private static final long serialVersionUID = -1038542340147556509L;

	public CUBRIDDataSource() {
		super();
	}

	protected CUBRIDDataSource(Reference ref) {
		super();
		setProperties(ref);
	}

	/*
	 * javax.sql.DataSource interface
	 */

	public Connection getConnection() throws SQLException {
		return getConnection(null, null);
	}

	public Connection getConnection(String username, String passwd)
			throws SQLException {
		String dataSourceName = getDataSourceName();
		Connection con;

		if (dataSourceName == null || dataSourceName.length() == 0) {
			if (getUrl() != null) {
				CUBRIDDriver driver = new CUBRIDDriver();
				Properties props = new Properties();

				if (username != null) {
					props.setProperty("user", username);
				}
				if (passwd != null) {
					props.setProperty("password", passwd);
				}
				con = driver.connect(getUrl(), props);
			} else {
				if (username == null) {
					username = getUser();
				}
				if (passwd == null) {
					passwd = getPassword();
				}
				UConnection u_con = UJCIManager.connect(
						getServerName(),
						getPortNumber(),
						getDatabaseName(), username,
						passwd,
						getDataSourceID(username));
				con = new CUBRIDConnection(u_con, null,
						username);
			}
			writeLog("getConnection(" + username + ")");
		} else {
			CUBRIDConnectionPoolDataSource cpds;

			cpds = CUBRIDConnectionPoolManager
					.getConnectionPoolDataSource(dataSourceName);
			if (username == null)
				username = cpds.getUser();
			if (passwd == null)
				passwd = cpds.getPassword();
			con = CUBRIDConnectionPoolManager.getConnection(cpds,
					username, passwd);
		}

		return con;
	}

	/*
	 * javax.naming.Referenceable interface
	 */

	public synchronized Reference getReference() throws NamingException {
		Reference ref = new Reference(
				this.getClass().getName(),
				"cubrid.jdbc.driver.CUBRIDDataSourceObjectFactory",
				null);

		ref = getProperties(ref);
		writeLog("Bind DataSource");
		return ref;
	}

	/* JDK 1.6 */
	public boolean isWrapperFor(Class<?> iface) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public <T> T unwrap(Class<T> iface) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.7 */
	public Logger getParentLogger() {
		throw new java.lang.UnsupportedOperationException();
	}
}
