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

package com.cubrid.cubridmanager.core.common;

import java.util.HashMap;
import java.util.Map;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;

/**
 * 
 * This class is responsible to manage all CUBRID Manager server
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class ServerManager {

	private static ServerManager manager = new ServerManager();
	private Map<String, ServerInfo> serverInfoMap = new HashMap<String, ServerInfo>();

	/**
	 * The private constructor
	 */
	private ServerManager() {
	}

	/**
	 * 
	 * Return the only CUBRID Manager server manager instance
	 * 
	 * @return
	 */
	public static ServerManager getInstance() {
		return manager;
	}

	/**
	 * 
	 * Return connected status of server
	 * 
	 * @param hostAddress
	 * @param port
	 * @return
	 */
	public boolean isConnected(String hostAddress, int port) {
		ServerInfo serverInfo = getServer(hostAddress, port);
		if (serverInfo == null)
			return false;
		return serverInfo.isConnected();
	}

	/**
	 * 
	 * Set connected status of server
	 * 
	 * @param hostAddress
	 * @param port
	 * @param isConnected
	 */
	public synchronized void setConnected(String hostAddress, int port,
			boolean isConnected) {
		ServerInfo serverInfo = getServer(hostAddress, port);
		if (serverInfo == null)
			return;
		serverInfo.setConnected(isConnected);
		if (!isConnected) {
			serverInfoMap.remove(hostAddress + ":" + port);
		}
	}

	/**
	 * 
	 * Get CUBRID Manager server information
	 * 
	 * @param serverName
	 * @param port
	 * @return
	 */
	public ServerInfo getServer(String hostAddress, int port) {
		return serverInfoMap.get(hostAddress + ":" + port);
	}

	/**
	 * 
	 * Remove CUBRID Manager server
	 * 
	 * @param hostAddress
	 * @param port
	 */
	public synchronized void removeServer(String hostAddress, int port) {
		setConnected(hostAddress, port, false);
		serverInfoMap.remove(hostAddress + ":" + port);
	}

	/**
	 * 
	 * Add CUBRID Manager server information
	 * 
	 * @param serverName
	 * @param port
	 * @param value
	 * @return
	 */
	public synchronized ServerInfo addServer(String hostAddress, int port,
			ServerInfo value) {
		return serverInfoMap.put(hostAddress + ":" + port, value);
	}

}
