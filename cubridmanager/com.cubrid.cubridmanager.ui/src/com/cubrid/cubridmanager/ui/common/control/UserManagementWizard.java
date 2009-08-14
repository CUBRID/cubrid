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
package com.cubrid.cubridmanager.ui.common.control;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.wizard.Wizard;
import org.eclipse.swt.widgets.Display;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.CasAuthType;
import com.cubrid.cubridmanager.core.common.model.ServerType;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.common.model.StatusMonitorAuthType;
import com.cubrid.cubridmanager.core.common.task.AddCMUserTask;
import com.cubrid.cubridmanager.core.common.task.ChangeCMUserPasswordTask;
import com.cubrid.cubridmanager.core.common.task.DeleteCMUserTask;
import com.cubrid.cubridmanager.core.common.task.UpdateCMUserTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DbCreateAuthType;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * CUBRID Manager user management wizard
 * 
 * @author pangqiren
 * @version 1.0 - 2009-4-23 created by pangqiren
 */
public class UserManagementWizard extends
		Wizard {

	private UserAuthGeneralInfoPage generalInfoPage = null;
	private UserAuthDbInfoPage authDbInfoPage = null;
	private CubridServer server = null;
	private ServerUserInfo userInfo = null;
	private List<ServerUserInfo> serverUserInfoList = null;
	private boolean isCanFinished = true;

	/**
	 * The constructor
	 */
	public UserManagementWizard(CubridServer server, ServerUserInfo userInfo,
			List<ServerUserInfo> serverUserInfoList) {
		if (userInfo == null)
			setWindowTitle(Messages.titleAddUser);
		else
			setWindowTitle(Messages.titleEditUser);
		this.userInfo = userInfo;
		this.server = server;
		this.serverUserInfoList = serverUserInfoList;
	}

	@Override
	public void addPages() {
		generalInfoPage = new UserAuthGeneralInfoPage(server, userInfo,
				serverUserInfoList);
		addPage(generalInfoPage);
		ServerType serverType = server.getServerInfo().getServerType();
		if (serverType == ServerType.BROKER) {
			return;
		}
		if (userInfo == null || !userInfo.isAdmin()) {
			authDbInfoPage = new UserAuthDbInfoPage(server, userInfo);
			addPage(authDbInfoPage);
		}
	}

	@Override
	public boolean canFinish() {
		ServerType serverType = server.getServerInfo().getServerType();
		if ((userInfo != null && userInfo.isAdmin())
				|| serverType == ServerType.BROKER) {
			return getContainer().getCurrentPage() == generalInfoPage;
		}
		return getContainer().getCurrentPage() == authDbInfoPage
				&& authDbInfoPage.isPageComplete();
	}

	/**
	 * Called when user clicks Finish
	 * 
	 * @return boolean
	 */
	public boolean performFinish() {
		isCanFinished = true;
		final String userId = generalInfoPage.getUserId();
		final String password = generalInfoPage.getPassword();
		final String dbCreationAuth = generalInfoPage.getDbCreationAuth();
		final String brokerAuth = generalInfoPage.getBrokerAuth();
		final String statusMonitorAuth = generalInfoPage.getStatusMonitorAuth();
		final List<DatabaseInfo> authDatabaselist = new ArrayList<DatabaseInfo>();
		TaskExecutor taskExcutor = new TaskExecutor() {
			public boolean exec(final IProgressMonitor monitor) {
				Display display = getShell().getDisplay();
				if (monitor.isCanceled()) {
					return false;
				}
				for (ITask task : taskList) {
					task.execute();
					final String msg = task.getErrorMsg();
					if (monitor.isCanceled()) {
						return false;
					}
					if (msg != null && msg.length() > 0
							&& !monitor.isCanceled()) {
						display.syncExec(new Runnable() {
							public void run() {
								CommonTool.openErrorBox(getShell(), msg);
							}
						});
						isCanFinished = false;
						return false;
					}
					if (monitor.isCanceled()) {
						isCanFinished = false;
						return false;
					}
				}
				ServerUserInfo loginedUserInfo = server.getServerInfo().getLoginedUserInfo();
				if (userInfo == null) {
					userInfo = new ServerUserInfo(userId, password);
				} else {
					userInfo.setPassword(password);
				}
				if (userInfo.getUserName().equals(loginedUserInfo.getUserName())) {
					loginedUserInfo.setPassword(password);
				}
				if (!userInfo.isAdmin()) {
					if (dbCreationAuth.equals(DbCreateAuthType.AUTH_NONE.getText()))
						userInfo.setDbCreateAuthType(DbCreateAuthType.AUTH_NONE);
					else if (dbCreationAuth.equals(DbCreateAuthType.AUTH_ADMIN.getText()))
						userInfo.setDbCreateAuthType(DbCreateAuthType.AUTH_ADMIN);
					if (brokerAuth.equals(CasAuthType.AUTH_NONE.getText()))
						userInfo.setCasAuth(CasAuthType.AUTH_NONE);
					else if (brokerAuth.equals(CasAuthType.AUTH_ADMIN.getText()))
						userInfo.setCasAuth(CasAuthType.AUTH_ADMIN);
					else if (brokerAuth.equals(CasAuthType.AUTH_MONITOR.getText()))
						userInfo.setCasAuth(CasAuthType.AUTH_MONITOR);
					if (statusMonitorAuth.equals(StatusMonitorAuthType.AUTH_NONE.getText()))
						userInfo.setStatusMonitorAuth(StatusMonitorAuthType.AUTH_NONE);
					else if (statusMonitorAuth.equals(StatusMonitorAuthType.AUTH_ADMIN.getText()))
						userInfo.setStatusMonitorAuth(StatusMonitorAuthType.AUTH_ADMIN);
					else if (statusMonitorAuth.equals(StatusMonitorAuthType.AUTH_MONITOR.getText()))
						userInfo.setStatusMonitorAuth(StatusMonitorAuthType.AUTH_MONITOR);
					userInfo.removeAllDatabaseInfo();
					userInfo.setDatabaseInfoList(authDatabaselist);
				}
				return true;
			}
		};

		if (userInfo != null && !userInfo.isAdmin()) {
			DeleteCMUserTask deleteCMUserTask = new DeleteCMUserTask(
					server.getServerInfo());
			deleteCMUserTask.setUserId(userId);
			taskExcutor.addTask(deleteCMUserTask);
		} else if (userInfo != null && userInfo.isAdmin()) {
			ChangeCMUserPasswordTask changeCMUserPasswordTask = new ChangeCMUserPasswordTask(
					server.getServerInfo());
			changeCMUserPasswordTask.setUserName(userId);
			changeCMUserPasswordTask.setPassword(password);
			taskExcutor.addTask(changeCMUserPasswordTask);
		}

		if (userInfo == null || !userInfo.isAdmin()) {
			AddCMUserTask addCMUserTask = new AddCMUserTask(
					server.getServerInfo());
			addCMUserTask.setUserId(userId);
			addCMUserTask.setPassword(password);
			addCMUserTask.setDbcreate(dbCreationAuth);
			addCMUserTask.setCasAuth(brokerAuth);
			addCMUserTask.setStautsMonitorAuth(statusMonitorAuth);
			taskExcutor.addTask(addCMUserTask);
		}
		ServerType serverType = server.getServerInfo().getServerType();
		if ((userInfo == null || !userInfo.isAdmin())
				&& (serverType == ServerType.BOTH || serverType == ServerType.DATABASE)) {
			List<Map<String, Object>> dbAuthInfoList = authDbInfoPage.getDbAuthInfoList();
			List<String> dbNameList = new ArrayList<String>();
			List<String> dbUserList = new ArrayList<String>();
			List<String> dbPasswordList = new ArrayList<String>();
			List<String> dbBrokerPortList = new ArrayList<String>();
			if (dbAuthInfoList != null && dbAuthInfoList.size() > 0) {
				int size = dbAuthInfoList.size();
				for (int i = 0; i < size; i++) {
					Map<String, Object> map = dbAuthInfoList.get(i);
					String allowConnectedStr = (String) map.get("1");
					if (allowConnectedStr.equals("Yes")) {
						String dbName = (String) map.get("0");
						dbNameList.add(dbName);
						String dbUser = (String) map.get("2");
						dbUserList.add(dbUser);
						String brokerIP = (String) map.get("3");
						String brokerPort = (String) map.get("4");
						String port = "";
						if (brokerPort.matches("^\\d+$")) {
							port = brokerPort;
						} else {
							port = brokerPort.substring(
									brokerPort.indexOf("[") + 1,
									brokerPort.indexOf("/"));
						}
						dbBrokerPortList.add(brokerIP + "," + port);
						dbPasswordList.add("");
						DatabaseInfo databaseInfo = new DatabaseInfo(dbName,
								server.getServerInfo());
						databaseInfo.setBrokerPort(brokerPort);
						databaseInfo.setBrokerIP(brokerIP);
						DbUserInfo dbUserInfo = new DbUserInfo();
						dbUserInfo.setName(dbUser);
						databaseInfo.setAuthLoginedDbUserInfo(dbUserInfo);
						authDatabaselist.add(databaseInfo);
					}
				}
			}
			String[] dbNameArr = new String[dbNameList.size()];
			String[] dbUserArr = new String[dbUserList.size()];
			String[] dbPasswordArr = new String[dbPasswordList.size()];
			String[] dbBrokerPortArr = new String[dbBrokerPortList.size()];
			UpdateCMUserTask updateTask = new UpdateCMUserTask(
					server.getServerInfo());
			updateTask.setCmUserName(userId);
			updateTask.setDbAuth(dbNameList.toArray(dbNameArr),
					dbUserList.toArray(dbUserArr),
					dbPasswordList.toArray(dbPasswordArr),
					dbBrokerPortList.toArray(dbBrokerPortArr));
			updateTask.setCasAuth(brokerAuth);
			updateTask.setDbCreator(dbCreationAuth);
			updateTask.setStatusMonitorAuth(statusMonitorAuth);
			taskExcutor.addTask(updateTask);
		}
		new ExecTaskWithProgress(taskExcutor).exec(true, true);
		return isCanFinished;
	}

	/**
	 * 
	 * Get server user information
	 * 
	 * @return
	 */
	public ServerUserInfo getServerUserInfo() {
		return this.userInfo;
	}
}