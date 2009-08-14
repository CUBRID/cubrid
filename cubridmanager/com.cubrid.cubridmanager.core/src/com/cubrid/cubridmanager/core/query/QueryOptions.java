/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */
package com.cubrid.cubridmanager.core.query;

import java.sql.Driver;

import org.eclipse.core.runtime.Preferences;

import com.cubrid.cubridmanager.core.CubridManagerCorePlugin;
import com.cubrid.cubridmanager.core.common.loader.CubridClassLoaderPool;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;

/**
 * 
 * provide some static method to get query options
 * 
 * @author wangsl 2009-3-31
 */
public class QueryOptions {

	public static final String PROPERTY = ".property";

	public static final String AUTO_COMMIT = ".auto_commit";
	public static final String ENABLE_UNIT_INSTANCES = ".enable_unit_instances";
	public static final String UNIT_INSTANCES_COUNT = ".instances_count";
	public static final String PAGE_INSTANCES_COUNT = ".page_count";
	public static final String ENABLE_QUERY_PLAN = ".enable_query_plan";
	public static final String ENABLE_GET_OID = ".enable_get_oid";
	public static final String BROKER_PORT = ".broker_port";
	public static final String ENABLE_CHAR_SET = ".enable_char_set";
	public static final String CHAR_SET = ".char_set";
	public static final String FONT_NAME = ".font_name";
	public static final String FONT_STRING = ".font_string";
	public static final String FONT_RGB_RED = ".font_red";
	public static final String FONT_RGB_GREEN = ".font_green";
	public static final String FONT_RGB_BLUE = ".font_blue";
	public static final String FONT_STYLE = ".font_style";
	public static final String SIZE = ".font_size";
	public static final String USE_DEFAULT_DRIVER = ".use_custom_driver";
	public static final String DRIVER_PATH = ".driver_path";
	public static final String BROKER_IP = ".broker_ip";

	public static final int defaultMaxRecordlimit = 5000;
	public static final int defaultMaxPagelimit = 100;

	public static final int fontColorRed = 0;
	public static final int fontColorGreen = 0;
	public static final int fontColorBlue = 0;
	public static final String STR_NULL = "NULL";

	public static final String[] ALlCHARSET = { "UTF-8", "ISO-8859-1",
			"EUC-KR", "EUC-JP", "GB2312", "GBK" };

	private static Preferences pref;

	static {
		if (CubridManagerCorePlugin.getDefault() == null) {
			pref = new Preferences();
		} else {
			pref = CubridManagerCorePlugin.getDefault().getPluginPreferences();
		}
		pref.setDefault(QueryOptions.AUTO_COMMIT, true);
		pref.setDefault(QueryOptions.ENABLE_UNIT_INSTANCES, true);
		pref.setDefault(QueryOptions.ENABLE_QUERY_PLAN, true);
		pref.setDefault(QueryOptions.USE_DEFAULT_DRIVER, true);
	}
	
	public static void setPref(String key, String value) {
		pref.setValue(key, value);
	}

	/**
	 * get query character set
	 * 
	 * @param database
	 * @return
	 */
	public static String getCharset(DatabaseInfo databaseInfo) {
		String prefix = getPrefix(databaseInfo);
		return pref.getString(prefix + QueryOptions.CHAR_SET);
	}

	public static String[] getFont(ServerInfo serverInfo) {
		String prefix = getPrefix(serverInfo);
		String fontString = pref.getString(prefix + QueryOptions.FONT_STRING);
		int fontStyle = pref.getInt(prefix + QueryOptions.FONT_STYLE);
		int fontSize = pref.getInt(prefix + QueryOptions.SIZE);
		if (fontSize == 0) {
			fontSize = 10;
		}
		return new String[] { fontString, String.valueOf(fontSize),
				String.valueOf(fontStyle) };
	}

	private static String getPrefix(ServerInfo serverInfo) {

		String prefix = "";
		if (serverInfo != null
				&& pref.contains(serverInfo.getHostAddress()
						+ QueryOptions.PROPERTY)) {
			prefix = serverInfo.getHostAddress();
		}
		return prefix;
	}

