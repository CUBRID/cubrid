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

import com.cubrid.cubridmanager.core.common.model.CasAuthType;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.common.model.StatusMonitorAuthType;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DbCreateAuthType;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;

/**
 * 
 * This task is responsible to get CUBRID Manager user information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class GetCMUserListTask extends
		SocketTask {
	private static final String[] sendedMsgItems = new String[] { "task",
			"token" };

	/**
	 * The constructor
	 * 
	 * @param taskName
	 * @param serverInfo
	 */
	public GetCMUserListTask(ServerInfo serverInfo) {
		super("getdbmtuserinfo", serverInfo, sendedMsgItems);
	}

	/**
	 * 
	 * Get server user information list
	 * 
	 */
	public List<ServerUserInfo> getServerUserInfoList() {
		TreeNode response = getResponse();
		if (response == null
				|| (this.getErrorMsg() != null && getErrorMsg().trim().length() > 0)) {
			return null;
		}
		List<ServerUserInfo> serverUserInfoList = new ArrayList<ServerUserInfo>();
		for (int i = 0; i < response.childrenSize(); i++) {
			TreeNode node1 = response.getChildren().get(i);
			if (node1.getValue("open") == null) {
				continue;
			}
			if (node1.getValue("open").trim().equals("dblist")) {
				serverInfo.removeAllDatabase();
				String[] dbNames = node1.getValues("dbname");
				for (int m = 0; dbNames != null && m < dbNames.length; m++) {
					serverInfo.addDatabase(dbNames[m]);
				}
			} else if (node1.getValue("open").trim().equals("userlist")) {
				for (int j = 0; j < node1.childrenSize(); j++) {
					TreeNode node1_1 = node1.getChildren().get(j);
					if (node1_1.getValue("open") == null) {
						continue;
					}
					if (node1_1.getValue("open").trim().equals("user")) {
						String userId = node1_1.getValue("id");
						if (userId == null) {
							continue;
						}
						String password = node1_1.getValue("passwd");
						String casAuthInfo = node1_1.getValue("casauth");
						String dbCreater = node1_1.getValue("dbcreate");
						String statusMonitorAuthInfo = node1_1.getValue("statusmonitorauth");
						ServerUserInfo userInfo = new ServerUserInfo(userId,
								password);

						CasAuthType casAuthType = CasAuthType.AUTH_NONE;
						if (casAuthInfo != null
								&& casAuthInfo.trim().equals(
										CasAuthType.AUTH_ADMIN.getText())) {
							casAuthType = CasAuthType.AUTH_ADMIN;
						} else if (casAuthInfo != null
								&& casAuthInfo.trim().equals(
										CasAuthType.AUTH_MONITOR.getText())) {
							casAuthType = CasAuthType.AUTH_MONITOR;
						}
						userInfo.setCasAuth(casAuthType);

						StatusMonitorAuthType statusMonitorAuth = StatusMonitorAuthType.AUTH_NONE;
						if (statusMonitorAuthInfo != null
								&& statusMonitorAuthInfo.trim().equals(
										StatusMonitorAuthType.AUTH_ADMIN.getText())) {
							statusMonitorAuth = StatusMonitorAuthType.AUTH_ADMIN;
						} else if (statusMonitorAuthInfo != null
								&& statusMonitorAuthInfo.trim().equals(
										StatusMonitorAuthType.AUTH_MONITOR.getText())) {
							statusMonitorAuth = StatusMonitorAuthType.AUTH_MONITOR;
						}
						userInfo.setStatusMonitorAuth(statusMonitorAuth);
						if (userInfo.isAdmin()) {
							userInfo.setStatusMonitorAuth(StatusMonitorAuthType.AUTH_ADMIN);
						}

						if (dbCreater != null
								&& dbCreater.equals(DbCreateAuthType.AUTH_ADMIN.getText())) {
							userInfo.setDbCreateAuthType(DbCreateAuthType.AUTH_ADMIN);
						} else {
							userInfo.setDbCreateAuthType(DbCreateAuthType.AUTH_NONE);
						}
						for (int k = 0; k < node1_1.childrenSize(); k++) {
							TreeNode node1_1_1 = node1_1.getChildren().get(k);
							if (node1_1_1.getValue("open") == null) {
								continue;
							}
							if (node1_1_1.getValue("open").trim().equals(
									"dbauth")) {
								String[] dbNameArr = node1_1_1.getValues("dbname");
								String[] dbUserIdArr = node1_1_1.getValues("dbid");
								String[] dbPasswordArr = node1_1_1.getValues("dbpasswd");
								String[] dbBrokerAddressArr = node1_1_1.getValues("dbbrokeraddress");
								for (int n = 0; dbNameArr != null
										&& n < dbNameArr.length; n++) {
									DatabaseInfo databaseInfo = new DatabaseInfo(
											dbNameArr[n], serverInfo);
									DbUserInfo databaseUserInfo = new DbUserInfo();
									if (dbUserIdArr != null
											&& dbUserIdArr.length > n) {
										databaseUserInfo.setName(dbUserIdArr[n]);
										if (dbUserIdArr[n].equals("dba")) {
											databaseUserInfo.setDbaAuthority(true);
										}
									}
									if (dbPasswordArr != null
											&& dbPasswordArr.length > n) {
										databaseUserInfo.setNoEncryptPassword(dbPasswordArr[n]);
									}

									if (dbBrokerAddressArr != null
											&& dbBrokerAddressArr.length > n) {
										String dbBroker = dbBrokerAddressArr[n];
										String brokerIp = "";
										String brokerPort = "";
										String[] dbBrokerArr = dbBroker.split(",");
										if (dbBrokerArr != null) {
											if (dbBrokerArr.length == 2) {
												brokerIp = dbBrokerArr[0];
												brokerPort = dbBrokerArr[1];
											}
										}
										databaseInfo.setBrokerIP(brokerIp);
										databaseInfo.setBrokerPort(brokerPort);
									}
									databaseInfo.setAuthLoginedDbUserInfo(databaseUserInfo);
									userInfo.addDatabaseInfo(databaseInfo);
								}
							}
						}
						serverUserInfoList.add(userInfo);
					}
				}
			}
		}
		return serverUserInfoList;
	}

	/**
	 * Cancel this task
	 */
	public void cancel() {
		serverInfo.setLoginedUserInfo(null);
		super.cancel();
	}
}
