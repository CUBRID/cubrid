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

import java.io.PrintWriter;
import java.net.InetAddress;
import java.sql.SQLException;

import javax.naming.Reference;
import javax.naming.StringRefAddr;

public class CUBRIDDataSourceBase {
	// DataSource Standard Properties
	private String databaseName;
	private String dataSourceName;
	private String description;
	private String networkProtocol;
	private String password;
	private int portNumber;
	private String roleName;
	private String serverName;
	private String user;
	private String url;

	private int loginTimeout;
	private PrintWriter logWriter;

	private String dataSourceID;

	protected CUBRIDDataSourceBase() {
		databaseName = null;
		dataSourceName = null;
		description = null;
		networkProtocol = null;
		password = null;
		portNumber = 0;
		roleName = null;
		serverName = null;
		user = null;
		url = null;

		loginTimeout = 0;
		logWriter = null;

		dataSourceID = null;
	}

	/*
	 * javax.sql.DataSource, javax.sql.ConnectionPoolDataSource,
	 * javax.sql.XADataSource interface
	 */

	public PrintWriter getLogWriter() throws SQLException {
		return logWriter;
	}

	public void setLogWriter(PrintWriter out) throws SQLException {
		logWriter = out;
	}

	public void setLoginTimeout(int seconds) throws SQLException {
		loginTimeout = seconds;
	}

	public int getLoginTimeout() throws SQLException {
		return loginTimeout;
	}

	public String getDatabaseName() {
		return databaseName;
	}

	public String getDataSourceName() {
		return dataSourceName;
	}

	public String getDescription() {
		return description;
	}

	public String getNetworkProtocol() {
		return networkProtocol;
	}

	public String getPassword() {
		return password;
	}

	public int getPortNumber() {
		return portNumber;
	}

	public int getPort() {
		return getPortNumber();
	}

	public String getRoleName() {
		return roleName;
	}

	public String getServerName() {
		return serverName;
	}

	public String getUser() {
		return user;
	}

	public String getUrl() {
		return url;
	}

	public String getURL() {
		return getUrl();
	}

	public void setDatabaseName(String dbName) {
		databaseName = dbName;
	}

	public void setDataSourceName(String dsName) {
		dataSourceName = dsName;
	}

	public void setDescription(String desc) {
		description = desc;
	}

	public void setNetworkProtocol(String netProtocol) {
		networkProtocol = netProtocol;
	}

	public void setPassword(String psswd) {
		password = psswd;
	}

	public void setPortNumber(int p) {
		portNumber = p;
	}

	void setPort(int p) {
		setPortNumber(p);
	}

	public void setRoleName(String rName) {
		roleName = rName;
	}

	public void setServerName(String svName) {
		serverName = svName;
	}

	public void setUser(String uName) {
		user = uName;
	}

	public void setUrl(String urlString) {
		url = urlString;
	}

	public void setURL(String urlString) {
		setUrl(urlString);
	}

	synchronized String getDataSourceID(String username) {
		if (username == null) {
			username = "";
		}
		if (dataSourceID == null) {
			if (url != null) {
				dataSourceID = url;
			} else {
				String host;
				String hostName = ((serverName != null) ? serverName : "");
				try {
					host = InetAddress
							.getByName(hostName)
							.getHostAddress();
				} catch (Exception e) {
					host = hostName;
				}
				dataSourceID = "jdbc:cubrid:" + host + ":"
						+ portNumber + ":"
						+ ((databaseName != null) ? databaseName : "");
			}
		}
		return (dataSourceID + ":" + username);
	}

	protected Reference getProperties(Reference ref) {
		ref.add(new StringRefAddr("serverName", getServerName()));
		ref.add(new StringRefAddr("databaseName", getDatabaseName()));
		ref.add(new StringRefAddr("portNumber", Integer
				.toString(getPortNumber())));
		ref.add(new StringRefAddr("url", getUrl()));
		ref.add(new StringRefAddr("dataSourceName", getDataSourceName()));
		ref.add(new StringRefAddr("description", getDescription()));
		ref.add(new StringRefAddr("networkProtocol",
				getNetworkProtocol()));
		ref.add(new StringRefAddr("password", getPassword()));
		ref.add(new StringRefAddr("roleName", getRoleName()));
		ref.add(new StringRefAddr("user", getUser()));

		return ref;
	}

	protected void setProperties(Reference ref) {
		setServerName((String) ref.get("serverName").getContent());
		setDatabaseName((String) ref.get("databaseName").getContent());
		setPortNumber(Integer.parseInt((String) ref.get("portNumber")
				.getContent()));
		setUrl((String) ref.get("url").getContent());
		setDataSourceName((String) ref.get("dataSourceName")
				.getContent());
		setDescription((String) ref.get("description").getContent());
		setNetworkProtocol((String) ref.get("networkProtocol")
				.getContent());
		setPassword((String) ref.get("password").getContent());
		setRoleName((String) ref.get("roleName").getContent());
		setUser((String) ref.get("user").getContent());
	}

	protected void writeLog(String log) {
		if (logWriter != null) {
			java.util.Date dt = new java.util.Date(
					System.currentTimeMillis());
			logWriter.println("[" + dt + "] " + log);
			logWriter.flush();
		}
	}
}
