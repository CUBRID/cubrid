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
package com.cubrid.cubridmanager.core.cubrid.jobauto.task;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.BackupPlanInfo;

/**
 * 
 * This task is responsible to get all backup plan list
 * 
 * @author pangqiren 2009-4-1
 */
public class GetBackupPlanListTask extends SocketTask {
	private static final String[] sendedMsgItems = new String[]
		{
		        "task", "token", "dbname"
		};

	/**
	 * @param taskName
	 * @param serverInfo
	 */
	public GetBackupPlanListTask(ServerInfo serverInfo) {
		super("getbackupinfo", serverInfo, sendedMsgItems);
	}

	public void setDbName(String dbName) {
		this.setMsgItem("dbname", dbName);
	}

	public List<BackupPlanInfo> getBackupPlanInfoList() {
		List<BackupPlanInfo> backupPlanInfoList = new ArrayList<BackupPlanInfo>();
		if(null != getErrorMsg()){
			return null;
		}
		TreeNode result = getResponse();
		String dbname = result.getValue("dbname");
		String[] backupidArr = result.getValues("backupid");
		String[] pathArr = result.getValues("path");
		String[] periodTypeArr = result.getValues("period_type");
		String[] periodDateArr = result.getValues("period_date");
		String[] timeArr = result.getValues("time");
		String[] levelArr = result.getValues("level");
		String[] archiveDelArr = result.getValues("archivedel");
		String[] updatestatusArr = result.getValues("updatestatus");
		String[] storeoldArr = result.getValues("storeold");
		String[] onoffArr = result.getValues("onoff");
		String[] zipArr = result.getValues("zip");
		String[] checkArr = result.getValues("check");
		String[] mtArr = result.getValues("mt");
		if(null == backupidArr)
			return null;
		for (int i = 0; i < backupidArr.length; i++) {
			BackupPlanInfo backupPlanInfo = new BackupPlanInfo();
			backupPlanInfo.setDbname(dbname);
			backupPlanInfo.setBackupid(backupidArr[i]);
			backupPlanInfo.setPath(pathArr[i]);
			backupPlanInfo.setPeriod_type(periodTypeArr[i]);
			backupPlanInfo.setPeriod_date(periodDateArr[i]);
			backupPlanInfo.setTime(timeArr[i]);
			backupPlanInfo.setLevel(levelArr[i]);
			backupPlanInfo.setArchivedel(archiveDelArr[i]);
			backupPlanInfo.setUpdatestatus(updatestatusArr[i]);
			backupPlanInfo.setStoreold(storeoldArr[i]);
			backupPlanInfo.setOnoff(onoffArr[i]);
			backupPlanInfo.setZip(zipArr[i]);
			backupPlanInfo.setCheck(checkArr[i]);
			backupPlanInfo.setMt(mtArr[i]);
			backupPlanInfoList.add(backupPlanInfo);
		}
		return backupPlanInfoList;
	}
}
