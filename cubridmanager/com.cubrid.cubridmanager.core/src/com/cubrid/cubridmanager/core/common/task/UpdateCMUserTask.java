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

/**
 * 
 * This task is responsible to update the latest logined database user to CUBRID
 * Manager server
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class UpdateCMUserTask extends
		SocketTask {
	private static final String[] sendedMsgItems = new String[] { "task",
			"token", "targetid",
			"{open,dbname,dbid,dbpassword,dbbrokeraddress,close}", "casauth",
			"dbcreate", "statusmonitorauth" };

	/**
	 * The constructor
	 * 
	 * @param taskName
	 * @param serverInfo
	 */
	public UpdateCMUserTask(ServerInfo serverInfo) {
		super("updatedbmtuser", serverInfo, sendedMsgItems);
	}

	/**
	 * 
	 * Set CUBRID Manager user name
	 * 
	 * @param userName
	 */
	public void setCmUserName(String userName) {
		this.setMsgItem("targetid", userName);
	}

	/**
	 * 
	 * Set cas authorization information(admin,30000)
	 * 
	 * @param casAuthStr
	 */
	public void setCasAuth(String casAuthStr) {
		this.setMsgItem("casauth", casAuthStr);
	}

	/**
	 * 
	 * Set db creator
	 * 
	 * @param dbCreator
	 */
	public void setDbCreator(String dbCreator) {
		this.setMsgItem("dbcreate", dbCreator);
	}

	/**
	 * 
	 * Set db authorization information
	 * 
	 * @param dbName
	 * @param dbId
	 * @param password
	 * @param brokerAddress
	 */
	public void setDbAuth(String dbName, String dbId, String password,
			String brokerAddress) {
		this.setMsgItem("open", "dbauth");
		this.setMsgItem("dbname", dbName);
		this.setMsgItem("dbid", dbId);
		this.setMsgItem("dbpassword", password);
		this.setMsgItem("dbbrokeraddress", brokerAddress);
		this.setMsgItem("close", "dbauth");
	}

	/**
	 * 
	 * Set db authorization information
	 * 
	 * @param dbName
	 * @param dbId
	 * @param password
	 * @param brokerAddress
	 */
	public void setDbAuth(String[] dbName, String[] dbId, String[] password,
			String[] brokerAddress) {
		if (dbName != null && dbName.length > 0) {
			String[] keys = new String[dbName.length];
			for (int i = 0; i < keys.length; i++) {
				keys[i] = "dbauth";
			}
			this.setMsgItem("open", keys);
			this.setMsgItem("dbname", dbName);
			this.setMsgItem("dbid", dbId);
			this.setMsgItem("dbpassword", password);
			this.setMsgItem("dbbrokeraddress", brokerAddress);
			this.setMsgItem("close", keys);
		} else {
			this.setMsgItem("open", "dbauth");
			this.setMsgItem("close", "dbauth");
		}
	}

	/**
	 * 
	 * Set status monitor auth
	 * 
	 * @param statusMonitorAuth
	 */
	public void setStatusMonitorAuth(String statusMonitorAuth) {
		this.setMsgItem("statusmonitorauth", statusMonitorAuth);
	}
}
