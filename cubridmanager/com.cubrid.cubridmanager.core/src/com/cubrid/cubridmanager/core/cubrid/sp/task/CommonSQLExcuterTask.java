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
package com.cubrid.cubridmanager.core.cubrid.sp.task;

import java.sql.CallableStatement;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.Messages;
import com.cubrid.cubridmanager.core.common.jdbc.JDBCTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;

/**
 * 
 * This task is responsible to delete serial
 * 
 * @author robin
 * @version 1.0 - 2009-5-20 created by robin
 */
public class CommonSQLExcuterTask extends
		JDBCTask {

	private List<String> sqls = null;
	private List<String> callSqls = null;

	private int errorCode = -1;
	private String currentDDL;

	public CommonSQLExcuterTask(DatabaseInfo dbInfo) {
		super("CreateFuncProcTask", dbInfo, true);
	}

	public void execute() {
		try {

			if (errorMsg != null && errorMsg.trim().length() > 0) {
				return;
			}
			if (connection == null || connection.isClosed()) {
				errorMsg = Messages.error_getConnection;
				return;
			}
			connection.setAutoCommit(false);
			stmt = connection.createStatement();
			if (sqls != null)
				for (String sql : sqls) {
					currentDDL = sql;
					stmt.executeUpdate(sql);
				}
			stmt.close();
			if (callSqls != null)
				for (String sql : callSqls) {
					currentDDL = sql;
					CallableStatement cs = connection.prepareCall(sql);
					cs.execute();
					cs.close();
				}
			connection.commit();
		} catch (SQLException e) {
			errorCode = e.getErrorCode();
			errorMsg = currentDDL + ";\nError code: " + errorCode
					+ "\n            " + e.getMessage();
			try {
				connection.rollback();
			} catch (SQLException e1) {
				errorMsg = e1.getMessage();
			}

		} finally {
			finish();
		}
	}

	public List<String> getSqls() {
		return sqls;
	}

	public void addSqls(String sql) {
		if (sqls == null)
			sqls = new ArrayList<String>();
		this.sqls.add(sql);
	}

	public int getErrorCode() {
		return errorCode;
	}

	public void setErrorCode(int errorCode) {
		this.errorCode = errorCode;
	}

	public List<String> getCallSqls() {
		return callSqls;
	}

	public void addCallSqls(String sql) {
		if (callSqls == null)
			callSqls = new ArrayList<String>();
		this.callSqls.add(sql);
	}

	public String getCurrentDDL() {
		return currentDDL;
	}

}
