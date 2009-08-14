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
package com.cubrid.cubridmanager.ui.common.dialog;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.broker.model.BrokerInfo;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfoList;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfos;
import com.cubrid.cubridmanager.core.broker.task.GetBrokerConfParameterTask;
import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.ServerManager;
import com.cubrid.cubridmanager.core.common.model.ConfConstants;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.model.ServerType;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.common.task.GetCMConfParameterTask;
import com.cubrid.cubridmanager.core.common.task.GetCMUserListTask;
import com.cubrid.cubridmanager.core.common.task.GetCubridConfParameterTask;
import com.cubrid.cubridmanager.core.common.task.GetEnvInfoTask;
import com.cubrid.cubridmanager.core.common.task.MonitoringTask;
import com.cubrid.cubridmanager.core.common.task.UpdateCMUserTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.query.QueryOptions;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.Version;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * This dialog is responsible to add and edit host information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class HostDialog extends
		CMTitleAreaDialog implements
		ModifyListener {
	public static int CONNECT_ID = 0;
	public static int ADD_ID = 2;

	private Text hostNameText = null;
	private Text addressText = null;
	private Text portText = null;
	private Text userNameText = null;
	private Text passwordText = null;
	private CubridServer server = null;
	private ServerInfo serverInfo = null;

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public HostDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		if (server == null)
			getHelpSystem().setHelp(parent, CubridManagerHelpContextIDs.hostAdd);
		else
			getHelpSystem().setHelp(parent,
					CubridManagerHelpContextIDs.hostConnect);

		Composite parentComp = (Composite) super.createDialogArea(parent);
		Composite composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(CommonTool.createGridData(GridData.FILL_BOTH,
				1, 1, -1, -1));
		GridLayout layout = new GridLayout();
		layout.numColumns = 2;
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);

		Label hostNameLabel = new Label(composite, SWT.LEFT);
		hostNameLabel.setText(Messages.lblHostName);
		hostNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		hostNameText = new Text(composite, SWT.LEFT | SWT.BORDER);
		hostNameText.setTextLimit(ValidateUtil.MAX_NAME_LENGTH);
		if (server != null)
			hostNameText.setText(server.getLabel());
		hostNameText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		hostNameText.addModifyListener(this);

		Label addressNameLabel = new Label(composite, SWT.LEFT);
		addressNameLabel.setText(Messages.lblAddress);

		addressNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		addressText = new Text(composite, SWT.LEFT | SWT.BORDER);
		if (server != null)
			addressText.setText(server.getHostAddress());
		addressText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		addressText.addModifyListener(this);

		Label portNameLabel = new Label(composite, SWT.LEFT);
		portNameLabel.setText(Messages.lblPort);
		portNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		portText = new Text(composite, SWT.LEFT | SWT.BORDER);
		portText.setTextLimit(5);
		if (server != null)
			portText.setText(server.getMonPort());
		else {
			portText.setText("8001");
		}
		portText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		portText.addModifyListener(this);

		Label userNameLabel = new Label(composite, SWT.LEFT);
		userNameLabel.setText(Messages.lblUserName);
		userNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		userNameText = new Text(composite, SWT.LEFT | SWT.BORDER);
		userNameText.setTextLimit(ValidateUtil.MAX_NAME_LENGTH);
		if (server != null)
			userNameText.setText(server.getUserName());
		userNameText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		userNameText.addModifyListener(this);

		Label passwordLabel = new Label(composite, SWT.LEFT);
		passwordLabel.setText(Messages.lblPassword);
		passwordLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		passwordText = new Text(composite, SWT.LEFT | SWT.PASSWORD | SWT.BORDER);
		passwordText.setTextLimit(ValidateUtil.MAX_NAME_LENGTH);
		passwordText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (server != null) {
			passwordText.setFocus();
		}
		if (server == null) {
			setTitle(Messages.titleAddHostDialog);
			setMessage(Messages.msgAddHostDialog);
		} else {
			setTitle(Messages.titleConnectHostDialog);
			setMessage(Messages.msgConnectHostDialog);
		}
		return parentComp;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		getShell().setSize(400, 350);
		CommonTool.centerShell(getShell());
		if (server == null)
			getShell().setText(Messages.titleAddHostDialog);
		else
			getShell().setText(Messages.titleConnectHostDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		if (server == null) {
			createButton(parent, ADD_ID, Messages.btnAddHost, true);
		}
		createButton(parent, CONNECT_ID, Messages.btnConnectHost, true);
		if (server == null) {
			getButton(ADD_ID).setToolTipText(Messages.btnAddHost);
			getButton(ADD_ID).setEnabled(false);
			getButton(CONNECT_ID).setToolTipText(Messages.tipConnectHostButton1);
			getButton(CONNECT_ID).setEnabled(false);
		} else {
			getButton(CONNECT_ID).setToolTipText(Messages.tipConnectHostButton2);
		}
		createButton(parent, IDialogConstants.CANCEL_ID, Messages.btnCancel,
				false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == HostDialog.ADD_ID || buttonId == HostDialog.CONNECT_ID) {
			String hostName = hostNameText.getText();
			String address = addressText.getText();
			String port = portText.getText();
			String userName = userNameText.getText();
			String password = passwordText.getText();

			serverInfo = new ServerInfo();
			serverInfo.setServerName(hostName);
			serverInfo.setHostAddress(address);
			serverInfo.setHostMonPort(Integer.parseInt(port));
			serverInfo.setHostJSPort(Integer.parseInt(port) + 1);
			serverInfo.setUserName(userName);
			serverInfo.setUserPassword(password);
		}
		if (buttonId == HostDialog.CONNECT_ID) {
			connect(buttonId);
		} else {
			setReturnCode(buttonId);
			close();
		}
	}

	/**
	 * 
	 * Execute task and connect the host
	 * 
	 * @param buttonId
	 */
	private void connect(final int buttonId) {
		//check admin user author database list,admin can access all database
		final List<DatabaseInfo> authDatabaseList = new ArrayList<DatabaseInfo>();
		TaskExecutor taskExcutor = new TaskExecutor() {
			@SuppressWarnings("unchecked")
			public boolean exec(final IProgressMonitor monitor) {
				Display display = getShell().getDisplay();
				if (monitor.isCanceled()) {
					return false;
				}
				boolean isRunUpdateCmUserTask = false;
				BrokerInfos brokerInfos = null;
				for (ITask task : taskList) {
					if (task instanceof MonitoringTask) {
						MonitoringTask monitoringTask = (MonitoringTask) task;
						serverInfo = monitoringTask.connectServer(Version.releaseVersion);
						if (serverInfo.isConnected()
								&& serverInfo.getUserName() != null
								&& serverInfo.getUserName().equals("admin")
								&& serverInfo.getUserPassword().equals("admin")) {
							display.syncExec(new Runnable() {
								public void run() {
									ChangePasswordDialog dialog = new ChangePasswordDialog(
											null, true);
									dialog.setServerInfo(serverInfo);
									dialog.open();
								}
							});
						}
					} else if (task instanceof GetEnvInfoTask) {
						GetEnvInfoTask getEnvInfoTask = (GetEnvInfoTask) task;
						getEnvInfoTask.loadEnvInfo();
					} else if ((task instanceof UpdateCMUserTask)) {
						if (isRunUpdateCmUserTask) {
							UpdateCMUserTask updateCMUserTask = (UpdateCMUserTask) task;
							if (serverInfo != null && serverInfo.isConnected()) {
								ServerUserInfo userInfo = serverInfo.getLoginedUserInfo();
								updateCMUserTask.setCasAuth(userInfo.getCasAuth().getText());
								updateCMUserTask.setDbCreator(userInfo.getDbCreateAuthType().getText());
								updateCMUserTask.setStatusMonitorAuth(userInfo.getStatusMonitorAuth().getText());

								List<String> dbNameList = new ArrayList<String>();
								List<String> dbUserList = new ArrayList<String>();
								List<String> dbPasswordList = new ArrayList<String>();
								List<String> dbBrokerPortList = new ArrayList<String>();
								if (authDatabaseList != null
										&& authDatabaseList.size() > 0) {
									int size = authDatabaseList.size();
									for (int i = 0; i < size; i++) {
										DatabaseInfo databaseInfo = authDatabaseList.get(i);
										dbNameList.add(databaseInfo.getDbName());
										dbUserList.add(databaseInfo.getAuthLoginedDbUserInfo().getName());
										dbBrokerPortList.add(QueryOptions.getBrokerIp(databaseInfo)
												+ ","
												+ databaseInfo.getBrokerPort());
										String password = databaseInfo.getAuthLoginedDbUserInfo().getNoEncryptPassword();
										dbPasswordList.add(password == null ? ""
												: password);
									}
								}
								String[] dbNameArr = new String[dbNameList.size()];
								String[] dbUserArr = new String[dbUserList.size()];
								String[] dbPasswordArr = new String[dbPasswordList.size()];
								String[] dbBrokerPortArr = new String[dbBrokerPortList.size()];
								updateCMUserTask.setDbAuth(
										dbNameList.toArray(dbNameArr),
										dbUserList.toArray(dbUserArr),
										dbPasswordList.toArray(dbPasswordArr),
										dbBrokerPortList.toArray(dbBrokerPortArr));
								updateCMUserTask.execute();
							}
						}
					} else {
						task.execute();
					}
					final String msg = task.getErrorMsg();
					if (monitor.isCanceled()) {
						ServerManager.getInstance().setConnected(
								serverInfo.getHostAddress(),
								serverInfo.getHostMonPort(), false);
						return false;
					}
					if (msg != null && msg.length() > 0
							&& !monitor.isCanceled()) {
						display.syncExec(new Runnable() {
							public void run() {
								CommonTool.openErrorBox(getShell(), msg);
							}
						});
						ServerManager.getInstance().setConnected(
								serverInfo.getHostAddress(),
								serverInfo.getHostMonPort(), false);
						return false;
					}
					if (task instanceof GetCMConfParameterTask) {
						GetCMConfParameterTask getCMConfParameterTask = (GetCMConfParameterTask) task;
						Map<String, String> confParameters = getCMConfParameterTask.getConfParameters();
						ServerType serverType = ServerType.BOTH;
						if (confParameters != null) {
							String target = confParameters.get(ConfConstants.CM_TARGET);
							if (target != null) {
								if (target.indexOf("broker") >= 0
										&& target.indexOf("server") >= 0) {
									serverType = ServerType.BOTH;
								} else if (target.indexOf("broker") >= 0) {
									serverType = ServerType.BROKER;
								} else if (target.indexOf("server") >= 0) {
									serverType = ServerType.DATABASE;
								}
							}
						}
						if (serverInfo != null)
							serverInfo.setServerType(serverType);
					}
					if (task instanceof CommonQueryTask) {
						CommonQueryTask<BrokerInfos> getBrokerTask = (CommonQueryTask<BrokerInfos>) task;
						brokerInfos = getBrokerTask.getResultModel();
						if (serverInfo != null)
							serverInfo.setBrokerInfos(brokerInfos);
					}
					if (task instanceof GetCMUserListTask) {
						if (serverInfo != null && serverInfo.isConnected()) {
							GetCMUserListTask getUserInfoTask = (GetCMUserListTask) task;
							List<ServerUserInfo> serverUserInfoList = getUserInfoTask.getServerUserInfoList();
							for (int i = 0; serverUserInfoList != null
									&& i < serverUserInfoList.size(); i++) {
								ServerUserInfo userInfo = serverUserInfoList.get(i);
								if (userInfo != null
										&& userInfo.getUserName().equals(
												serverInfo.getUserName())) {
									serverInfo.setLoginedUserInfo(userInfo);
									break;
								}
							}
							//Defaultly not all database lies in admin user authorization(cmdb.pass) list,hence add them
							if (serverInfo.getLoginedUserInfo() != null
									&& serverInfo.getLoginedUserInfo().isAdmin()) {
								List<DatabaseInfo> databaseInfoList = serverInfo.getLoginedUserInfo().getDatabaseInfoList();
								if (databaseInfoList != null)
									authDatabaseList.addAll(databaseInfoList);
								List<String> databaseList = serverInfo.getAllDatabaseList();
								for (int i = 0; databaseList != null
										&& i < databaseList.size(); i++) {
									String dbName = databaseList.get(i);
									boolean isExist = false;
									for (int j = 0; databaseInfoList != null
											&& j < databaseInfoList.size(); j++) {
										String name = databaseInfoList.get(j).getDbName();
										if (dbName.equals(name)) {
											isExist = true;
											break;
										}
									}
									if (!isExist) {
										DatabaseInfo databaseInfo = new DatabaseInfo(
												dbName, serverInfo);
										authDatabaseList.add(databaseInfo);
										isRunUpdateCmUserTask = true;
									}
								}
								//set database default broker port and default login user for CUBRID Manager user admin
								String defaultBrokerPort = "30000";
								if (brokerInfos != null) {
									BrokerInfoList brokerInfoList = brokerInfos.getBorkerInfoList();
									if (brokerInfoList != null) {
										List<BrokerInfo> list = brokerInfoList.getBrokerInfoList();
										for (int i = 0; list != null
												&& i < list.size(); i++) {
											BrokerInfo brokerInfo = list.get(i);
											if (i == 0) {
												defaultBrokerPort = brokerInfo.getPort();
											}
											if (brokerInfo.getName().equals(
													"query_editor")) {
												defaultBrokerPort = brokerInfo.getPort();
												break;
											}
										}
									}
								}
								for (int i = 0; i < authDatabaseList.size(); i++) {
									DatabaseInfo databaseInfo = authDatabaseList.get(i);
									if (databaseInfo.getBrokerPort() == null
											|| databaseInfo.getBrokerPort().trim().length() <= 0) {
										databaseInfo.setBrokerPort(defaultBrokerPort);
										isRunUpdateCmUserTask = true;
									}
									if (databaseInfo.getAuthLoginedDbUserInfo() == null) {
										DbUserInfo userInfo = new DbUserInfo(
												databaseInfo.getDbName(),
												"dba", "", "", true);
										databaseInfo.setAuthLoginedDbUserInfo(userInfo);
										isRunUpdateCmUserTask = true;
									}
								}
							}
						}
					}
					if (task instanceof GetCubridConfParameterTask) {
						GetCubridConfParameterTask getCubridConfParameterTask = (GetCubridConfParameterTask) task;
						Map<String, Map<String, String>> confParas = getCubridConfParameterTask.getConfParameters();
						if (serverInfo != null)
							serverInfo.setCubridConfParaMap(confParas);
					}
					if (task instanceof GetBrokerConfParameterTask) {
						GetBrokerConfParameterTask getBrokerConfParameterTask = (GetBrokerConfParameterTask) task;
						Map<String, Map<String, String>> confParas = getBrokerConfParameterTask.getConfParameters();
						if (serverInfo != null)
							serverInfo.setBrokerConfParaMap(confParas);
					}
					if (monitor.isCanceled()) {
						ServerManager.getInstance().setConnected(
								serverInfo.getHostAddress(),
								serverInfo.getHostMonPort(), false);
						return false;
					}
				}
				if (!monitor.isCanceled()) {
					display.syncExec(new Runnable() {
						public void run() {
							setReturnCode(buttonId);
							close();
						}
					});
				}
				return true;
			}
		};
		MonitoringTask monitoringTask = serverInfo.getMonitoringTask();
		GetEnvInfoTask getEnvInfoTask = new GetEnvInfoTask(serverInfo);
		GetCMConfParameterTask getCMConfParameterTask = new GetCMConfParameterTask(
				serverInfo);
		CommonQueryTask<BrokerInfos> getBrokerTask = new CommonQueryTask<BrokerInfos>(
				serverInfo, CommonSendMsg.commonSimpleSendMsg,
				new BrokerInfos());
		GetCMUserListTask getUserInfoTask = new GetCMUserListTask(serverInfo);
		UpdateCMUserTask updateTask = new UpdateCMUserTask(serverInfo);
		updateTask.setCmUserName(serverInfo.getUserName());
		GetCubridConfParameterTask getCubridConfParameterTask = new GetCubridConfParameterTask(
				serverInfo);
		GetBrokerConfParameterTask getBrokerConfParameterTask = new GetBrokerConfParameterTask(
				serverInfo);
		taskExcutor.addTask(monitoringTask);
		taskExcutor.addTask(getEnvInfoTask);
		taskExcutor.addTask(getCMConfParameterTask);
		taskExcutor.addTask(getBrokerTask);
		taskExcutor.addTask(getUserInfoTask);
		taskExcutor.addTask(updateTask);
		taskExcutor.addTask(getCubridConfParameterTask);
		taskExcutor.addTask(getBrokerConfParameterTask);
		new ExecTaskWithProgress(taskExcutor).exec(true, true);
		if (passwordText != null && !passwordText.isDisposed()) {
			passwordText.selectAll();
			passwordText.setFocus();
		}
	}

	/**
	 * When modify the page content and check the validation
	 */
	public void modifyText(ModifyEvent e) {
		String hostName = hostNameText.getText();
		boolean isValidHostName = hostName.indexOf(" ") < 0
				&& hostName.trim().length() >= 4
				&& hostName.trim().length() <= ValidateUtil.MAX_NAME_LENGTH;
		boolean isHostExist = CubridNodeManager.getInstance().isContainedByName(
				hostName, server);

		String address = addressText.getText();
		boolean isValidAddress = address.indexOf(" ") < 0
				&& address.trim().length() > 0;
		String port = portText.getText();
		boolean isValidPort = ValidateUtil.isNumber(port);
		if (isValidPort) {
			int portVal = Integer.parseInt(port);
			if (portVal < 1024 || portVal > 65535) {
				isValidPort = false;
			}
		}
		boolean isAddressExist = false;
		if (isValidPort)
			isAddressExist = CubridNodeManager.getInstance().isContainedByHostAddress(
					address, port, server);

		String userName = userNameText.getText();
		boolean isValidUserName = userName.indexOf(" ") < 0
				&& userName.trim().length() >= 4
				&& userName.trim().length() <= ValidateUtil.MAX_NAME_LENGTH;

		if (!isValidHostName) {
			setErrorMessage(Messages.errHostName);
		} else if (isHostExist) {
			setErrorMessage(Messages.errHostExist);
		} else if (!isValidAddress) {
			setErrorMessage(Messages.errAddress);
		} else if (!isValidPort) {
			setErrorMessage(Messages.errPort);
		} else if (isAddressExist) {
			setErrorMessage(Messages.errAddressExist);
		} else if (!isValidUserName) {
			setErrorMessage(Messages.errUserName);
		} else {
			setErrorMessage(null);
		}
		boolean isEnabled = isValidHostName && !isHostExist && isValidAddress
				&& !isAddressExist && isValidPort && isValidUserName;
		if (server == null)
			getButton(ADD_ID).setEnabled(isEnabled);
		getButton(CONNECT_ID).setEnabled(isEnabled);

	}

	/**
	 * 
	 * Get added CubridServer
	 * 
	 * @return
	 */
	public CubridServer getServer() {
		return server;
	}

	/**
	 * 
	 * Set edited CubridServer
	 * 
	 * @param server
	 */
	public void setServer(CubridServer server) {
		this.server = server;
	}

	/**
	 * 
	 * Return serverInfo
	 * 
	 * @return
	 */
	public ServerInfo getServerInfo() {
		return this.serverInfo;
	}
}
