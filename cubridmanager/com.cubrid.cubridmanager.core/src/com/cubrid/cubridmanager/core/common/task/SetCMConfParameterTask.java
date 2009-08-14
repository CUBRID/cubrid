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

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import com.cubrid.cubridmanager.core.common.model.ConfComments;
import com.cubrid.cubridmanager.core.common.model.ConfConstants;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;

/**
 * 
 * This task is responsible to set cm.conf parameter
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-2 created by pangqiren
 */
public class SetCMConfParameterTask extends
		SocketTask {
	private static final String[] sendedMsgItems = new String[] { "task",
			"token", "confname", "confdata" };

	/**
	 * The constructor
	 * 
	 * @param taskName
	 * @param serverInfo
	 */
	public SetCMConfParameterTask(ServerInfo serverInfo) {
		super("setsysparam", serverInfo, sendedMsgItems);
		this.setMsgItem("confname", "cmconf");
	}

	/**
	 * 
	 * Set cm.conf parameters
	 * 
	 * @param confParameters
	 */
	public void setConfParameters(Map<String, String> confParameters) {
		List<String> confDatas = new ArrayList<String>();
		confDatas.add("");
		confDatas.add("#");
		ConfComments.addComments(confDatas,
				ConfComments.cubrid_copyright_comments);
		confDatas.add("");
		confDatas.add("#");
		if (confParameters != null) {
			for (int i = 0; i < ConfConstants.cmParameters.length; i++) {
				String key = ConfConstants.cmParameters[i][0];
				String value = confParameters.get(key);
				if (value != null && value.trim().length() > 0) {
					confDatas.add(key + " " + value);
				}
			}
		}
		String[] confDataArr = new String[confDatas.size()];
		confDatas.toArray(confDataArr);
		this.setMsgItem("confdata", confDataArr);
	}

}
