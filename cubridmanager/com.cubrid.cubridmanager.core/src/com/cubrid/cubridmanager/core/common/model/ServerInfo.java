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
package com.cubrid.cubridmanager.core.common.model;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import com.cubrid.cubridmanager.core.broker.model.BrokerInfos;
import com.cubrid.cubridmanager.core.common.ServerManager;
import com.cubrid.cubridmanager.core.common.task.MonitoringTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.OsInfoType;
import com.cubrid.cubridmanager.core.logs.model.LogInfoManager;
import com.cubrid.cubridmanager.core.query.QueryOptions;

/**
 * 
 * This class is responsible to cache CUBRID Manager server information
 * 
 * ServerInfo Description
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class ServerInfo {

	// to save server inforamton
	private String serverName = null;
	private String hostAddress = null;
	private int hostMonPort = 8001;
	private int hostJSPort = 8002;
	// to save current server login user and password
	private String userName = null;
	private String userPassword = null;
	// every server only have a montitoring task,it run continuously
	private MonitoringTask monitoringTask = null;
	// to save current server token
	private String hostToken = null;
	// to indicate whether the server connection is OK.
	private boolean isConnected = false;

	// CUBRID Manager server user information
	private ServerUserInfo loginedUserInfo = null;
	private List<ServerUserInfo> serverUserInfoList = null;
	// CUBRID Server environment information
	private EnvInfo envInfo = null;
	private ServerType serverType = ServerType.BOTH;

	private LogInfoManager logInfoManager = null;

	private Map<String, Map<String, String>> cubridConfParaMap = null;
	private Map<String, Map<String, String>> brokerConfParaMap = null;
	private Map<String, String> cmConfParaMap = null;

	private BrokerInfos brokerInfos = null;

	private List<String> allDatabaseList = null;

	public ServerInfo() {

	}

	/**
	 * 
	 * Get host address
	 * 
	 * @return
	 */
	public String getHostAddress() {
		return hostAddress;
	}

	/**
	 * 
	 * Set host address
	 * 
	 * @param hostAddress
	 */
	public void setHostAddress(String hostAddress) {
		this.hostAddress = hostAddress;
	}

	/**
	 * 
	 * Get host monitor port
	 * 
	 * @return
	 */
	public int getHostMonPort() {
		return hostMonPort;
	}

	/**
	 * 
	 * Set host monitor port
	 * 
	 * @param hostMonPort
	 */
	public void setHostMonPort(int hostMonPort) {
		this.hostMonPort = hostMonPort;
	}

	/**
	 * 
	 * Get host JS port
	 * 
	 * @return
	 */
	public int getHostJSPort() {
		return hostJSPort;
	}

	/**
	 * 
	 * Set host JS port
	 * 
	 * @param hostJSPort
	 */
	public void setHostJSPort(int hostJSPort) {
		this.hostJSPort = hostJSPort;
	}

	/**
	 * 
	 * Get host user name
	 * 
	 * @return
	 */
	public String getUserName() {
		return userName;
	}

	/**
	 * 
	 * Set host user name
	 * 
	 * @param userName
	 */
	public void setUserName(String userName) {
		this.userName = userName;
	}

	/**
	 * 
	 * Get host user password
	 * 
	 * @return
	 */
	public String getUserPassword() {
		return userPassword;
	}

	/**
	 * 
	 * Set host user password
	 * 
	 * @param userPassword
	 */
	public void setUserPassword(String userPassword) {
		this.userPassword = userPassword;
	}

	/**
	 * 
	 * Get host connected token
	 * 
	 * @return
	 */
	public String getHostToken() {
		return hostToken;
	}

	/**
	 * 
	 * Set host connected token
	 * 
	 * @param hostToken
	 */
	public void setHostToken(String hostToken) {
		this.hostToken = hostToken;
	}

	/**
	 * 
	 * Return host connected status
	 * 
	 * @return
	 */
	public boolean isConnected() {
		return isConnected;
	}

	/**
	 * 
	 * Set host connected status
	 * 
	 * @param isConnected
	 */
	public synchronized void setConnected(boolean isConnected) {
		boolean nowIsConnected = this.isConnected;
		this.isConnected = isConnected;
		if (monitoringTask != null && nowIsConnected && !isConnected) {
			monitoringTask.stopMonitor();
			monitoringTask = null;
		}
		if (!isConnected) {
			disConnect();
		}
	}

	/**
	 * 
	 * Get server name
	 * 
	 * @return
	 */
	public String getServerName() {
		return serverName;
	}

	/**
	 * 
	 * Set server name
	 * 
	 * @param serverName
	 */
	public void setServerName(String serverName) {
		this.serverName = serverName;
	}

	/**
	 * 
	 * Get monitoring task of each server
	 * 
	 * @return
	 */
	public synchronized MonitoringTask getMonitoringTask() {
		if (monitoringTask == null) {
			if (getHostAddress() != null)
				ServerManager.getInstance().addServer(getHostAddress(),
						getHostMonPort(), this);
			monitoringTask = new MonitoringTask(this);
		}
		return monitoringTask;
	}

	/**
	 * 
	 * Get logined user information
	 * 
	 * @return
	 */
	public ServerUserInfo getLoginedUserInfo() {
		return loginedUserInfo;
	}

	/**
	 * 
	 * Set logined user information
	 * 
	 * @param loginedUser
	 */
	public void setLoginedUserInfo(ServerUserInfo loginedUser) {
		this.loginedUserInfo = loginedUser;
	}

	/**
	 * 
	 * Get all CUBRID Manager server user information
	 * 
	 * @return
	 */
	public List<ServerUserInfo> getServerUserInfoList() {
		return serverUserInfoList;
	}

	/**
	 * 
	 * Get CUBRID Manager server user information by user name
	 * 
	 * @param userName
	 * @return
	 */
	public ServerUserInfo getServerUserInfo(String userName) {
		if (serverUserInfoList != null) {
			for (int i = 0; i < serverUserInfoList.size(); i++) {
				ServerUserInfo userInfo = serverUserInfoList.get(i);
				if (userInfo != null && userInfo.getUserName().equals(userName)) {
					return userInfo;
				}
			}
		}
		return null;
	}

	/**
	 * 
	 * Set CUBRID Manager server user information list
	 * 
	 * @param serverUserInfoList
	 */
	public void setServerUserInfoList(List<ServerUserInfo> serverUserInfoList) {
		this.serverUserInfoList = serverUserInfoList;
	}

	/**
	 * 
	 * Add CUBRID Manager server user information
	 * 
	 * @param serverUserInfo
	 */
	public void addServerUserInfo(ServerUserInfo serverUserInfo) {
		if (serverUserInfoList == null) {
			serverUserInfoList = new ArrayList<ServerUserInfo>();
		}
		if (serverUserInfo != null
				&& !serverUserInfoList.contains(serverUserInfo)) {
			serverUserInfoList.add(serverUserInfo);
			if (serverUserInfo.getUserName().equals(userName)) {
				setLoginedUserInfo(serverUserInfo);
			}
		}
	}

	/**
	 * 
	 * Remove CUBRID Manager server user information
	 * 
	 * @param serverUserInfo
	 */
	public void removeServerUserInfo(ServerUserInfo serverUserInfo) {
		if (serverUserInfoList != null) {
			serverUserInfoList.remove(serverUserInfo);
			if (serverUserInfo != null
					&& serverUserInfo.getUserName().equals(userName)) {
				setLoginedUserInfo(null);
			}
		}
	}

	/**
	 * 
	 * Remove all server user information
	 * 
	 */
	public void removeAllServerUserInfo() {
		if (serverUserInfoList != null)
			serverUserInfoList.clear();
	}

	/**
	 * 
	 * Get CUBRID Server env information
	 * 
	 * @return
	 */
	public EnvInfo getEnvInfo() {
		return envInfo;
	}

	/**
	 * 
	 * Set CUBRID Server env information
	 * 
	 * @param envInfo
	 */
	public void setEnvInfo(EnvInfo envInfo) {
		this.envInfo = envInfo;
	}

	/**
	 * 
	 * When server disconnect,clear all resource
	 * 
	 */
	public synchronized void disConnect() {
		setHostToken(null);
		userPassword = null;
		serverUserInfoList = null;
		allDatabaseList = null;
		setLoginedUserInfo(null);
		setEnvInfo(null);
		brokerInfos = null;
		cubridConfParaMap = null;
		brokerConfParaMap = null;
		logInfoManager = null;
	}

	/**
	 * 
	 * Get log information manager
	 * 
	 * @return
	 */
	public synchronized LogInfoManager getLogInfoManager() {
		if (logInfoManager == null)
			logInfoManager = new LogInfoManager();
		return logInfoManager;
	}

	/**
	 * 
	 * Get all broker information
	 * 
	 * @return
	 */
	public BrokerInfos getBrokerInfos() {
		return brokerInfos;
	}

	/**
	 * 
	 * Set broker information
	 * 
	 * @param brokerInfos
	 */
	public void setBrokerInfos(BrokerInfos brokerInfos) {
		this.brokerInfos = brokerInfos;
	}

	/**
	 * 
	 * Get all database list
	 * 
	 * @return
	 */
	public List<String> getAllDatabaseList() {
		return allDatabaseList;
	}

	/**
	 * 
	 * Set all database list
	 * 
	 * @param allDatabaseList
	 */
	public void setAllDatabaseList(List<String> allDatabaseList) {
		this.allDatabaseList = allDatabaseList;
	}

	/**
	 * 
	 * Remove all databases
	 * 
	 */
	public void removeAllDatabase() {
		if (this.allDatabaseList != null) {
			this.allDatabaseList.clear();
		}
	}

	/**
	 * 
	 * Add database
	 * 
	 * @param dbName
	 */
	public void addDatabase(String dbName) {
		if (this.allDatabaseList == null) {
			this.allDatabaseList = new ArrayList<String>();
		}
		this.allDatabaseList.add(dbName);
	}

	/**
	 * Return whether the server host is local
	 * 
	 * @return
	 */
	public boolean isLocalServer() {
		boolean isFlag = false;
		if (hostAddress != null && hostAddress.length() > 0) {
			InetAddress[] addrs;
			try {
				addrs = InetAddress.getAllByName(hostAddress);
				String ip = InetAddress.getLocalHost().getHostAddress();
				if (addrs.length > 0) {

					for (int i = 0; i < addrs.length; i++) {
						if (addrs[i].getHostAddress().equals(ip)
								|| addrs[i].getHostAddress().equals("127.0.0.1"))
							isFlag = true;
					}
				}
			} catch (UnknownHostException e) {
				isFlag = false;
			}
		}
		return isFlag;
	}

	/**
	 * Get server os info
	 * 
	 * @return
	 */
	public OsInfoType getServerOsInfo() {

		if (envInfo == null)
			return null;
		String osInfo = envInfo.getOsInfo();
		if (OsInfoType.NT.getText().equalsIgnoreCase(osInfo))
			return OsInfoType.NT;
		if (OsInfoType.LINUX.getText().equalsIgnoreCase(osInfo))
			return OsInfoType.LINUX;
		if (OsInfoType.UNIX.getText().equalsIgnoreCase(osInfo))
			return OsInfoType.UNIX;
		if (OsInfoType.UNKNOWN.getText().equalsIgnoreCase(osInfo))
			return OsInfoType.UNKNOWN;
		return null;

	}

	/**
	 * get the path Separator of the server system
	 * 
	 * @return
	 */
	public String getPathSeparator() {
		if (getServerOsInfo() == OsInfoType.NT)
			return "\\";
		else
			return "/";
	}

	/**
	 * 
	 * Get cubrid.conf file all parameters
	 * 
	 * @return
	 */
	public Map<String, Map<String, String>> getCubridConfParaMap() {
		return cubridConfParaMap;
	}

	/**
	 * 
	 * Get parameter in cubrid.conf file
	 * 
	 * @param para
	 * @param databaseName
	 * @return
	 */
	public String getCubridConfPara(String para, String databaseName) {
		if (null != cubridConfParaMap) {
			Map<String, String> map = cubridConfParaMap.get("service");
			String ret = "";
			if (map != null) {
				ret = map.get(para);
				if (null != ret) {
					return ret;
				}
			}
			if (databaseName != null && databaseName.trim().length() > 0) {
				map = cubridConfParaMap.get("[@" + databaseName + "]");
				if (map != null) {
					ret = map.get(para);
					if (null != ret) {
						return ret;
					}
				}
			}
			map = cubridConfParaMap.get("common");
			if (map != null) {
				ret = map.get(para);
				if (null != ret) {
					return ret;
				}
			}
			for (String[] strs : ConfConstants.dbBaseParameters) {
				if (strs[0].equals(para)) {
					return strs[1];
				}
			}
			for (String[] strs : ConfConstants.dbAdvancedParameters) {
				if (strs[0].equals(para)) {
					return strs[2];
				}
			}
		}
		return null;
	}

	/**
	 * 
	 * Set cubrid.conf parameters
	 * 
	 * @param cubridConfParaMap
	 */
	public void setCubridConfParaMap(
			Map<String, Map<String, String>> cubridConfParaMap) {
		this.cubridConfParaMap = cubridConfParaMap;
	}

	/**
	 * 
	 * Get cubrid_broker.conf all parameters
	 * 
	 * @return
	 */
	public Map<String, Map<String, String>> getBrokerConfParaMap() {
		return brokerConfParaMap;
	}

	/**
	 * 
	 * Set cubrid_broker.conf parameters
	 * 
	 * @param brokerConfParaMap
	 */
	public void setBrokerConfParaMap(
			Map<String, Map<String, String>> brokerConfParaMap) {
		this.brokerConfParaMap = brokerConfParaMap;
	}

	/**
	 * 
	 * Get cm.conf all parameters
	 * 
	 * @return
	 */
	public Map<String, String> getCmConfParaMap() {
		return cmConfParaMap;
	}

	/**
	 * 
	 * Set cubrid_broker.conf parameters
	 * 
	 * @param cmConfParaMap
	 */
	public void setCmConfParaMap(Map<String, String> cmConfParaMap) {
		this.cmConfParaMap = cmConfParaMap;
	}

	/**
	 * 
	 * Get the driver path of this server
	 * 
	 * @return
	 */
	public String getDriverPath() {
		return QueryOptions.getDriverPath(this);
	}

	/**
	 * 
	 * Return the server type of this server
	 * 
	 * @return
	 */
	public ServerType getServerType() {
		return serverType;
	}

	/**
	 * 
	 * Set the server type of this server
	 * 
	 * @param serverType
	 */
	public void setServerType(ServerType serverType) {
		this.serverType = serverType;
	}

}
