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

import java.util.List;

import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.common.model.CasAuthType;
import com.cubrid.cubridmanager.core.common.model.ServerType;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.common.model.StatusMonitorAuthType;
import com.cubrid.cubridmanager.core.cubrid.database.model.DbCreateAuthType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;

/**
 * 
 * CUBRID Manager user general information page
 * 
 * UserAuthGeneralInfoPage Description
 * 
 * @author pangqiren
 * @version 1.0 - 2009-4-23 created by pangqiren
 */
public class UserAuthGeneralInfoPage extends
		WizardPage implements
		ModifyListener {

	public static String PAGENAME = UserAuthGeneralInfoPage.class.getName();
	private Text userIdText;
	private Text passwordText;
	private Text passwordConfirmText;
	private Combo dbCreationAuthCombo;
	private Combo brokerAuthCombo;
	private CubridServer server = null;
	private ServerUserInfo userInfo = null;
	private List<ServerUserInfo> serverUserInfoList = null;
	private Combo statusMonitorAuthCombo;

	/**
	 * The constructor
	 */
	public UserAuthGeneralInfoPage(CubridServer server,
			ServerUserInfo userInfo, List<ServerUserInfo> serverUserInfoLis) {
		super(PAGENAME);
		this.server = server;
		this.userInfo = userInfo;
		setPageComplete(false);
		this.serverUserInfoList = serverUserInfoLis;
	}

	/**
	 * Create the control for this page
	 */
	public void createControl(Composite parent) {
		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		if (userInfo == null)
			whs.setHelp(parent, CubridManagerHelpContextIDs.userAdd);
		else
			whs.setHelp(parent, CubridManagerHelpContextIDs.userEdit);

		Composite composite = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.numColumns = 3;
		layout.marginHeight = 10;
		layout.marginWidth = 10;
		composite.setLayout(layout);
		GridData gridData = new GridData(GridData.FILL_BOTH);
		composite.setLayoutData(gridData);

		Label userIdLabel = new Label(composite, SWT.LEFT);
		userIdLabel.setText(Messages.lblUserName);
		userIdLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		userIdText = new Text(composite, SWT.LEFT | SWT.BORDER);
		userIdText.setTextLimit(32);
		userIdText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		userIdText.addModifyListener(this);

		Label passwordLabel = new Label(composite, SWT.LEFT);
		passwordLabel.setText(Messages.lblPassword);
		passwordLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		passwordText = new Text(composite, SWT.LEFT | SWT.PASSWORD | SWT.BORDER);
		passwordText.setTextLimit(32);
		passwordText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		passwordText.addModifyListener(this);

		Label newPasswordLabel = new Label(composite, SWT.LEFT);
		newPasswordLabel.setText(Messages.lblPasswordConfirm);
		newPasswordLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		passwordConfirmText = new Text(composite, SWT.LEFT | SWT.PASSWORD
				| SWT.BORDER);
		passwordConfirmText.setTextLimit(32);
		passwordConfirmText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		passwordConfirmText.addModifyListener(this);
		ServerType serverType = server.getServerInfo().getServerType();
		if (serverType == ServerType.BOTH || serverType == ServerType.DATABASE) {
			Label dbCreationAuthLabel = new Label(composite, SWT.LEFT);
			dbCreationAuthLabel.setText(Messages.lblDbAuth);
			dbCreationAuthLabel.setLayoutData(CommonTool.createGridData(1, 1,
					-1, -1));
			dbCreationAuthCombo = new Combo(composite, SWT.LEFT | SWT.BORDER
					| SWT.READ_ONLY);
			dbCreationAuthCombo.setLayoutData(CommonTool.createGridData(
					GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		}
		if (serverType == ServerType.BOTH || serverType == ServerType.BROKER) {
			Label brokerLabel = new Label(composite, SWT.LEFT);
			brokerLabel.setText(Messages.lblBrokerAuth);
			brokerLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
			brokerAuthCombo = new Combo(composite, SWT.LEFT | SWT.BORDER
					| SWT.READ_ONLY);
			brokerAuthCombo.setLayoutData(CommonTool.createGridData(
					GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		}

		Label statusMonitorLabel = new Label(composite, SWT.LEFT);
		statusMonitorLabel.setText(Messages.lblMonitorAuth);
		statusMonitorLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		statusMonitorAuthCombo = new Combo(composite, SWT.LEFT | SWT.BORDER
				| SWT.READ_ONLY);
		statusMonitorAuthCombo.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		initial();
		if (userInfo == null) {
			setTitle(Messages.titleAddUser);
			setMessage(Messages.msgAddUser);
		} else {
			setTitle(Messages.titleEditUser);
			setMessage(Messages.msgEidtUser);
		}
		setControl(composite);
	}

	/**
	 * 
	 * Initial the page content
	 */
	private void initial() {
		ServerType serverType = server.getServerInfo().getServerType();
		if (userInfo == null || !userInfo.isAdmin()) {
			if (serverType == ServerType.BOTH
					|| serverType == ServerType.DATABASE) {
				dbCreationAuthCombo.add(DbCreateAuthType.AUTH_NONE.getText());
			}
			if (serverType == ServerType.BOTH
					|| serverType == ServerType.BROKER) {
				brokerAuthCombo.setItems(new String[] {
						CasAuthType.AUTH_NONE.getText(),
						CasAuthType.AUTH_MONITOR.getText(),
						CasAuthType.AUTH_ADMIN.getText() });
			}
			statusMonitorAuthCombo.setItems(new String[] {
					StatusMonitorAuthType.AUTH_NONE.getText(),
					StatusMonitorAuthType.AUTH_MONITOR.getText(),
					StatusMonitorAuthType.AUTH_ADMIN.getText() });
		} else if (userInfo.isAdmin()) {
			if (serverType == ServerType.BOTH
					|| serverType == ServerType.DATABASE) {
				dbCreationAuthCombo.add(DbCreateAuthType.AUTH_ADMIN.getText());
			}
			if (serverType == ServerType.BOTH
					|| serverType == ServerType.BROKER) {
				brokerAuthCombo.add(CasAuthType.AUTH_ADMIN.getText());
			}
			statusMonitorAuthCombo.add(StatusMonitorAuthType.AUTH_ADMIN.getText());
		}

		if (userInfo != null) {
			userIdText.setText(userInfo.getUserName());
			userIdText.setEditable(false);
			passwordText.setText(userInfo.getPassword());
			passwordConfirmText.setText(userInfo.getPassword());
			if (serverType == ServerType.BOTH
					|| serverType == ServerType.DATABASE) {
				dbCreationAuthCombo.setText(userInfo.getDbCreateAuthType().getText());
			}
			if (serverType == ServerType.BOTH
					|| serverType == ServerType.BROKER) {
				brokerAuthCombo.setText(userInfo.getCasAuth().getText());
			}
			statusMonitorAuthCombo.setText(userInfo.getStatusMonitorAuth().getText());
		} else {
			if (serverType == ServerType.BOTH
					|| serverType == ServerType.DATABASE) {
				dbCreationAuthCombo.select(0);
			}
			if (serverType == ServerType.BOTH
					|| serverType == ServerType.BROKER) {
				brokerAuthCombo.select(0);
			}
			statusMonitorAuthCombo.select(0);
		}
	}

	/**
	 * When modify the page content,check the validation
	 */
	public void modifyText(ModifyEvent e) {
		if (userIdText == null || passwordText == null
				|| passwordConfirmText == null) {
			return;
		}
		String userId = userIdText.getText();
		String password = passwordText.getText();
		String passwordConfirm = passwordConfirmText.getText();
		boolean isUserExist = false;
		if (userInfo == null && serverUserInfoList != null) {
			for (int i = 0; i < serverUserInfoList.size(); i++) {
				ServerUserInfo user = serverUserInfoList.get(i);
				if (user.getUserName().equals(userId)) {
					isUserExist = true;
					break;
				}
			}
		}
		boolean isValidUserId = userId.indexOf(" ") < 0
				&& userId.trim().length() >= 4 && userId.trim().length() <= 32;
		boolean isValidPassword = password.indexOf(" ") < 0
				&& !password.equals("admin") && password.trim().length() >= 4
				&& password.trim().length() <= 32;
		boolean isValidPasswordConfirm = passwordConfirm.indexOf(" ") < 0
				&& !passwordConfirm.equals("admin")
				&& passwordConfirm.trim().length() >= 4
				&& passwordConfirm.trim().length() <= 32;
		boolean isEqualPassword = password.equals(passwordConfirm);
		if (!isValidUserId) {
			setErrorMessage(Messages.errUserId);
		} else if (isUserExist) {
			setErrorMessage(Messages.errUserExist);
		} else if (!isValidPassword) {
			setErrorMessage(Messages.errPassword);
		} else if (!isValidPasswordConfirm) {
			setErrorMessage(Messages.errPasswordConfirm);
		} else if (!isEqualPassword) {
			setErrorMessage(Messages.errNotEqualPassword);
		}
		boolean isEnabled = isValidUserId && !isUserExist && isValidPassword
				&& isValidPasswordConfirm && isEqualPassword;
		if (isEnabled) {
			setErrorMessage(null);
		}
		setPageComplete(isEnabled);
	}

	/**
	 * 
	 * Get CubridServer
	 * 
	 * @return
	 */
	public CubridServer getServer() {
		return server;
	}

	/**
	 * 
	 * Set CubridServer
	 * 
	 * @param server
	 */
	public void setServer(CubridServer server) {
		this.server = server;
	}

	/**
	 * 
	 * Get user id
	 * 
	 * @return
	 */
	public String getUserId() {
		return userIdText.getText();
	}

	/**
	 * 
	 * Get user password
	 * 
	 * @return
	 */
	public String getPassword() {
		return passwordText.getText();
	}

	/**
	 * 
	 * Get database creation auth
	 * 
	 * @return
	 */
	public String getDbCreationAuth() {
		ServerType serverType = server.getServerInfo().getServerType();
		if (serverType == ServerType.BOTH || serverType == ServerType.DATABASE) {
			return dbCreationAuthCombo.getText();
		} else {
			return DbCreateAuthType.AUTH_NONE.getText();
		}
	}

	/**
	 * 
	 * Get broker auth
	 * 
	 * @return
	 */
	public String getBrokerAuth() {
		ServerType serverType = server.getServerInfo().getServerType();
		if (serverType == ServerType.BOTH || serverType == ServerType.BROKER) {
			return brokerAuthCombo.getText();
		} else {
			return CasAuthType.AUTH_NONE.getText();
		}

	}

	/**
	 * 
	 * Get status monitor auth
	 * 
	 * @return
	 */
	public String getStatusMonitorAuth() {
		return statusMonitorAuthCombo.getText();
	}
}
