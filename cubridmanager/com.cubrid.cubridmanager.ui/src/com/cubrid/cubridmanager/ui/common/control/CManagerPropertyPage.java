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

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

import org.eclipse.jface.preference.PreferencePage;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.common.model.ConfConstants;
import com.cubrid.cubridmanager.core.common.task.SetCMConfParameterTask;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;

/**
 * 
 * CUBRID Manager property page show cm.conf configuraiton parameter
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-4 created by pangqiren
 */
public class CManagerPropertyPage extends
		PreferencePage implements
		ModifyListener {

	private ICubridNode node = null;
	private boolean isAdmin = false;
	private Text cmPortText;
	private Text monitorIntervalText;
	private Button allowUserMultiConButton;
	private Button executeDiagButton;
	private Text queryTimeText;
	private Map<String, String> initialValueMap = new HashMap<String, String>();
	private Map<String, String> defaultValueMap = new HashMap<String, String>();
	private Button autoStartBrokerButton;
	private boolean isChanged = false;
	private boolean isApply = false;

	/**
	 * The constructor
	 * 
	 * @param node
	 * @param name
	 */
	public CManagerPropertyPage(ICubridNode node, String name) {
		super(name, null);
		noDefaultAndApplyButton();
		this.node = node;
		if (this.node != null
				&& this.node.getServer().getServerInfo().getLoginedUserInfo().isAdmin()) {
			isAdmin = true;
		}
	}

	/**
	 * Creates the page content
	 */
	protected Control createContents(Composite parent) {
		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		whs.setHelp(parent, CubridManagerHelpContextIDs.managerProperty);

		Composite composite = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		composite.setLayout(layout);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));

		createGeneralGroup(composite);
		createDiagnosticsGroup(composite);

		initial();
		return composite;
	}

	/**
	 * 
	 * Create the general group composite
	 * 
	 * @param parent
	 */
	private void createGeneralGroup(Composite parent) {
		Group generalInfoGroup = new Group(parent, SWT.NONE);
		generalInfoGroup.setText(Messages.grpGeneral);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		generalInfoGroup.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		layout.numColumns = 2;
		generalInfoGroup.setLayout(layout);

		Label cmPortLabel = new Label(generalInfoGroup, SWT.LEFT);
		cmPortLabel.setText(ConfConstants.cm_port + ":");
		cmPortLabel.setLayoutData(CommonTool.createGridData(1, 1, 150, -1));
		cmPortText = new Text(generalInfoGroup, SWT.LEFT | SWT.BORDER);
		cmPortText.setTextLimit(5);
		cmPortText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (!isAdmin) {
			cmPortText.setEditable(false);
		}

		Label monitorIntervalLabel = new Label(generalInfoGroup, SWT.LEFT);
		monitorIntervalLabel.setText(ConfConstants.monitor_interval + ":");
		monitorIntervalLabel.setLayoutData(CommonTool.createGridData(1, 1, 150,
				-1));
		monitorIntervalText = new Text(generalInfoGroup, SWT.LEFT | SWT.BORDER);
		monitorIntervalText.setTextLimit(8);
		monitorIntervalText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (!isAdmin) {
			monitorIntervalText.setEditable(false);
		}

		allowUserMultiConButton = new Button(generalInfoGroup, SWT.CHECK
				| SWT.LEFT);
		allowUserMultiConButton.setText(ConfConstants.allow_user_multi_connection);
		allowUserMultiConButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		if (!isAdmin) {
			allowUserMultiConButton.setEnabled(false);
		}

		autoStartBrokerButton = new Button(generalInfoGroup, SWT.CHECK
				| SWT.LEFT);
		autoStartBrokerButton.setText(ConfConstants.auto_start_broker);
		autoStartBrokerButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		if (!isAdmin) {
			autoStartBrokerButton.setEnabled(false);
		}
	}

	/**
	 * 
	 * Create diagnostics group composite
	 * 
	 * @param parent
	 */
	private void createDiagnosticsGroup(Composite parent) {
		Group diagInfoGroup = new Group(parent, SWT.NONE);
		diagInfoGroup.setText(Messages.grpDiagnositics);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		diagInfoGroup.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		layout.numColumns = 2;
		diagInfoGroup.setLayout(layout);

		executeDiagButton = new Button(diagInfoGroup, SWT.CHECK | SWT.LEFT);
		executeDiagButton.setText(ConfConstants.execute_diag);
		executeDiagButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		if (!isAdmin) {
			executeDiagButton.setEnabled(false);
		}

		Label queryTimeLabel = new Label(diagInfoGroup, SWT.LEFT);
		queryTimeLabel.setText(ConfConstants.server_long_query_time + ":");
		queryTimeLabel.setLayoutData(CommonTool.createGridData(1, 1, 150, -1));
		queryTimeText = new Text(diagInfoGroup, SWT.LEFT | SWT.BORDER);
		queryTimeText.setTextLimit(8);
		queryTimeText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (!isAdmin) {
			queryTimeText.setEditable(false);
		}
	}

	/**
	 * 
	 * initial the page content
	 * 
	 */
	private void initial() {
		Map<String, String> confParaMap = node.getServer().getServerInfo().getCmConfParaMap();
		if (confParaMap != null) {
			Iterator<Map.Entry<String, String>> it = confParaMap.entrySet().iterator();
			while (it.hasNext()) {
				Map.Entry<String, String> entry = it.next();
				String key = entry.getKey();
				String value = entry.getValue();
				initialValueMap.put(key, value);
			}
		}
		for (int i = 0; i < ConfConstants.cmParameters.length; i++) {
			defaultValueMap.put(ConfConstants.cmParameters[i][0],
					ConfConstants.cmParameters[i][2]);
		}
		defaultValue();
		cmPortText.addModifyListener(this);
		monitorIntervalText.addModifyListener(this);
		queryTimeText.addModifyListener(this);
	}

	/**
	 * 
	 * Restore the default value
	 * 
	 */
	private void defaultValue() {
		if (initialValueMap != null) {
			cmPortText.setText(defaultValueMap.get(ConfConstants.cm_port));
			String cmPort = initialValueMap.get(ConfConstants.cm_port);
			if (cmPort != null)
				cmPortText.setText(cmPort);

			monitorIntervalText.setText(defaultValueMap.get(ConfConstants.monitor_interval));
			String monitorInterval = initialValueMap.get(ConfConstants.monitor_interval);
			if (monitorInterval != null)
				monitorIntervalText.setText(monitorInterval);

			allowUserMultiConButton.setSelection(defaultValueMap.get(
					ConfConstants.allow_user_multi_connection).equals("YES"));
			String allowMultiConn = initialValueMap.get(ConfConstants.allow_user_multi_connection);
			if (allowMultiConn != null)
				allowUserMultiConButton.setSelection(allowMultiConn.equals("YES"));

			autoStartBrokerButton.setSelection(defaultValueMap.get(
					ConfConstants.auto_start_broker).equals("YES"));
			String autoStartBroker = initialValueMap.get(ConfConstants.auto_start_broker);
			if (autoStartBroker != null)
				autoStartBrokerButton.setSelection(autoStartBroker.equals("YES"));

			executeDiagButton.setSelection(defaultValueMap.get(
					ConfConstants.execute_diag).equals("ON"));
			String executeDiag = initialValueMap.get(ConfConstants.execute_diag);
			if (executeDiag != null)
				executeDiagButton.setSelection(executeDiag.equals("ON"));

			queryTimeText.setText(defaultValueMap.get(ConfConstants.server_long_query_time));
			String serverLongQueryTime = initialValueMap.get(ConfConstants.server_long_query_time);
			if (serverLongQueryTime != null)
				queryTimeText.setText(serverLongQueryTime);
		}
	}

	/**
	 * When modify,check the validation
	 */
	public void modifyText(ModifyEvent e) {
		String port = cmPortText.getText();
		boolean isValidPort = ValidateUtil.isInteger(port);
		if (isValidPort) {
			int intValue = Integer.parseInt(port);
			if (intValue > 65535 || intValue < 1024) {
				isValidPort = false;
			}
		}
		if (!isValidPort) {
			setErrorMessage(Messages.errCmPort);
			setValid(false);
			return;
		}
		String monitorInterval = monitorIntervalText.getText();
		boolean isValidMonitorInterval = ValidateUtil.isInteger(monitorInterval);
		if (isValidMonitorInterval) {
			int intValue = Integer.parseInt(monitorInterval);
			if (intValue < 1) {
				isValidMonitorInterval = false;
			}
		}
		if (!isValidMonitorInterval) {
			setErrorMessage(Messages.errMonitorInterval);
			setValid(false);
			return;
		}
		String queryTime = queryTimeText.getText();
		boolean isValidQueryTime = ValidateUtil.isInteger(queryTime);
		if (isValidQueryTime) {
			int intValue = Integer.parseInt(queryTime);
			if (intValue < 0) {
				isValidQueryTime = false;
			}
		}
		if (!isValidQueryTime) {
			setErrorMessage(Messages.errServerLongQueryTime);
			setValid(false);
			return;
		}
		boolean isValid = isValidPort && isValidMonitorInterval
				&& isValidQueryTime;
		if (isValid) {
			setErrorMessage(null);
		}
		setValid(isValid);
	}

	@Override
	protected void performDefaults() {
		defaultValue();
		if (isApply) {
			perform(initialValueMap);
		}
		isChanged = false;
		isApply = false;
	}

	@Override
	public boolean performOk() {
		if (cmPortText == null || cmPortText.isDisposed()) {
			return true;
		}
		if (!isAdmin) {
			return true;
		}
		Map<String, String> confParaMap = node.getServer().getServerInfo().getCmConfParaMap();
		String port = cmPortText.getText();
		if (isChanged(ConfConstants.cm_port, port)) {
			confParaMap.put(ConfConstants.cm_port, port);
			isChanged = true;
		}
		String monitorInterval = monitorIntervalText.getText();
		if (isChanged(ConfConstants.monitor_interval, monitorInterval)) {
			confParaMap.put(ConfConstants.monitor_interval, monitorInterval);
			isChanged = true;
		}
		boolean isAllowUserMultiConn = allowUserMultiConButton.getSelection();
		if (isChanged(ConfConstants.allow_user_multi_connection,
				isAllowUserMultiConn ? "YES" : "NO")) {
			confParaMap.put(ConfConstants.allow_user_multi_connection,
					isAllowUserMultiConn ? "YES" : "NO");
			isChanged = true;
		}
		boolean isAutoStartBroker = autoStartBrokerButton.getSelection();
		if (isChanged(ConfConstants.auto_start_broker,
				isAutoStartBroker ? "YES" : "NO")) {
			confParaMap.put(ConfConstants.auto_start_broker,
					isAutoStartBroker ? "YES" : "NO");
			isChanged = true;
		}
		boolean isExecuteDialg = executeDiagButton.getSelection();
		if (isChanged(ConfConstants.execute_diag, isExecuteDialg ? "ON" : "OFF")) {
			confParaMap.put(ConfConstants.execute_diag, isExecuteDialg ? "ON"
					: "OFF");
			isChanged = true;
		}
		String queryTime = queryTimeText.getText();
		if (isChanged(ConfConstants.server_long_query_time, queryTime)) {
			confParaMap.put(ConfConstants.server_long_query_time, queryTime);
			isChanged = true;
		}
		if (!isChanged) {
			return true;
		}
		perform(confParaMap);
		isChanged = false;
		isApply = true;
		return true;
	}

	/**
	 * 
	 * Perform task and set parameter to cm.conf
	 * 
	 * @param confParaMap
	 */
	private void perform(Map<String, String> confParaMap) {
		CommonTaskExec taskExcutor = new CommonTaskExec();
		SetCMConfParameterTask task = new SetCMConfParameterTask(
				node.getServer().getServerInfo());
		task.setConfParameters(confParaMap);
		taskExcutor.addTask(task);
		new ExecTaskWithProgress(taskExcutor).exec(true, true);
		if (taskExcutor.isSuccess()) {
			CommonTool.openInformationBox(Messages.titleSuccess,
					Messages.msgChangeCMParaSuccess);
		}
	}

	/**
	 * 
	 * Test whether has modify
	 * 
	 * @param paraName
	 * @param uiValue
	 * @return
	 */
	private boolean isChanged(String paraName, String uiValue) {
		String initialValue = initialValueMap.get(paraName);
		String defaultValue = defaultValueMap.get(paraName);
		if (initialValue == null && !uiValue.equals(defaultValue)) {
			return true;
		}
		if (initialValue != null && !uiValue.equals(initialValue)) {
			return true;
		}
		return false;
	}
}
