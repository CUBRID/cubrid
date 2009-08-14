/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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
package com.cubrid.cubridmanager.core.common.jdbc;

import java.sql.Connection;
import java.sql.Driver;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Properties;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.common.loader.CubridClassLoaderPool;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.query.QueryOptions;

/**
 * 
 * This class is responsible to provide JDBC connection service
 * 
 * @author wangsl
 * @version 1.0 - 2009-6-4 created by wangsl
 */
public class JDBCConnectionManager {

	private static final Logger logger = LogUtil.getLogger(JDBCConnectionManager.class);

	/**
	 * 
	 * Get new JDBC connection
	 * 
	 * @param dbInfo
	 * @param autoCommit
	 * @return
	 * @throws SQLException
	 */
	public static Connection getConnection(DatabaseInfo dbInfo,
			boolean autoCommit) throws SQLException {
		if (dbInfo == null || dbInfo.getServerInfo() == null) {
			throw new IllegalArgumentException();
		}
		ServerInfo serverInfo = dbInfo.getServerInfo();
		String hostAddress = serverInfo.getHostAddress();
		String brokerIP = QueryOptions.getBrokerIp(dbInfo);
		DbUserInfo userInfo = dbInfo.getAuthLoginedDbUserInfo();
		String url = "jdbc:cubrid:"
				+ (brokerIP != null && brokerIP.trim().length() > 0 ? brokerIP
						: hostAddress) + ":"
				+ QueryOptions.getBrokerPort(dbInfo) + ":" + dbInfo.getDbName()
				+ ":" + userInfo.getName() + ":";
		if (userInfo.getNoEncryptPassword() != null) {
			url += userInfo.getNoEncryptPassword() + ":";
		} else {
			url += ":";
		}
		if (dbInfo.getCharSet() != null
				&& dbInfo.getCharSet().trim().length() > 0) {
			url += "charset=" + dbInfo.getCharSet();
		}
		logger.debug("connection url=" + url);
		Driver cubridDriver = CubridClassLoaderPool.getCubridDriver(serverInfo.getDriverPath());
		DriverManager.registerDriver(cubridDriver);
		//can't get connection throw DriverManger
		Properties props = new Properties();
		props.put("user", userInfo.getName());
		props.put("password", userInfo.getNoEncryptPassword());
		Connection conn = cubridDriver.connect(url, props);
		// Connection conn = DriverManager.getConnection(url);
		conn.setAutoCommit(autoCommit);
		return conn;
	}

	/**
	 * close the jdbc connection
	 * 
	 * @throws SQLException
	 */
	public static void close(Connection conn, Statement stmt, ResultSet rs) {
		try {
			if (rs != null)
				rs.close();
			rs = null;
		} catch (Exception e) {
			logger.error(e);
		}
		try {
			if (stmt != null)
				stmt.close();
			stmt = null;
		} catch (Exception e) {
			logger.error(e);
		}
		try {
			if (conn != null)
				conn.close();
			conn = null;
		} catch (Exception e) {
			logger.error(e);
		}
	}

}
