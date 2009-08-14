package com.cubrid.cubridmanager.core;

import java.io.IOException;
import java.net.URL;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;

import junit.framework.TestCase;

import org.eclipse.core.runtime.FileLocator;
import org.osgi.framework.Bundle;

import com.cubrid.cubridmanager.core.common.jdbc.JDBCConnectionManager;
import com.cubrid.cubridmanager.core.common.model.EnvInfo;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.ClientSocket;
import com.cubrid.cubridmanager.core.common.task.GetEnvInfoTask;
import com.cubrid.cubridmanager.core.common.task.MonitoringTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;

public abstract class SetupJDBCTestCase extends
		TestCase {
	// to store the latest token, use for sending message
	protected String token = null;
	// to store the host
	protected String host = SystemParameter.getParameterValue("host");
	//			protected String host = "10.34.63.221";
	// the monitor port in the host, use when login
	protected int monport = SystemParameter.getParameterIntValue("monport");
	// the job service port in the host, use when requesting job service
	protected int jpport = SystemParameter.getParameterIntValue("jpport");
	// the monitor socket, which must be kept alive during the whole session
	protected ClientSocket hostsocket = null;
	// the ServerInfo Object, use when initial a task
	protected ServerInfo site = new ServerInfo();
	//broker port
	protected String brokerPort = SystemParameter.getParameterValue("brokerPort");

	// define some variables for subtestcases
	// the login password, default is "1", for convenient
	protected String passwd = SystemParameter.getParameterValue("passwd");
	// the test database name, use when integration testing with a real database
	protected String dbname = SystemParameter.getParameterValue("dbname");
	protected String dbUser = SystemParameter.getParameterValue("dbUser");
	protected String dbPass = SystemParameter.getParameterValue("dbPass");
	protected String charset = SystemParameter.getParameterValue("charset");
	// the test class(table) name
	protected String classname = "";
	// cubrid test server path
	protected String serverPath = SystemParameter.getParameterValue("serverPath");
	protected DatabaseInfo databaseInfo = null;
	protected String port = SystemParameter.getParameterValue("port");

	/**
	 * set environment for subtestcases, providing the latest token, the
	 * ServerInfo object and so on
	 */
	protected void setUp() throws Exception {
		super.setUp();

		site.setHostAddress(host);
		site.setHostJSPort(jpport);
		site.setUserName("admin");
		site.setUserPassword(passwd);
		MonitoringTask monitoringTask = site.getMonitoringTask();
		site = monitoringTask.connectServer("8.2.0");

		GetEnvInfoTask env = new GetEnvInfoTask(site);
		EnvInfo envinfo = env.loadEnvInfo();
		serverPath = envinfo.getRootDir();

		databaseInfo = new DatabaseInfo(dbname, site);
		DbUserInfo userInfo = new DbUserInfo(databaseInfo.getDbName(), dbUser,
				"", dbPass, true);
		databaseInfo.setAuthLoginedDbUserInfo(userInfo);
	}

	@SuppressWarnings("unchecked")
	public Connection getConn() {
		//		String url = "jdbc:cubrid:localhost:30000:demodb:dba::";
		//
		//		Properties props = new Properties();
		//		props.put("user", "dba");
		//		props.put("password", "");
		//		String CUBRIDDRIVER = "cubrid.jdbc.driver.CUBRIDDriver";
		Connection conn = null;
		//		try {
		//			Class clazz = Class.forName(CUBRIDDRIVER);
		//			Driver driver = (Driver) clazz.newInstance();
		//			conn = driver.connect(url, props);
		//			return conn;
		//		} catch (Exception e) {
		//			return null;
		//		}
		try {
			conn = JDBCConnectionManager.getConnection(databaseInfo, site,
					null, port, null, false);
		} catch (Exception e) {

		}
		return conn;
	}

	/**
	 * execute DDL statement like "create table","drop table"
	 * 
	 * @param sql
	 * @return
	 * @throws SQLException
	 */
	public boolean executeDDL(String sql) {
		boolean success = false;
		Connection con = null;
		Statement stmt = null;
		try {
			con = this.getConn();
			con.setAutoCommit(true);
			stmt = con.createStatement();
			boolean isMultiResult = stmt.execute(sql);
			assert (isMultiResult == false);
			success = true;
			con.commit();
		} catch (Exception e) {
			try {
				System.out.println(e.getMessage());
				onException();
			} catch (Exception e2) {
			}
		} finally {
			try {
				stmt.close();
			} catch (Exception ee) {
			}
			try {
				con.close();
			} catch (Exception ee) {
			}
		}
		return success;
	}

	/**
	 * execute DML statement like "insert into ","delete from table"
	 * 
	 * @param sql
	 * @return
	 * @throws SQLException
	 */
	public int executeUpdate(String sql) {
		Connection con = null;
		Statement stmt = null;
		try {
			con = this.getConn();
			con.setAutoCommit(false);
			stmt = con.createStatement();
			int count = stmt.executeUpdate(sql);
			con.commit();
			return count;
		} catch (Exception e) {
			try {
				System.out.println(e.getMessage());
				onException();
			} catch (Exception e2) {
			}
		} finally {
			try {
				stmt.close();
			} catch (Exception ee) {
			}
			try {
				con.close();
			} catch (Exception ee) {
			}
		}
		return -1;
	}

	protected void onException() {

	}

	/**
	 * This method is to find the file's path in a fragment or a plugin.
	 * 
	 * @param filepath the file path in the fragment or a plugin
	 * @return the absolute file path
	 */
	public String getFilePathInPlugin(String filepath) {
		Bundle bundle = CubridManagerCorePlugin.getDefault().getBundle();
		URL url = bundle.getResource(filepath);
		URL fileUrl = null;
		try {
			fileUrl = FileLocator.toFileURL(url);
		} catch (IOException e) {
			return null;
		}
		return fileUrl.getPath();
	}
	
}
