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
package com.cubrid.cubridmanager.ui.spi.model;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;

/**
 * 
 * CUBRID Server node
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridServer extends
		DefaultCubridNode {

	private String connectedIconPath;

	/**
	 * The constructor
	 * 
	 * @param id
	 * @param label
	 * @param disconnectedIconPath
	 * @param connectedIconPath
	 */
	public CubridServer(String id, String label, String disconnectedIconPath,
			String connectedIconPath) {
		super(id, label, disconnectedIconPath);
		setConnectedIconPath(connectedIconPath);
		setType(CubridNodeType.SERVER);
		setRoot(true);
		setServer(this);
		setContainer(true);
	}

	/**
	 * 
	 * Get CUBRID Manager server information
	 * 
	 * @return
	 */
	public ServerInfo getServerInfo() {
		if (this.getAdapter(ServerInfo.class) != null)
			return (ServerInfo) this.getAdapter(ServerInfo.class);
		return null;
	}

	/**
	 * 
	 * Set CUBRID Manager server information
	 * 
	 * @param serverInfo
	 */
	public void setServerInfo(ServerInfo serverInfo) {
		this.setModelObj(serverInfo);
	}

	/**
	 * 
	 * Return whether server is connected
	 * 
	 * @return
	 */
	public boolean isConnected() {
		return getServerInfo() != null ? getServerInfo().isConnected() : false;
	}

	/**
	 * 
	 * Get logined user name
	 * 
	 * @return
	 */
	public String getUserName() {
		return getServerInfo() != null ? getServerInfo().getUserName() : null;
	}

	/**
	 * 
	 * Get logined password
	 * 
	 * @return
	 */
	public String getPassword() {
		return getServerInfo() != null ? getServerInfo().getUserPassword()
				: null;
	}

	/**
	 * 
	 * Get server name
	 * 
	 * @return
	 */
	public String getServerName() {
		return getServerInfo() != null ? getServerInfo().getServerName() : null;
	}

	/**
	 * 
	 * Get host address
	 * 
	 * @return
	 */
	public String getHostAddress() {
		return getServerInfo() != null ? getServerInfo().getHostAddress()
				: null;
	}

	/**
	 * 
	 * Get host monitor port
	 * 
	 * @return
	 */
	public String getMonPort() {
		return getServerInfo() != null ? String.valueOf(getServerInfo().getHostMonPort())
				: null;
	}

	/**
	 * 
	 * Get host JS port
	 * 
	 * @return
	 */
	public String getJSPort() {
		return getServerInfo() != null ? String.valueOf(getServerInfo().getHostJSPort())
				: null;
	}

	/**
	 * 
	 * Get server connected status icon path
	 * 
	 * @return
	 */
	public String getConnectedIconPath() {
		return connectedIconPath;
	}

	/**
	 * 
	 * Set server connected status icon path
	 * 
	 * @param connectedIconPath
	 */
	public void setConnectedIconPath(String connectedIconPath) {
		this.connectedIconPath = connectedIconPath;
	}

	/**
	 * 
	 * Get server disconnected status icon path
	 * 
	 * @return
	 */
	public String getDisConnectedIconPath() {
		return this.getIconPath();
	}

	/**
	 * 
	 * Set server disconnected status icon path
	 * 
	 * @param disConnectedIconPath
	 */
	public void setDisConnectedIconPath(String disConnectedIconPath) {
		this.setIconPath(disConnectedIconPath);
	}
}
