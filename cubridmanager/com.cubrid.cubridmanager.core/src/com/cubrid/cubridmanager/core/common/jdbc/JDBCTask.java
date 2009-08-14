/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search Solution. 
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
package com.cubrid.cubridmanager.core.common.jdbc;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.Messages;
import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;

/**
 * 
 * This task is responsible to execute sql by JDBC
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public abstract class JDBCTask implements
		ITask {
	private static final Logger logger = LogUtil.getLogger(JDBCTask.class);
	protected String taskName = "";
	protected String errorMsg = null;
	protected String warningMsg = null;
	protected DatabaseInfo databaseInfo = null;
	protected Connection connection = null;
	protected Statement stmt = null;
	protected ResultSet rs = null;
	protected boolean isCancel = false;

	/**
	 * The constructor
	 * 
	 * @param taskName
	 * @param dbInfo
	 */
	public JDBCTask(String taskName, DatabaseInfo dbInfo) {
		this(taskName, dbInfo, true);
	}

	/**
	 * The constructor
	 * 
	 * @param taskName
	 * @param dbInfo
	 * @param isAutoCommit
	 */
	public JDBCTask(String taskName, DatabaseInfo dbInfo, boolean isAutoCommit) {
		this.databaseInfo = dbInfo;
		try {
			connection = JDBCConnectionManager.getConnection(dbInfo,
					isAutoCommit);
		} catch (Exception e) {
			errorMsg = Messages.error_getConnection;
		}
	}

	/**
	 * 
	 * Get error message after this task execute.if it is null,this task is
	 * ok,or it has error
	 * 
	 * @return
	 */
	public String getErrorMsg() {
		return errorMsg;
	}

	/**
	 * 
	 * Get warning message after this task execute
	 * 
	 * @return
	 */
	public String getWarningMsg() {
		return warningMsg;
	}

	/**
	 * 
	 * Set error message
	 * 
	 * @param errorMsg
	 */
	public void setErrorMsg(String errorMsg) {
		this.errorMsg = errorMsg;
	}

	/**
	 * 
	 * Set warning message
	 * 
	 * @param warningMsg
	 */
	public void setWarningMsg(String warningMsg) {
		this.warningMsg = warningMsg;
	}

	/**
	 * Return task name
	 */
	public String getTaskname() {
		return this.taskName;
	}

	/**
	 * Set task name
	 */
	public void setTaskname(String taskName) {
		this.taskName = taskName;
	}

	/**
	 * 
	 * Send request to Server
	 * 
	 */
	public void execute() {

	}

	/**
	 * 
	 * Get connection
	 * 
	 * @return
	 */
	public Connection getConnection() {
		return connection;
	}

	/**
	 * Cancel this operation
	 */
	public void cancel() {
		try {
			isCancel = true;
			if (stmt != null) {
				stmt.cancel();
			}
		} catch (SQLException e) {
			logger.error(e);
		}
		JDBCConnectionManager.close(connection, stmt, rs);
	}

	/**
	 * Free JDBC connection resource
	 */
	public void finish() {
		JDBCConnectionManager.close(connection, stmt, rs);
	}

	public boolean isCancel() {
		return isCancel;
	}

	public void setCancel(boolean isCancel) {
		this.isCancel = isCancel;
	}

	public boolean isSuccess() {
		return this.errorMsg == null;
	}
}
