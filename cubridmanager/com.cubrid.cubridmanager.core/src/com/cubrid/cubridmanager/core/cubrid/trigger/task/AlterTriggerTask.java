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

package com.cubrid.cubridmanager.core.cubrid.trigger.task;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerStatus;
import com.cubrid.cubridmanager.core.cubrid.trigger.model.Trigger;

/**
 * This class is to alter a trigger in CUBRID database.
 * 
 * Usage: You must first set fields by invoking setXXX(\<T\>) methods, then
 * call sendMsg() method to send a request message, the response message is the
 * information of the special class.
 * 
 * @author moulinwang 2009-3-3
 */
public class AlterTriggerTask extends SocketTask {
	private final static String[] sendMSGItems = new String[]
		{
		        "task",
		        "token",
		        "dbname",
		        "triggername",
		        "status",
		        "priority"
		};


	public AlterTriggerTask(ServerInfo serverInfo) {
		super("altertrigger", serverInfo, AlterTriggerTask.sendMSGItems);
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
	 * Set the key "triggername" in request message
	 * 
	 * @param triggerName
	 */
	public void setTriggerName(String triggerName) {
		this.setMsgItem("triggername", triggerName);
	}
	

	/**
	 * Set the key "status" in request message
	 * 
	 * @param status
	 */
	public void setStatus(TriggerStatus status) {
		this.setMsgItem("status", status.getText());
	}

	/**
	 * Set the key "priority" in request message
	 * 
	 * @param priority
	 */
	public void setPriority(String priority) {
		this.setMsgItem("priority", Trigger.formatPriority(priority));
	}

	/**
	 * Return whether altering a trigger task is executed well
	 * 
	 * @return
	 */
	public boolean isSuccess() {
		if (super.isSuccess()) {
			return true;
		}
		this.errorMsg = this.getResponse().getValue("note");
		return false;
	}

}
