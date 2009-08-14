/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */
package com.cubrid.cubridmanager.ui.query.dialog;

import java.util.List;

import org.eclipse.core.runtime.Preferences;
import org.eclipse.jface.preference.PreferencePage;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.IWorkbench;
import org.eclipse.ui.IWorkbenchPreferencePage;

import com.cubrid.cubridmanager.core.CubridManagerCorePlugin;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfo;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfoList;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfos;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.query.QueryOptions;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * query options page for database node
 * 
 * @author wangsl 2009-5-11
 */
public class Query4DatabasePreferencePage extends
		PreferencePage implements
		IWorkbenchPreferencePage {

	private Combo brokerPortCombo;
	private CubridDatabase database;
	private Button charSetBtn;
	private Combo charsetText;
	private String oldBrokerPort = "";
	private String oldCharSet = "";
	private String oldBrokerIp = "";
	private Text brokerIpText = null;

	public Query4DatabasePreferencePage(CubridDatabase database, String name) {
		super(name);
		noDefaultAndApplyButton();
		this.database = database;
	}

	@Override
	public boolean performOk() {
		if (charsetText == null || charsetText.isDisposed()) {
			return true;
		}
		boolean isChanged = false;
		if (!brokerIpText.getText().equals(oldBrokerIp)) {
			isChanged = true;
		}
		String brokerPort = "";
		if (database != null) {
			String text = brokerPortCombo.getText();
			brokerPort = (String) brokerPortCombo.getData(text);
			if (brokerPort == null) {
				brokerPort = text;
			}
			if (brokerPort != null && !brokerPort.equals(this.oldBrokerPort)) {
				isChanged = true;
			}
		}
		String charset = charsetText.getText();
		if (!charset.equals(this.oldCharSet)) {
			isChanged = true;
		}
		if (!isChanged) {
			return true;
		}
		Preferences pref = CubridManagerCorePlugin.getDefault().getPluginPreferences();
		String prefix = database.getServer().getServerInfo().getHostAddress()
				+ "." + database.getDatabaseInfo().getDbName();
		pref.setValue(prefix + QueryOptions.PROPERTY, true);

		pref.setValue(prefix + QueryOptions.ENABLE_CHAR_SET,
				charSetBtn.getSelection());
		pref.setValue(prefix + QueryOptions.CHAR_SET, charset);

		if (brokerPort != null) {
			pref.setValue(prefix + QueryOptions.BROKER_PORT, brokerPort);
		}
		pref.setValue(prefix + QueryOptions.BROKER_IP, brokerIpText.getText());
		CubridManagerCorePlugin.getDefault().savePluginPreferences();
		CommonTool.openInformationBox(
				com.cubrid.cubridmanager.ui.common.Messages.titleSuccess,
				Messages.msgChangeConnectionInfo);
		return super.performOk();
	}

	@Override
	protected Control createContents(Composite parent) {
		Composite top = new Composite(parent, SWT.NONE);
		top.setLayout(new GridLayout());
		top.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, true));
		final Group group_2 = new Group(top, SWT.NONE);
		group_2.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
		group_2.setLayout(new GridLayout());

		final Composite composite_1 = new Composite(group_2, SWT.NONE);
		composite_1.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 3;
		composite_1.setLayout(gridLayout2);

		final Label label_0 = new Label(composite_1, SWT.NONE);
		label_0.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
		label_0.setText(Messages.brokerIP);

		brokerIpText = new Text(composite_1, SWT.BORDER);
		final GridData gd_brokerIpText = new GridData(SWT.FILL, SWT.CENTER,
				true, false, 2, 1);
		brokerIpText.setLayoutData(gd_brokerIpText);

		final Label label_1 = new Label(composite_1, SWT.NONE);
		label_1.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
		label_1.setText(Messages.brokerPort);

		brokerPortCombo = new Combo(composite_1, SWT.NONE);
		final GridData gd_brokerPortCombo = new GridData(SWT.FILL, SWT.CENTER,
				true, false, 2, 1);
		brokerPortCombo.setLayoutData(gd_brokerPortCombo);
		BrokerInfos brokerInfos = database.getServer().getServerInfo().getBrokerInfos();
		if (brokerInfos != null) {
			BrokerInfoList bis = brokerInfos.getBorkerInfoList();
			if (bis != null) {
				List<BrokerInfo> brokerInfoList = bis.getBrokerInfoList();
				for (BrokerInfo brokerInfo : brokerInfoList) {
					if (brokerInfo.getPort() == null
							|| brokerInfo.getPort().trim().length() == 0) {
						continue;
					}
					String status = "";
					if (brokerInfos.getBrokerstatus() == null
							|| !brokerInfos.getBrokerstatus().equalsIgnoreCase(
									"ON")) {
						status = "OFF";
					} else {
						status = brokerInfo.getState() == null
								|| brokerInfo.getState().trim().equalsIgnoreCase(
										"OFF") ? "OFF" : "ON";
					}
					String text = brokerInfo.getName() + "["
							+ brokerInfo.getPort() + "/" + status + "]";
					brokerPortCombo.add(text);
					brokerPortCombo.setData(brokerInfo.getPort(), text);
					brokerPortCombo.setData(text, brokerInfo.getPort());
				}
			}
		}

		charSetBtn = new Button(composite_1, SWT.CHECK);
		final GridData gd_charSetBtn = new GridData(SWT.FILL, SWT.CENTER,
				false, false);
		charSetBtn.setLayoutData(gd_charSetBtn);
		charSetBtn.setText(Messages.charSet);
		charSetBtn.addSelectionListener(new SelectionAdapter() {

			@Override
			public void widgetSelected(SelectionEvent e) {
				charsetText.setEnabled(charSetBtn.getSelection());
			}

		});

		charsetText = new Combo(composite_1, SWT.BORDER);
		final GridData gd_charsetText = new GridData(SWT.FILL, SWT.CENTER,
				true, false, 2, 1);
		charsetText.setLayoutData(gd_charsetText);
		charsetText.setItems(QueryOptions.ALlCHARSET);
		loadPreference();
		return top;
	}

	private void loadPreference() {
		if (database != null) {
			String brokerIp = QueryOptions.getBrokerIp(database.getDatabaseInfo());
			if (brokerIp == null || brokerIp.trim().length() == 0) {
				brokerIp = database.getServer().getHostAddress();
			}
			this.oldBrokerIp = brokerIp;
			brokerIpText.setText(brokerIp);

			String brokerPort = QueryOptions.getBrokerPort(database.getDatabaseInfo());
			this.oldBrokerPort = brokerPort;
			String text = (String) brokerPortCombo.getData(brokerPort);
			if (text != null) {
				brokerPortCombo.setText(text);
			} else if (brokerPort != null) {
				brokerPortCombo.setText(brokerPort);
			}
			ServerUserInfo userInfo = database.getServer().getServerInfo().getLoginedUserInfo();
			if (!userInfo.isAdmin()) {
				brokerPortCombo.setEnabled(false);
				brokerIpText.setEnabled(false);
			}
		}
		boolean enableCharset = QueryOptions.getEnableCharset(database.getDatabaseInfo());
		charSetBtn.setSelection(enableCharset);
		charsetText.setEnabled(enableCharset);
		String charset = QueryOptions.getCharset(database.getDatabaseInfo());
		this.oldCharSet = charset;
		charsetText.setText(charset);
	}

	public void init(IWorkbench workbench) {

	}

}
