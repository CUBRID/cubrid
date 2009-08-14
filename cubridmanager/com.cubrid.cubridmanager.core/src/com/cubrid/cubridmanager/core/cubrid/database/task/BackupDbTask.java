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

import com.cubrid.cubridmanager.core.common.StringUtil;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;

/**
 * 
 * This task is responsible to backup database
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class BackupDbTask extends
		SocketTask {

	private static final String[] sendMSGItems = new String[] { "task",
			"token", "dbname", "level", "volname", "backupdir", "removelog",
			"check", "mt", "zip", "safereplication" };

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public BackupDbTask(ServerInfo serverInfo) {
		super("backupdb", serverInfo, sendMSGItems);
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
	 * Set backup level
	 * 
	 * @param level
	 */
	public void setLevel(String level) {
		super.setMsgItem("level", level);
	}

	/**
	 * 
	 * Set volume name
	 * 
	 * @param volName
	 */
	public void setVolumeName(String volName) {
		super.setMsgItem("volname", volName);
	}

	/**
	 * 
	 * Set backup dir
	 * 
	 * @param dir
	 */
	public void setBackupDir(String dir) {
		super.setMsgItem("backupdir", dir);
	}

	/**
	 * 
	 * Set whether log is removed
	 * 
	 * @param isDeleted
	 */
	public void setRemoveLog(boolean isDeleted) {
		super.setMsgItem("removelog", StringUtil.yn(isDeleted));
	}

	/**
	 * 
	 * Set whether check database consistence
	 * 
	 * @param isChecked
	 */
	public void setCheckDatabaseConsist(boolean isChecked) {
		super.setMsgItem("check", StringUtil.yn(isChecked));
	}

	/**
	 * 
	 * Set parallel backup thread count
	 * 
	 * @param threadNum
	 */
	public void setThreadCount(String threadNum) {
		super.setMsgItem("mt", threadNum);
	}

	/**
	 * 
	 * Set whether compress
	 * 
	 * @param isZiped
	 */
	public void setZiped(boolean isZiped) {
		super.setMsgItem("zip", StringUtil.yn(isZiped));
	}

	/**
	 * 
	 * Set whether safe replication
	 * 
	 * @param isSafeReplication
	 */
	public void setSafeReplication(boolean isSafeReplication) {
		super.setMsgItem("safereplication", StringUtil.yn(isSafeReplication));
	}
}
