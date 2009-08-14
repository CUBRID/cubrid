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

package com.cubrid.cubridmanager.core.cubrid.user.task;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.task.CommonTaskName;
import com.cubrid.cubridmanager.core.cubrid.database.model.UserSendObj;

/**
 * 
 * the task of copy database
 * 
 * @author robin 2009-3-25
 */
public class UpdateAddUserTask extends SocketTask {

	private static final String[] sendMSGItems = new String[]
		{
		        "task",
		        "token",
		        "dbname",
		        "username"
		};
	private UserSendObj userSendObj;

	public UserSendObj getUserSendObj() {
		return userSendObj;
	}

	public void setUserSendObj(UserSendObj userSendObj) {

		this.userSendObj = userSendObj;

		StringBuffer sb = new StringBuffer();
		sb.append("dbname:").append(userSendObj.getDbname()).append("\n");
		sb.append("username:").append(userSendObj.getUsername()).append("\n");
		if (userSendObj.getUserpass() != null)
			sb.append("userpass:").append(userSendObj.getUserpass()).append("\n");
		if (userSendObj.getGroups() != null && userSendObj.getGroups().size() > 0) {
			sb.append("open:groups\n");
			for (String group : userSendObj.getGroups())
				sb.append("group:").append(group).append("\n");
			sb.append("close:groups\n");
		}
		if (userSendObj.getAddmembers() != null && userSendObj.getAddmembers().size() > 0) {
			sb.append("open:addmembers\n");
			for (String member : userSendObj.getAddmembers())
				sb.append("member:").append(member).append("\n");
			sb.append("close:addmembers\n");
		}

		if (userSendObj.getRemovemembers() != null && userSendObj.getRemovemembers().size() > 0) {
			sb.append("open:removemembers\n");
			for (String member : userSendObj.getRemovemembers())
				sb.append("member:").append(member).append("\n");
			sb.append("close:removemembers\n");
		}

		sb.append("open:authorization\n");
		if (userSendObj.getAuthorization() != null && userSendObj.getAuthorization().size() > 0) {
			for (String tableName : userSendObj.getAuthorization().keySet())
				sb.append(tableName).append(":").append(userSendObj.getAuthorization().get(tableName)).append("\n");
		}
		sb.append("close:authorization\n");

		this.setAppendSendMsg(sb.toString());
	}

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public UpdateAddUserTask(ServerInfo serverInfo,boolean isNew) {

		super(isNew ? CommonTaskName.ADD_USER_TASK_NAME : CommonTaskName.UPDATE_USER_TASK_NAME, serverInfo,
		        sendMSGItems);
	}

	public void setDbName(String dbname) {
		super.setMsgItem("dbname", dbname);
	}

	public void setUserName(String username) {
		super.setMsgItem("username", username);
	}

}
