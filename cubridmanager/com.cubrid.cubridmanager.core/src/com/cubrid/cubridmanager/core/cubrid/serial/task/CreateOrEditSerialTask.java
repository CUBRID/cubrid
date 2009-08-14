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
package com.cubrid.cubridmanager.core.cubrid.serial.task;

import java.sql.SQLException;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.Messages;
import com.cubrid.cubridmanager.core.common.jdbc.JDBCTask;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;

/**
 * 
 * This task is responsible to create serial
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-11 created by pangqiren
 */
public class CreateOrEditSerialTask extends
		JDBCTask {

	private static final Logger logger = LogUtil.getLogger(CreateOrEditSerialTask.class);

	/**
	 * The constructor
	 * 
	 * @param dbInfo
	 */
	public CreateOrEditSerialTask(DatabaseInfo dbInfo) {
		super("CreateOrEditSerial", dbInfo, true);
	}

	/**
	 * 
	 * Create serial by JDBC
	 * 
	 * @param serialName
	 * @param startVal
	 * @param incrementVal
	 * @param maxVal
	 * @param minVal
	 * @param isCycle
	 */
	public void createSerial(String serialName, String startVal,
			String incrementVal, String maxVal, String minVal, boolean isCycle) {
		if (errorMsg != null && errorMsg.trim().length() > 0) {
			return;
		}
		String sql = "create serial \"" + serialName + "\"";
		if (startVal != null && startVal.trim().length() > 0) {
			sql += " start with " + startVal;
		}
		if (incrementVal != null && incrementVal.trim().length() > 0) {
			sql += " increment by " + incrementVal;
		}
		if (minVal != null && minVal.trim().length() > 0) {
			sql += " minvalue " + minVal;
		}
		if (maxVal != null && maxVal.trim().length() > 0) {
			sql += " maxvalue " + maxVal;
		}
		if (isCycle) {
			sql += " cycle";
		} else {
			sql += " nocycle";
		}
		try {
			if (connection == null || connection.isClosed()) {
				errorMsg = Messages.error_getConnection;
				return;
			}
			stmt = connection.createStatement();
			stmt.execute(sql);
		} catch (SQLException e) {
			errorMsg = e.getMessage();
		} finally {
			finish();
		}
	}

	/**
	 * 
	 * Edit serial by JDBC
	 * 
	 * @param serialName
	 * @param startVal
	 * @param incrementVal
	 * @param maxVal
	 * @param minVal
	 * @param isCycle
	 */
	public void editSerial(String serialName, String startVal,
			String incrementVal, String maxVal, String minVal, boolean isCycle) {
		if (errorMsg != null && errorMsg.trim().length() > 0) {
			return;
		}
		String dropSerialSql = "drop serial \"" + serialName + "\"";
		String sql = "create serial \"" + serialName + "\"";
		if (startVal != null && startVal.trim().length() > 0) {
			sql += " start with " + startVal;
		}
		if (incrementVal != null && incrementVal.trim().length() > 0) {
			sql += " increment by " + incrementVal;
		}
		if (minVal != null && minVal.trim().length() > 0) {
			sql += " minvalue " + minVal;
		}
		if (maxVal != null && maxVal.trim().length() > 0) {
			sql += " maxvalue " + maxVal;
		}
		if (isCycle) {
			sql += " cycle";
		} else {
			sql += " nocycle";
		}
		try {
			if (connection == null || connection.isClosed()) {
				errorMsg = Messages.error_getConnection;
				return;
			}
			connection.setAutoCommit(false);
			stmt = connection.createStatement();
			stmt.execute(dropSerialSql);
			stmt.execute(sql);
			connection.commit();
		} catch (SQLException e) {
			try {
				connection.rollback();
			} catch (SQLException e1) {
				logger.error(e1);
			}
			errorMsg = e.getMessage();
		} finally {
			finish();
		}
	}

}
