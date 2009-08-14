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

import com.cubrid.cubridmanager.core.Messages;
import com.cubrid.cubridmanager.core.common.model.EnvInfo;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

/**
 * 
 * This task is responsible to load CUBRID Server environment information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class GetEnvInfoTask extends
		SocketTask {
	private static final String[] sendedMsgItems = new String[] { "task",
			"token" };

	/**
	 * The constructor
	 * 
	 * @param taskName
	 * @param serverInfo
	 */
	public GetEnvInfoTask(ServerInfo serverInfo) {
		super("getenv", serverInfo, sendedMsgItems);
	}

	/**
	 * 
	 * Load CUBRID Server environment information
	 * 
	 */
	public EnvInfo loadEnvInfo() {
		execute();
		TreeNode response = getResponse();
		if (response == null
				|| (this.getErrorMsg() != null && getErrorMsg().trim().length() > 0)) {
			if (this.getErrorMsg() == null
					|| getErrorMsg().trim().length() <= 0) {
				errorMsg = Messages.error_invalidRequest;
			}
			return null;
		}
		String rootDir = response.getValue("CUBRID");
		String databaseDir = response.getValue("CUBRID_DATABASES");
		String cmServerDir = response.getValue("CUBRID_DBMT");
		String serverVersion = response.getValue("CUBRIDVER");
		String brokerVersion = response.getValue("BROKERVER");
		int i = 0;
		List<String> hostMonTabStatusList = new ArrayList<String>();
		while (true) {
			String value = response.getValue("HOSTMONTAB" + i);
			if (value == null) {
				break;
			} else if (value.trim().equals("ON") || value.trim().equals("OFF")) {
				hostMonTabStatusList.add(value.trim());
			} else {
				break;
			}
			i++;
		}
		String osInfo = response.getValue("osinfo");
		EnvInfo envInfo = new EnvInfo();
		envInfo.setRootDir(rootDir);
		envInfo.setDatabaseDir(databaseDir);
		envInfo.setCmServerDir(cmServerDir);
		envInfo.setServerVersion(serverVersion);
		envInfo.setBrokerVersion(brokerVersion);
		envInfo.setOsInfo(osInfo);
		int size = hostMonTabStatusList.size();
		String[] hostMonTabArr = new String[size];
		envInfo.setHostMonTabStatus(hostMonTabStatusList.toArray(hostMonTabArr));
		serverInfo.setEnvInfo(envInfo);
		return envInfo;
	}

	/**
	 * Cancel this task
	 */
	public void cancel() {
		serverInfo.setEnvInfo(null);
		super.cancel();
	}
}
