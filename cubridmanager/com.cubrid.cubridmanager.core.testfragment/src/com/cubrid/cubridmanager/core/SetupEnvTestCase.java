package com.cubrid.cubridmanager.core;

import java.io.IOException;
import java.net.URL;
import java.util.List;

import junit.framework.TestCase;

import org.eclipse.core.runtime.FileLocator;
import org.osgi.framework.Bundle;

import com.cubrid.cubridmanager.core.common.ServerManager;
import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.common.model.EnvInfo;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.ClientSocket;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.common.task.CommonTaskName;
import com.cubrid.cubridmanager.core.common.task.CommonUpdateTask;
import com.cubrid.cubridmanager.core.common.task.GetEnvInfoTask;
import com.cubrid.cubridmanager.core.common.task.MonitoringTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.database.task.GetDatabaseListTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.OsInfoType;

public abstract class SetupEnvTestCase extends TestCase {
	// to store the latest token, use for sending message
	protected String token = null;
	// to store the host
	// protected String host = "localhost";
	protected String host = SystemParameter.getParameterValue("host");

	// the monitor port in the host, use when login
	protected int monport = SystemParameter.getParameterIntValue("monport");
	// the job service port in the host, use when requesting job service
	protected int jpport = SystemParameter.getParameterIntValue("jpport");
	// the monitor socket, which must be kept alive during the whole session
	protected ClientSocket hostsocket = null;
	// the ServerInfo Object, use when initial a task
	protected static ServerInfo site = null;

	protected String clientVersion = SystemParameter.getParameterValue("clientVersion");

	// define some variables for subtestcases
	// the login password, default is "1", for convenient
	protected String passwd = SystemParameter.getParameterValue("passwd");
	// the test database name, use when integration testing with a real database
	protected String dbname = SystemParameter.getParameterValue("dbname");
	// the test class(table) name
	protected String classname = "";
	// cubrid test server path
	protected String serverPath = SystemParameter.getParameterValue("serverPath");
	protected static EnvInfo envInfo = null;

	// protected ServerInfo serverInfo;
	// protected List<DatabaseInfo> databaseInfoList;
	/**
	 * set environment for subtestcases, providing the latest token, the
	 * ServerInfo object and so on
	 */
	protected void setUp() throws Exception {
		super.setUp();
		if (site == null) {
			site = new ServerInfo();
			site.setHostAddress(host);
			site.setHostMonPort(monport);
			site.setHostJSPort(jpport);
			site.setUserName("admin");
			site.setUserPassword(passwd);
			ServerManager.getInstance().addServer(host, monport, site);

			MonitoringTask monTask = new MonitoringTask(site);
			site = monTask.connectServer(clientVersion);

			final GetDatabaseListTask getDatabaseListTask = new GetDatabaseListTask(site);
			getDatabaseListTask.execute();
			List<DatabaseInfo> databaseInfoList = getDatabaseListTask.loadDatabaseInfo();

			DatabaseInfo databaseInfo = null;
			for (DatabaseInfo bean : databaseInfoList) {

				if (bean.getDbName().equals(dbname)) {
					databaseInfo = bean;
					break;
				}
			}

			if (databaseInfo.getRunningType() == DbRunningType.STANDALONE) {
				CommonUpdateTask startTask = new CommonUpdateTask(CommonTaskName.START_DB_TASK_NAME, site,
				        CommonSendMsg.commonDatabaseSendMsg);
				startTask.setDbName(databaseInfo.getDbName());
				startTask.execute();
				assertEquals(null, startTask.getErrorMsg());
			}

		}

		// get the latest token
		token = site.getHostToken();

		if (envInfo == null) {
			GetEnvInfoTask env = new GetEnvInfoTask(site);
			envInfo = env.loadEnvInfo();
		}
		serverPath = envInfo.getRootDir();

	}

	/**
	 * This method is to find the file's path in a fragment or a plugin.
	 * 
	 * @param filepath
	 *            the file path in the fragment or a plugin
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

	/**
	 * Get server separator
	 * 
	 * @return
	 */
	public String getPathSeparator() {
		if (getServerOsInfo() == OsInfoType.NT)
			return "\\";
		else
			return "/";
	}

	/**
	 * Get server os info
	 * 
	 * @return
	 */
	public OsInfoType getServerOsInfo() {

		if (envInfo == null)
			return null;
		String osInfo = envInfo.getOsInfo();
		if (OsInfoType.NT.getText().equalsIgnoreCase(osInfo))
			return OsInfoType.NT;
		if (OsInfoType.LINUX.getText().equalsIgnoreCase(osInfo))
			return OsInfoType.LINUX;
		if (OsInfoType.UNIX.getText().equalsIgnoreCase(osInfo))
			return OsInfoType.UNIX;
		if (OsInfoType.UNKNOWN.getText().equalsIgnoreCase(osInfo))
			return OsInfoType.UNKNOWN;
		return null;

	}

}
