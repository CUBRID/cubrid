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

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;

/**
 * 
 * This task is responsible to load database list that CUBRID Manager user can
 * access.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class GetDatabaseListTask extends
		SocketTask {
	private static final String[] sendedMsgItems = new String[] { "task",
			"token" };

	/**
	 * @param taskName
	 * @param serverInfo
	 */
	public GetDatabaseListTask(ServerInfo serverInfo) {
		super("startinfo", serverInfo, sendedMsgItems);
	}

	/**
	 * 
	 * Load CUBRID Server database information
	 * 
	 */
	public List<DatabaseInfo> loadDatabaseInfo() {
		TreeNode response = getResponse();
		if (response == null
				|| (this.getErrorMsg() != null && getErrorMsg().trim().length() > 0)) {
			return null;
		}
		List<DatabaseInfo> databaseInfoList = new ArrayList<DatabaseInfo>();
		for (int i = 0; i < response.childrenSize(); i++) {
			TreeNode node = response.getChildren().get(i);
			if (node.getValue("open") != null
					&& node.getValue("open").equals("dblist")) {
				String[] dbNameArr = node.getValues("dbname");
				String[] dbDirArr = node.getValues("dbdir");
				for (int j = 0; dbNameArr != null && j < dbNameArr.length; j++) {
					String dbName = dbNameArr[j];
					DatabaseInfo databaseInfo = new DatabaseInfo(dbName,
							serverInfo);
					if (dbDirArr != null && dbDirArr.length > j) {
						databaseInfo.setDbDir(dbDirArr[j]);
					}
					databaseInfoList.add(databaseInfo);
				}
			}
			if (node.getValue("open") != null
					&& node.getValue("open").equals("activelist")) {
				String[] dbNameArr = node.getValues("dbname");
				for (int j = 0; dbNameArr != null && j < dbNameArr.length; j++) {
					String dbName = dbNameArr[j];
					for (int k = 0; k < databaseInfoList.size(); k++) {
						DatabaseInfo databaseInfo = databaseInfoList.get(k);
						if (databaseInfo.getDbName().equals(dbName)) {
							databaseInfo.setRunningType(DbRunningType.CS);
							break;
						}
					}
				}
			}
		}
		return databaseInfoList;
	}
}
