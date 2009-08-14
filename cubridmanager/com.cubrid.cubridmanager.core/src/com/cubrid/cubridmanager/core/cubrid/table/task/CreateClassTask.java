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
package com.cubrid.cubridmanager.core.cubrid.table.task;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBClasses;
/**
 * to create a table in CUBRID
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-5 created by moulinwang
 */
public class CreateClassTask extends SocketTask {
	private final static String[] sendMSGItems = new String[]
		{
		        "task",
		        "token",
		        "dbname",
		        "classname",
		        "username"
		};

	public CreateClassTask(ServerInfo serverInfo) {
		super("createclass", serverInfo, CreateClassTask.sendMSGItems);
	}

	/**
	 * Set the key "dbname" in request message
	 * 
	 * @param dbname
	 */
	public void setDbName(String dbname) {
		this.setMsgItem("dbname", dbname);
	}

	/**
	 * Set the key "classname" in request message
	 * 
	 * @param className
	 */
	public void setClassName(String className) {
		this.setMsgItem("classname", className);
	}

	/**
	 * Set the key "username" in request message,which indicates the user who
	 * creates the table
	 * 
	 * @param className
	 */
	public void setUserName(String userName) {
		this.setMsgItem("username", userName);
	}

	/**
	 * Get the ClassList object from the response
	 * 
	 * @return
	 */
	public DBClasses getClassList() {
		TreeNode classinfo = getResponse();
		return ModelUtil.getClassList(classinfo);
	}

}
