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

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;

/**
 * 
 * This task is responsible to restore database
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class RestoreDbTask extends
		SocketTask {

	private static final String[] sendMSGItems = new String[] { "task",
			"token", "dbname", "date", "level", "partial", "pathname",
			"recoverypath" };

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public RestoreDbTask(ServerInfo serverInfo) {
		super("restoredb", serverInfo, sendMSGItems);
	}

	/**
	 * Set the database name
	 * 
	 * @param dirs
	 */
	public void setDbName(String dbName) {
		super.setMsgItem("dbname", dbName);
	}

	/**
	 * 
	 * Set restored date
	 * 
	 * @param date
	 */
	public void setDate(String date) {
		super.setMsgItem("date", date);
	}

	/**
	 * 
	 * Set restored level
	 * 
	 * @param level
	 */
	public void setLevel(String level) {
		super.setMsgItem("level", level);
	}

	/**
	 * Set whether it is partial
	 * 
	 * @param isPartial
	 */
	public void setPartial(boolean isPartial) {
		if (isPartial)
			super.setMsgItem("partial", "y");
		else
			super.setMsgItem("partial", "n");
	}

	/**
	 * 
	 * Set restored level file path
	 * 
	 * @param str
	 */
	public void setPathName(String str) {
		super.setMsgItem("pathname", str);
	}

	/**
	 * 
	 * Set recovery path
	 * 
	 * @param str
	 */
	public void setRecoveryPath(String str) {
		super.setMsgItem("recoverypath", str);
	}
}
