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
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

/**
 * 
 * Get backup volume information of some level and pathname,if do not set level
 * and pathname, this task will get the last level's backup volume information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class GetBackupVolInfoTask extends
		SocketTask {

	private static final String[] sendMSGItems = new String[] { "task",
			"token", "dbname", "level", "pathname" };

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public GetBackupVolInfoTask(ServerInfo serverInfo) {
		super("backupvolinfo", serverInfo, sendMSGItems);
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
	 * Set the level of backup
	 * 
	 * @param level
	 */
	public void setLevel(String level) {
		super.setMsgItem("level", level);
	}

	/**
	 * 
	 * Set backup path
	 * 
	 * @param path
	 */
	public void setPath(String path) {
		super.setMsgItem("pathname", path);
	}

	/**
	 * 
	 * Get database backup vol information
	 * 
	 * @return
	 */
	public String getDbBackupVolInfo() {
		TreeNode response = getResponse();
		if (response == null
				|| (this.getErrorMsg() != null && getErrorMsg().trim().length() > 0)) {
			return null;
		}
		StringBuffer buffer = new StringBuffer();
		String[] lines = response.getValues("line");
		for (int i = 0; lines != null && i < lines.length; i++) {
			buffer.append(lines[i] + "\n");
		}
		return buffer.toString();
	}

}
