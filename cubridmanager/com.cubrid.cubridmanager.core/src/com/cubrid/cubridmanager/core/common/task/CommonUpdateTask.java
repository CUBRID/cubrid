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
package com.cubrid.cubridmanager.core.common.task;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.YesNoType;

/**
 * 
 * The common task of update transaction. eg:drop class,copy class,back up
 * database
 * 
 * @author robin
 * @version 1.0 - 2009-6-4 created by robin
 */
public class CommonUpdateTask extends
		SocketTask {

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public CommonUpdateTask(String taskName, ServerInfo serverInfo,
			String[] sendMSGItems) {
		super(taskName, serverInfo, sendMSGItems);
	}

	/**
	 * set dbname into msg
	 * 
	 * @param dbName
	 */
	public void setDbName(String dbName) {
		super.setMsgItem("dbname", dbName);
	}
	/**
	 * set dbname into msg
	 * 
	 * @param dbName
	 */
	public void setRepairDb(YesNoType type) {
		super.setMsgItem("repairdb", type.toString().toLowerCase());
	}

	/**
	 * set class name.taskname:optimizedb
	 * 
	 * @param className
	 */
	public void setClassName(String className) {
		super.setMsgItem("classname", className);
	}

	/**
	 * set delbackup value.taskname:deletedb
	 * 
	 * @param delbackup
	 */
	public void setDelbackup(YesNoType delbackup) {
		super.setMsgItem("delbackup", delbackup.getText().toLowerCase());
	}

	/**
	 * set delbackup value.taskname:deletedb
	 * 
	 * @param delbackup
	 */
	public void setUserName(String userName) {
		super.setMsgItem("username", userName);
	}

}
