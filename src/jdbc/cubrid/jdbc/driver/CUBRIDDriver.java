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

import java.io.File;
import java.io.FileNotFoundException;
import java.io.PrintStream;
import java.sql.Connection;
import java.sql.Driver;
import java.sql.DriverManager;
import java.sql.DriverPropertyInfo;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Hashtable;
import java.util.Properties;
import java.util.StringTokenizer;
import java.util.logging.Logger;

import cubrid.jdbc.jci.UConnection;
import cubrid.jdbc.jci.UJCIManager;
import cubrid.jdbc.jci.UJCIUtil;

/**
 * Title: CUBRID JDBC Driver Description:
 * 
 * @version 2.0
 */

public class CUBRIDDriver implements Driver {
	// version
	static final String version_string = "@JDBC_DRIVER_VERSION_STRING@";
	public static final int major_version;
	public static final int minor_version;
	public static final int patch_version;
	static {
		StringTokenizer st = new StringTokenizer(version_string, ".");
		if (st.countTokens() != 4) {
			throw new RuntimeException("Could not parse version_string: "
					+ version_string);
		}
		major_version = Integer.parseInt(st.nextToken());
		minor_version = Integer.parseInt(st.nextToken());
		patch_version = Integer.parseInt(st.nextToken());
	}

	// default connection informations
	public static final String default_hostname = "localhost";
	public static final int default_port = 30000;
	public static final String default_user = "public";
	public static final String default_password = "";

	private final static String CUBRID_JDBC_URL_HEADER = "jdbc:cubrid";
	private final static String JDBC_DEFAULT_CONNECTION = "jdbc:default:connection";

	static {
		try {
			DriverManager.registerDriver(new CUBRIDDriver());
		} catch (SQLException e) {
		}
	}

	private static PrintStream debugOutput;
	private static Hashtable<String, String> connInfoTable;
	static {
		if (UJCIUtil.isConsoleDebug()) {
			try {
				debugOutput = new PrintStream(new File("cubrid.log"));
			} catch (FileNotFoundException e) {
				debugOutput = System.out;
			}
		}
		connInfoTable = new Hashtable<String, String>();
	}

	public static void printDebug(String msg) {
		Timestamp timestamp = new Timestamp(System.currentTimeMillis());
		SimpleDateFormat fmt = new SimpleDateFormat("MM-dd hh:mm:ss.SSS");

		String line = String.format("%s %s", fmt.format(timestamp), msg);
		debugOutput.println(line);
	}

	public static void setLastConnectInfo(String url, String info) {
		if (url != null) {
			connInfoTable.put(url, info);

			if (UJCIUtil.isConsoleDebug()) {
				printDebug(String.format("S[K,V]=(%s,%s)", url, info));
			}
		}
	}

	public static String getLastConnectInfo(String url) {
		String info = connInfoTable.get(url);
		if (UJCIUtil.isConsoleDebug()) {
			printDebug(String.format("G[K,V]=(%s,%s)", url, info));
		}
		return info;
	}

	/*
	 * java.sql.Driver interface
	 */

	public Connection connect(String url, Properties info) throws SQLException {
		String magickey, hostname, db_name, dummy, conn_string, prop_string;
		String user = null, passwd = null;
		int prop_pos = 0;
		int port;
		UConnection u_con;
		String resolvedUrl;
		ConnectionProperties connProperties;

		if (!acceptsURL(url))
			throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_url, url);

		if (url.toLowerCase().startsWith(JDBC_DEFAULT_CONNECTION)) {
			return defaultConnection();
		} else {
			// parse url
			try {
				prop_pos = url.indexOf('?');

				if (prop_pos != -1) {
					conn_string = url.substring(0, prop_pos);
					prop_string = url.substring(prop_pos, url.length());
				} else {
					conn_string = url;
					prop_string = null;
				}

				StringTokenizer tokenizer = new StringTokenizer(conn_string,
						":", true);
				dummy = tokenizer.nextToken();
				if (dummy.equals(":")) {
					throw new Exception("Invalid URL");
				} else {
					tokenizer.nextToken();
				}

				magickey = tokenizer.nextToken();
				if (magickey.equals(":")) {
					throw new Exception("Invalid URL");
				} else {
					tokenizer.nextToken();
				}

				hostname = tokenizer.nextToken();
				if (hostname.equals(":")) {
					hostname = default_hostname;
				} else {
					tokenizer.nextToken();
				}

				dummy = tokenizer.nextToken();
				if (dummy.equals(":")) {
					port = default_port;
				} else {
					port = Integer.parseInt(dummy);
					tokenizer.nextToken();
				}

				db_name = tokenizer.nextToken();
				if (db_name.equals(":")) {
					throw new CUBRIDException(CUBRIDJDBCErrorCode.no_dbname);
				}

				/*
				 * Both user and password are optional. Test if there are more
				 * tokens available to prevent NoSuchElementException.
				 */
				if (tokenizer.hasMoreTokens()) {
					/* skip ':' */
					tokenizer.nextToken();
					if (tokenizer.hasMoreTokens()) {
						user = tokenizer.nextToken();
						if (user.equals(":")) {
							user = null;
						}
					}
				}
				if (tokenizer.hasMoreTokens()) {
					/* skip ':' */
					tokenizer.nextToken();
					if (tokenizer.hasMoreTokens()) {
						passwd = tokenizer.nextToken();
						if (passwd.equals(":")) {
							passwd = null;
						}
					}
				}
			} catch (CUBRIDException e) {
				throw e;
			} catch (Exception e) {
				throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_url, url);
			}

