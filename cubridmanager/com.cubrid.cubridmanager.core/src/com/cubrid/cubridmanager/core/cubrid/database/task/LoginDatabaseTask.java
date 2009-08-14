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
package com.cubrid.cubridmanager.core.cubrid.database.task;

import com.cubrid.cubridmanager.core.Messages;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;

/**
 * 
 * This task is responsible to login database
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class LoginDatabaseTask extends
		SocketTask {
	private static final String[] sendedMsgItems = new String[] { "task",
			"token", "targetid", "dbname", "dbuser", "dbpasswd" };

	private String dbUser = null;
	private String dbPassword = null;
	private String dbName = null;

	/**
	 * The constructor
	 * 
	 * @param taskName
	 * @param serverInfo
	 */
	public LoginDatabaseTask(ServerInfo serverInfo) {
		super("dbmtuserlogin", serverInfo, sendedMsgItems);
	}

	/**
	 * 
	 * Set CUBRID Manager user name
	 * 
	 * @param userName
	 */
	public void setCMUser(String userName) {
		this.setMsgItem("targetid", userName);
	}

	/**
	 * 
	 * Set database name
	 * 
	 * @param dbName
	 */
	public void setDbName(String dbName) {
		this.dbName = dbName;
		this.setMsgItem("dbname", dbName);
	}

	/**
	 * 
	 * Set database user
	 * 
	 * @param dbUser
	 */
	public void setDbUser(String dbUser) {
		this.dbUser = dbUser;
		this.setMsgItem("dbuser", dbUser);
	}

	/**
	 * 
	 * Set database password
	 * 
	 * @param dbPassword
	 */
	public void setDbPassword(String dbPassword) {
		this.dbPassword = dbPassword;
		this.setMsgItem("dbpasswd", dbPassword);
	}

	/**
	 * 
	 * Get logined database user information
	 * 
	 * @return
	 */
	public DbUserInfo getLoginedDbUserInfo() {
		TreeNode response = getResponse();
		if (response == null || response.getValue("dbname") == null) {
			setErrorMsg(Messages.error_invalidUser);
			return null;
		}
		// String cmUser = response.getValue("targetid");
		String dbName = response.getValue("dbname");
		String isDbaStr = response.getValue("authority");
		boolean isDba = isDbaStr != null && isDbaStr.equals("isdba");
		DbUserInfo userInfo = new DbUserInfo(dbName, dbUser, "", dbPassword,
				isDba);
		return userInfo;
	}

	/**
	 * Cancel this task
	 */
	public void cancel() {
		serverInfo.getLoginedUserInfo().getDatabaseInfo(dbName).setLogined(true);
		serverInfo.getLoginedUserInfo().getDatabaseInfo(dbName).setAuthLoginedDbUserInfo(
				null);
		super.cancel();
	}
}
