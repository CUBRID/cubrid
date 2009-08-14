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

import com.cubrid.cubridmanager.core.common.model.OnOffType;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.YesNoType;

/**
 * Base on the name task,this task is responsible for add or edit the backup plan.
 * 
 * @author lizhiqiang Apr 1, 2009
 */
public class BackupPlanTask extends SocketTask {

	private static final String[] sendMSGItems = new String[] { 
		"task",
		"token",
		"dbname",
		"backupid",
	    "path",
	    "period_type",
	    "period_date",
	    "time",
	    "level",
	    "archivedel",
	    "updatestatus",
	    "storeold",
	    "onoff",
	    "zip",
	    "check",
        "mt"
	};
	
	/**
	 * The constructor
	 * 
	 * @param taskname admit only "addbackupinfo","setbackupinfo","deletebackupinfo"
	 * @param serverInfo
	 */
	public BackupPlanTask(String taskname,ServerInfo serverInfo) {	
		 super(taskname, serverInfo,sendMSGItems);	
	    }
	
	public void setDbname(String dbname) {
		super.setMsgItem("dbname", dbname);
	}
	public void setBackupid(String backupid) {
		super.setMsgItem("backupid", backupid);
	}
	public void setPath(String path) {
		super.setMsgItem("path", path);
	}
	public void setPeriodType(String period_type) {
		super.setMsgItem("period_type", period_type);
	}
	public void setPeriodDate(String period_date) {
		super.setMsgItem("period_date", period_date);
	}
	public void setTime(String time) {
		super.setMsgItem("time", time);
	}
	public void setLevel(String level) {
		super.setMsgItem("level", level);
	}
	public void setArchivedel(OnOffType onOffType) {
		super.setMsgItem("archivedel", onOffType.getText());
	}
	public void setStoreold(OnOffType onOffType) {
		super.setMsgItem("storeold", onOffType.getText());
	}
	public void setOnoff(OnOffType onOffType) {
		super.setMsgItem("onoff", onOffType.getText());
	}
	public void setZip(YesNoType yesNoType) {
		super.setMsgItem("zip", yesNoType.getText());
	}
	public void setCheck(YesNoType yesNoType) {
		super.setMsgItem("check", yesNoType.getText());
	}
	public void setUpdatestatus(OnOffType onOffType) {
		super.setMsgItem("updatestatus", onOffType.getText());
	}
	public void setMt(String mt) {
		super.setMsgItem("mt", mt);
	}

}