	private static String getPrefix(DatabaseInfo databaseInfo) {
		String prefix = "";
		if (databaseInfo != null
				&& pref.contains(databaseInfo.getServerInfo().getHostAddress()
						+ "." + databaseInfo.getDbName()
						+ QueryOptions.PROPERTY)) {
			prefix = databaseInfo.getServerInfo().getHostAddress() + "."
					+ databaseInfo.getDbName();
		}
		return prefix;
	}

	public static boolean getOidInfo(ServerInfo serverInfo) {
		String prefix = getPrefix(serverInfo);
		return pref.getBoolean(prefix + QueryOptions.ENABLE_GET_OID);
	}

	public static int getSearchLimit(ServerInfo serverInfo) {
		String prefix = getPrefix(serverInfo);
		int limit = pref.getInt(prefix + QueryOptions.UNIT_INSTANCES_COUNT);
		if (!getEnableSearchUnit(serverInfo)) {
			return -1;
		}
		return limit <= 0 ? defaultMaxRecordlimit : limit;
	}

	public static int getPageLimit(ServerInfo serverInfo) {
		String prefix = getPrefix(serverInfo);
		int limit = pref.getInt(prefix + QueryOptions.PAGE_INSTANCES_COUNT);
		return limit <= 0 ? defaultMaxPagelimit : limit;
	}

	public static String getBrokerIp(DatabaseInfo databaseInfo) {
		ServerUserInfo userInfo = databaseInfo.getServerInfo().getLoginedUserInfo();
		String brokerIP = null;
		if (userInfo.isAdmin()) {
			String prefix = getPrefix(databaseInfo);
			String ip = pref.getString(prefix + QueryOptions.BROKER_IP);
			if (ip != null && !ip.equals("")) {
				brokerIP = ip;
			}
			if (brokerIP == null) {
				brokerIP = databaseInfo.getServerInfo().getHostAddress();
			}
		}
		if (brokerIP == null) {
			brokerIP = databaseInfo.getBrokerIP();
		}
		return brokerIP;
	}

	/**
	 * get broker port for current user when admin login, use the item selected
	 * in host property page when un-admin user login, use the default broker
	 * port in database info.
	 * 
	 * @param database
	 * @return
	 */
	public static String getBrokerPort(DatabaseInfo databaseInfo) {
		ServerUserInfo userInfo = databaseInfo.getServerInfo().getLoginedUserInfo();
		String portInfo = null;
		if (userInfo.isAdmin()) {
			String prefix = getPrefix(databaseInfo);
			String port = pref.getString(prefix + QueryOptions.BROKER_PORT);
			if (port != null && !port.equals("")) {
				portInfo = port;
			}
		}
		if (portInfo == null) {
			portInfo = databaseInfo.getBrokerPort();
		}
		return portInfo;
	}

	public static boolean getAutoCommit(ServerInfo serverInfo) {
		String prefix = getPrefix(serverInfo);
		return pref.getBoolean(prefix + QueryOptions.AUTO_COMMIT);
	}

	public static boolean getQueryPlan(ServerInfo serverInfo) {
		String prefix = getPrefix(serverInfo);
		return pref.getBoolean(prefix + QueryOptions.ENABLE_QUERY_PLAN);
	}

	public static Driver getDriver(ServerInfo serverInfo) {
		String driverPath = getDriverPath(serverInfo);
		return CubridClassLoaderPool.getCubridDriver(driverPath);
	}

	public static String getDriverPath(ServerInfo serverInfo) {

		String prefix = getPrefix(serverInfo);
		boolean useDefaultDriver = pref.getBoolean(prefix
				+ QueryOptions.USE_DEFAULT_DRIVER);
		if (useDefaultDriver) {
			return "";
		}
		String driver = pref.getString(prefix + QueryOptions.DRIVER_PATH);
		return driver;
	}

	public static boolean getEnableCharset(DatabaseInfo databaseInfo) {
		String prefix = getPrefix(databaseInfo);
		return pref.getBoolean(prefix + QueryOptions.ENABLE_CHAR_SET);
	}

	public static boolean getEnableSearchUnit(ServerInfo serverInfo) {
		String prefix = getPrefix(serverInfo);
		return pref.getBoolean(prefix + QueryOptions.ENABLE_UNIT_INSTANCES);
	}
}
