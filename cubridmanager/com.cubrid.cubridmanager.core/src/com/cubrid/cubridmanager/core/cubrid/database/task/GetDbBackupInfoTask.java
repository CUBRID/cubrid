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
import com.cubrid.cubridmanager.core.cubrid.database.model.DbBackupHistoryInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DbBackupInfo;

/**
 * 
 * when backup database,this task will get database backup history information
 * firstly.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class GetDbBackupInfoTask extends
		SocketTask {

	private static final String[] sendMSGItems = new String[] { "task",
			"token", "dbname" };

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public GetDbBackupInfoTask(ServerInfo serverInfo) {
		super("backupdbinfo", serverInfo, sendMSGItems);
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
	 * Get database backup information
	 * 
	 * @return
	 */
	public DbBackupInfo getDbBackupInfo() {
		TreeNode response = getResponse();
		if (response == null
				|| (this.getErrorMsg() != null && getErrorMsg().trim().length() > 0)) {
			return null;
		}
		DbBackupInfo dbBackupInfo = new DbBackupInfo();
		String dbDir = response.getValue("dbdir");
		dbBackupInfo.setDbDir(dbDir);
		String freeSpace = response.getValue("freespace");
		dbBackupInfo.setFreeSpace(freeSpace);
		for (int i = 0; i < response.childrenSize(); i++) {
			TreeNode node = response.getChildren().get(i);
			if (node == null) {
				continue;
			}
			String levelName = node.getValue("open");
			if (levelName == null || levelName.trim().length() <= 0) {
				continue;
			}
			if (levelName.indexOf("level") >= 0) {
				String path = node.getValue("path");
				String size = node.getValue("size");
				String date = node.getValue("data");
				DbBackupHistoryInfo dbBackupHistoryInfo = new DbBackupHistoryInfo(
						levelName, path, size, date);
				dbBackupInfo.addDbBackupHistoryInfo(dbBackupHistoryInfo);
			}
		}
		return dbBackupInfo;
	}

}
