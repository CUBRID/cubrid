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
package com.cubrid.cubridmanager.core.monitoring.task;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.monitoring.model.StatusTemplateInfo;
import com.cubrid.cubridmanager.core.monitoring.model.TargetConfigInfo;

/**
 * A task that defined the task of "addstatustemplate"
 * 
 * @author lizhiqiang 2009-4-29
 */
public class AddStatusTemplateTask extends SocketTask {

	private StatusTemplateInfo statusTemplateInfo;

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public AddStatusTemplateTask(ServerInfo serverInfo) {
		super("addstatustemplate", serverInfo);
	}

	/**
	 * Builds a message which includes the items exclusive of task,token and
	 * dbname
	 * 
	 * @param list
	 * @return
	 */
	public void buildMsg() {
		StringBuffer msg = new StringBuffer();
		msg.append("name:" + statusTemplateInfo.getName());
		msg.append("\n");
		msg.append("desc:" + statusTemplateInfo.getDesc());
		msg.append("\n");
		msg.append("db_name:" + statusTemplateInfo.getDb_name());
		msg.append("\n");
		msg.append("sampling_term:" + statusTemplateInfo.getSampling_term());
		msg.append("\n");
		msg.append("open:target_config\n");
		for (TargetConfigInfo targetConfigInfo : statusTemplateInfo.getTargetConfigInfoList()) {
			for(String[] strings :targetConfigInfo.getList()){
				if(null!=strings){
					msg.append(strings[0]+":");
					msg.append(strings[1] + " " + strings[2]);
					msg.append("\n");
				}
			}
		}
		msg.append("close:target_config\n");
		appendSendMsg = msg.toString();
	}

	public void setStatusTemplateInfo(StatusTemplateInfo statusTemplateInfo) {
		this.statusTemplateInfo = statusTemplateInfo;
	}

}
