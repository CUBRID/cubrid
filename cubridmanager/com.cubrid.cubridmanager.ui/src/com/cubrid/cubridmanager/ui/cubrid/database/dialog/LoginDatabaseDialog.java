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
package com.cubrid.cubridmanager.ui.cubrid.database.dialog;

import java.util.ArrayList;
import java.util.List;

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

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.common.task.UpdateCMUserTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.database.task.LoginDatabaseTask;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.query.QueryOptions;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * The dialog is used to login database
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class LoginDatabaseDialog extends
		CMTitleAreaDialog implements
		ModifyListener {
	private Text userNameText = null;
	private Text passwordText = null;
	private CubridDatabase database = null;

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public LoginDatabaseDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		getHelpSystem().setHelp(parent,
				CubridManagerHelpContextIDs.databaseUser);
		Composite parentComp = (Composite) super.createDialogArea(parent);
		Composite composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.numColumns = 3;
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);

		Label userNameLabel = new Label(composite, SWT.LEFT);
		userNameLabel.setText(Messages.lblDbUserName);
		userNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		userNameText = new Text(composite, SWT.LEFT | SWT.BORDER);
		if (database != null && database.getUserName() != null)
			userNameText.setText(database.getUserName());
		userNameText.addModifyListener(this);
		userNameText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, 100, -1));

		Label passwordLabel = new Label(composite, SWT.LEFT);
		passwordLabel.setText(Messages.lblDbPassword);
		passwordLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		passwordText = new Text(composite, SWT.LEFT | SWT.PASSWORD | SWT.BORDER);
		passwordText.setTextLimit(ValidateUtil.MAX_PASSWORD_LENGTH);
		passwordText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, 100, -1));

		if (database != null && database.getUserName() != null) {
			passwordText.setFocus();
		} else {
			userNameText.setFocus();
		}
		setTitle(Messages.titleLoginDbDialog);
		setMessage(Messages.msgLoginDbDialog);
		return parentComp;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		getShell().setSize(400, 240);
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleLoginDbDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);
		if (database == null || database.getUserName() == null
				|| database.getUserName().trim().length() <= 0) {
			getButton(IDialogConstants.OK_ID).setEnabled(false);
		}
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			connect(buttonId);
		} else
			super.buttonPressed(buttonId);
	}

	/**
	 * 
	 * Execute task and login database
	 * 
	 * @param buttonId
	 */
	private void connect(final int buttonId) {
		final String dbUser = userNameText.getText();
		final String dbPassword = passwordText.getText();
		TaskExecutor taskExcutor = new TaskExecutor() {
			public boolean exec(final IProgressMonitor monitor) {
				Display display = getShell().getDisplay();
				if (monitor.isCanceled()) {
					return false;
				}
				DbUserInfo dbUserInfo = null;
				DbUserInfo preDbUserInfo = database.getDatabaseInfo().getAuthLoginedDbUserInfo();
				for (ITask task : taskList) {
					if (task instanceof UpdateCMUserTask) {
						UpdateCMUserTask updateCMUserTask = (UpdateCMUserTask) task;
						ServerInfo serverInfo = database.getServer().getServerInfo();
						if (serverInfo != null && serverInfo.isConnected()) {
							ServerUserInfo userInfo = serverInfo.getLoginedUserInfo();
							updateCMUserTask.setCasAuth(userInfo.getCasAuth().getText());
							updateCMUserTask.setDbCreator(userInfo.getDbCreateAuthType().getText());
							updateCMUserTask.setStatusMonitorAuth(userInfo.getStatusMonitorAuth().getText());
							List<String> dbNameList = new ArrayList<String>();
							List<String> dbUserList = new ArrayList<String>();
							List<String> dbPasswordList = new ArrayList<String>();
							List<String> dbBrokerPortList = new ArrayList<String>();
							List<DatabaseInfo> authDatabaseList = userInfo.getDatabaseInfoList();
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
						}
					}
					task.execute();
					final String msg = task.getErrorMsg();
					if (msg != null && msg.length() > 0
							&& !monitor.isCanceled()) {
						database.setLogined(false);
						database.getDatabaseInfo().setAuthLoginedDbUserInfo(
								preDbUserInfo);
						display.syncExec(new Runnable() {
							public void run() {
								CommonTool.openErrorBox(getShell(), msg);
							}
						});
						return false;
					}
					if (monitor.isCanceled()) {
						return false;
					}
					if (task instanceof LoginDatabaseTask) {
						dbUserInfo = ((LoginDatabaseTask) task).getLoginedDbUserInfo();
						database.setLogined(true);
						dbUserInfo.setNoEncryptPassword(dbPassword);
						database.getDatabaseInfo().setAuthLoginedDbUserInfo(
								dbUserInfo);
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
		CubridServer server = database.getServer();
		ServerInfo serverInfo = server.getServerInfo();
		LoginDatabaseTask loginDatabaseTask = new LoginDatabaseTask(serverInfo);
		loginDatabaseTask.setCMUser(server.getUserName());
		loginDatabaseTask.setDbName(database.getLabel());
		loginDatabaseTask.setDbUser(dbUser);
		loginDatabaseTask.setDbPassword(dbPassword);
		UpdateCMUserTask updateCMUserTask = new UpdateCMUserTask(serverInfo);
		updateCMUserTask.setCmUserName(server.getUserName());
		taskExcutor.addTask(loginDatabaseTask);
		taskExcutor.addTask(updateCMUserTask);
		new ExecTaskWithProgress(taskExcutor).exec(true, true);
		if (!loginDatabaseTask.isSuccess() && passwordText != null
				&& !passwordText.isDisposed()) {
			passwordText.selectAll();
			passwordText.setFocus();
		}
	}

	public void modifyText(ModifyEvent e) {
		String userName = userNameText.getText();
		boolean isValidUserName = userName.trim().length() > 0
				&& userName.indexOf(" ") < 0;
		ServerUserInfo userInfo = database.getServer().getServerInfo().getLoginedUserInfo();
		if (userInfo != null && !userInfo.isAdmin() && isValidUserName) {
			isValidUserName = userName.equalsIgnoreCase(database.getUserName());
		}
		if (!isValidUserName) {
			setErrorMessage(Messages.errUserName);
			if (userNameText != null && !userNameText.isDisposed()) {
				userNameText.selectAll();
				userNameText.setFocus();
			}
		}
		boolean isEnabled = isValidUserName;
		if (isEnabled) {
			setErrorMessage(null);
		}
		getButton(IDialogConstants.OK_ID).setEnabled(isEnabled);
	}

	/**
	 * 
	 * Get CUBRID Database
	 * 
	 * @return
	 */
	public CubridDatabase getDatabase() {
		return database;
	}

	/**
	 * 
	 * Set CUBRID Database
	 * 
	 * @param database
	 */
	public void setDatabase(CubridDatabase database) {
		this.database = database;
	}
}