			connProperties = new ConnectionProperties();
			connProperties.setProperties(prop_string);

			// getting informations from the Properties object
			dummy = info.getProperty("user");
			if (dummy != null) {
				user = dummy;
			}
			dummy = info.getProperty("password");
			if (dummy != null) {
				passwd = dummy;
			}

			if (user == null) {
				user = default_user;
			}
			if (passwd == null) {
				passwd = default_password;
			}

			resolvedUrl = "jdbc:cubrid:" + hostname + ":" + port + ":"
					+ db_name + ":" + user + "::";
			if (prop_string != null) {
				resolvedUrl += "?" + prop_string;
			}

			connProperties.setProperties(info);

			dummy = connProperties.getAltHosts();
			if (dummy != null) {
				ArrayList<String> altHostList = new ArrayList<String>();
				altHostList.add(hostname + ":" + port);

				StringTokenizer st = new StringTokenizer(dummy, ",", false);
				while (st.hasMoreTokens()) {
					altHostList.add(st.nextToken());
				}
				try {
					u_con = UJCIManager.connect(altHostList, db_name, user,
							passwd, resolvedUrl);
				} catch (CUBRIDException e) {
					throw e;
				}
			} else {
				try {
					u_con = UJCIManager.connect(hostname, port, db_name, user,
							passwd, resolvedUrl);
				} catch (CUBRIDException e) {
					throw e;
				}
			}

		    	u_con.setCharset(connProperties.getCharSet());
			u_con.setZeroDateTimeBehavior(connProperties.getZeroDateTimeBehavior());
		}

		u_con.setConnectionProperties(connProperties);
		u_con.tryConnect();
		return new CUBRIDConnection(u_con, url, user);
	}

	public Connection defaultConnection() throws SQLException {
		if (UJCIUtil.isServerSide()) {
			Thread t = Thread.currentThread();
			Connection c = (Connection) UJCIUtil.invoke(
					"com.cubrid.jsp.ExecuteThread", "getJdbcConnection", null,
					t, null);
			if (c != null) {
				return c;
			}

			UConnection u_con = UJCIManager.connectDefault();
			CUBRIDConnection con = new CUBRIDConnection(u_con,
					"jdbc:default:connection:", "default", true);
			UJCIUtil.invoke("com.cubrid.jsp.ExecuteThread",
					"setJdbcConnection", new Class[] { Connection.class }, t,
					new Object[] { con });
			return con;
		} else {
			return null;
		}
	}

	public boolean acceptsURL(String url) throws SQLException {
	    if (url == null)
            return false;

        String urlHeader = CUBRID_JDBC_URL_HEADER;
        String className = CUBRIDDriver.class.getName();
        if (className.matches(".*oracle.*")) {
            urlHeader += "-oracle";
        } else if (className.matches(".*mysql.*")) {
            urlHeader += "-mysql";
        }
        
        return url.toLowerCase().startsWith(urlHeader)
                || url.toLowerCase().startsWith(JDBC_DEFAULT_CONNECTION);
	}

	public DriverPropertyInfo[] getPropertyInfo(String url, Properties info)
			throws SQLException {
		return new DriverPropertyInfo[0];
	}

	public int getMajorVersion() {
		return major_version;
	}

	public int getMinorVersion() {
		return minor_version;
	}

	public boolean jdbcCompliant() {
		return true;
	}

	/* JDK 1.7 */
	public Logger getParentLogger() {
		throw new java.lang.UnsupportedOperationException();
	}
}
