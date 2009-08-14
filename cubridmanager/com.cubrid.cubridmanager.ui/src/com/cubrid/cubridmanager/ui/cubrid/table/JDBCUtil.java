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
package com.cubrid.cubridmanager.ui.cubrid.table;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.List;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.common.jdbc.JDBCConnectionManager;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.serial.model.SerialInfo;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

import cubrid.jdbc.driver.CUBRIDConnection;

public class JDBCUtil {
	private static final Logger logger = LogUtil.getLogger(JDBCUtil.class);
	private static String resultMsg;
	private static final String NEW_LINE = System.getProperty("line.separator");

	public static Connection getConnection(CubridDatabase database) throws SQLException,
			ClassNotFoundException {
		Connection con = null;
		DatabaseInfo dbInfo = database.getDatabaseInfo();
		//open connection
		con = JDBCConnectionManager.getConnection(dbInfo, true);

		//set configuration of connection
		con.setTransactionIsolation(Connection.TRANSACTION_READ_UNCOMMITTED);
		((CUBRIDConnection) con).setLockTimeout(1);
		return con;
	}

	public static boolean insertRecord(CubridDatabase database, String sql) {
		Connection conn = null;
		PreparedStatement stmt = null;
		resultMsg = "";
		try {
			conn = JDBCUtil.getConnection(database);
			stmt = conn.prepareStatement(sql);
			stmt.execute();

			int updateCount = stmt.getUpdateCount();
			if (updateCount <= 1) {
				resultMsg = Messages.insertedCountMsg1;
			} else {
				resultMsg = Messages.bind(Messages.insertedCountMsg1,
						updateCount);
			}
			stmt.close();
			conn.close();
			return true;
		} catch (SQLException e) {
			resultMsg = Messages.bind(Messages.insertFailed1, e.getErrorCode(),
					e.getMessage());
			logger.error(e);
			return false;
		} catch (ClassNotFoundException e) {
			resultMsg = Messages.bind(Messages.insertFailed2, e.getMessage());
			logger.error(e);
			return false;
		} finally {
			try {
				if (stmt != null)
					stmt.close();
			} catch (SQLException e) {
				logger.error(e);
			}
			try {
				if (conn != null)
					conn.close();
			} catch (SQLException e) {
				logger.error(e);
			}
		}
	}

	public static String getResultMsg() {
		return resultMsg;
	}



	public static List<SerialInfo> getAutoIncrement(CubridDatabase database,
			String table) {
		List<SerialInfo> serialInfoList = new ArrayList<SerialInfo>();
		Connection conn = null;
		PreparedStatement stmt = null;

		try {
			String sql = "select name,owner.name,current_val,increment_val,max_val,min_val,cyclic,started,class_name,att_name "
					+ "from db_serial where class_name =\'" + table + "\'";
			conn = JDBCUtil.getConnection(database);
			stmt = conn.prepareStatement(sql);
			stmt.execute();
			ResultSet rs = stmt.getResultSet();

			while (rs.next()) {
				String name = rs.getString("name");
				String owner = rs.getString("owner.name");
				String currentVal = rs.getString("current_val");
				String incrementVal = rs.getString("increment_val");
				String maxVal = rs.getString("max_val");
				String minVal = rs.getString("min_val");
				String cyclic = rs.getString("cyclic");
				String startVal = rs.getString("started");
				String className = rs.getString("class_name");
				String attName = rs.getString("att_name");
				boolean isCycle = false;
				if (cyclic != null && cyclic.equals("1")) {
					isCycle = true;
				}
				SerialInfo serialInfo = new SerialInfo(name, owner, currentVal,
						incrementVal, maxVal, minVal, isCycle, startVal,
						className, attName);
				serialInfoList.add(serialInfo);
			}

			stmt.close();
			conn.close();
			return serialInfoList;
		} catch (SQLException e) {
			CommonTool.openErrorBox(e.getErrorCode() + NEW_LINE
					+ e.getMessage());
			logger.error(e);
		} catch (ClassNotFoundException e) {
			CommonTool.openErrorBox(Messages.sqlConnectionError + NEW_LINE
					+ e.getMessage());
			logger.error(e);
		} finally {
			try {
				if (stmt != null)
					stmt.close();
			} catch (SQLException e) {
				logger.error(e);
			}
			try {
				if (conn != null)
					conn.close();
			} catch (SQLException e) {
				logger.error(e);
			}
		}
		return serialInfoList;
	}

}
