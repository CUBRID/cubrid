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

import com.cubrid.cubridmanager.core.Messages;
import com.cubrid.cubridmanager.core.common.ServerManager;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

/**
 * 
 * This task is responsible to communite with CUBRID Manager server by
 * monitoring port.because every server only permit to have a monitroing
 * socket,hence this task is responsible to handle with all requests which use
 * this monitoring port.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class MonitoringTask extends
		SocketTask {
	private final String[] connectServerMsgItems = new String[] { "id",
			"password", "clientver" };

	private boolean isConnectServerRunning = false;

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public MonitoringTask(ServerInfo serverInfo) {
		super(null, serverInfo, null);
		setUsingMonPort(true);
		setNeedMultiSend(true);
	}

	/**
	 * Connect CUBRID Manager server
	 * 
	 * @return
	 */
	public ServerInfo connectServer(String clientVersion) {
		// test whether server is running
		if (getClientSocket() != null
				&& getClientSocket().getHeartbitThread() != null) {
			return serverInfo;
		}
		isConnectServerRunning = true;
		clearMsgItems();
		setOrders(connectServerMsgItems);
		setMsgItem("id", serverInfo.getUserName());
		setMsgItem("password", serverInfo.getUserPassword());
		setMsgItem("clientver", clientVersion);
		setNeedServerConnected(false);
		execute();
		TreeNode node = getResponse();
		if (node == null
				|| (getErrorMsg() != null && getErrorMsg().trim().length() > 0)) {
			finish();
			return serverInfo;
		}
		String token = node.getValue("token");
		if (token != null && token.trim().length() > 0)
			ServerManager.getInstance().setConnected(
					serverInfo.getHostAddress(), serverInfo.getHostMonPort(),
					true);
		else {
			errorMsg = Messages.error_invalidToken;
			finish();
			return serverInfo;
		}
		serverInfo.setHostToken(token);
		getClientSocket().setHeartbeat(1000);
		isConnectServerRunning = false;
		return serverInfo;
	}

	/**
	 * Cancel this task
	 */
	public void cancel() {
		if (isConnectServerRunning) {
			super.cancel();
			getClientSocket().stopHeartbitThread();
			ServerManager.getInstance().setConnected(
					serverInfo.getHostAddress(), serverInfo.getHostMonPort(),
					false);
		} else {
			getClientSocket().stopRead();
		}
	}

	/**
	 * 
	 * Stop montioring
	 * 
	 */
	public void stopMonitor() {
		getClientSocket().stopRead();
		getClientSocket().tearDownConnection();
		getClientSocket().stopHeartbitThread();
	}
}
