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
package com.cubrid.cubridmanager.core.cubrid.table.task;

import java.sql.SQLException;
import java.util.ArrayList;

import com.cubrid.cubridmanager.core.Messages;
import com.cubrid.cubridmanager.core.common.jdbc.JDBCTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.serial.model.SerialInfo;

import cubrid.jdbc.driver.CUBRIDResultSet;

/**
 * get auto increment information for schema
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-10 created by moulinwang
 */
public class GetAutoIncrementTask extends
		JDBCTask {
	String tableName;
	private ArrayList<SerialInfo> serialInfoList;

	public void setTableName(String tableName) {
		this.tableName = tableName.toLowerCase();
	}

	/**
	 * The constructor
	 * 
	 * @param dbInfo
	 * 
	 */
	public GetAutoIncrementTask(DatabaseInfo dbInfo) {
		super("GetSerialInfo", dbInfo,  false);
	}

	/**
	 * 
	 * Send request to Server
	 * 
	 */
	public void execute() {
		serialInfoList = new ArrayList<SerialInfo>();
		try {
			if (errorMsg != null && errorMsg.trim().length() > 0) {
				return;
			}
			if (connection == null || connection.isClosed()) {
				errorMsg = Messages.error_getConnection;
				return;
			}
			stmt = connection.createStatement();

			String sql = "select name,owner.name,current_val,increment_val,max_val,min_val,cyclic,started,class_name,att_name "
					+ "from db_serial where class_name =\'" + tableName + "\'";
			rs = (CUBRIDResultSet) stmt.executeQuery(sql);
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
			rs.close();
			stmt.close();
			connection.close();
		} catch (SQLException e) {
			errorMsg = e.getMessage();
		} finally {
			finish();
		}
	}

	public ArrayList<SerialInfo> getSerialInfoList() {
		return serialInfoList;
	}

}
