/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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

package com.cubrid.cubridmanager.core.logs.model;

import java.util.ArrayList;
import java.util.List;

/**
 * 
 * This class is responsible to store all database log information
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-4-3 created by wuyingshi
 */
public class DbLogInfoList {

	private List<LogInfo> dbLogInfoList = null;

	/**
	 * The constructor
	 */
	public DbLogInfoList() {
		dbLogInfoList = new ArrayList<LogInfo>();
	}

	/**
	 * add a database log info to a DbLogInfo list.
	 * 
	 * @param dbLogInfo
	 */
	public synchronized void addLog(LogInfo dbLogInfo) {
		if (dbLogInfoList == null) {
			dbLogInfoList = new ArrayList<LogInfo>();
		}
		if (!dbLogInfoList.contains(dbLogInfo))
			dbLogInfoList.add(dbLogInfo);
	}

	/**
	 * remove a database log information from dbLogInfoList.
	 * 
	 * @param dbLogInfo
	 */
	public synchronized void removeLog(LogInfo dbLogInfo) {
		if (dbLogInfoList != null) {
			dbLogInfoList.remove(dbLogInfo);
		}
	}

	/**
	 * clear the dbLogInfoList.
	 * 
	 */
	public synchronized void removeAllLog() {
		if (dbLogInfoList != null) {
			dbLogInfoList.clear();
		}
	}

	/**
	 * get the dbLogInfoList.
	 * 
	 * @return
	 */
	public List<LogInfo> getDbLogInfoList() {
		return dbLogInfoList;
	}

	/**
	 * get log information of it's path equal to parameter path.
	 * 
	 * @param path
	 * @return
	 */
	public synchronized LogInfo getDbLogInfo(String path) {
		if (dbLogInfoList == null) {
			return null;
		}
		for (LogInfo dbLogInfo : dbLogInfoList) {
			if (dbLogInfo.getPath().equals(path)) {
				return dbLogInfo;
			}
		}
		return null;
	}
}
