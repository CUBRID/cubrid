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
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.eclipse.jface.viewers.CellEditor;
import org.eclipse.jface.viewers.ComboBoxCellEditor;
import org.eclipse.jface.viewers.ICellModifier;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.jface.viewers.TextCellEditor;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Item;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Table;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.broker.model.BrokerInfo;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfoList;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfos;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.cubrid.database.control.VolumeInfoPage;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.TableViewerSorter;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;

/**
 * 
 * 
 * CUBRID Manager user authorization database page
 * 
 * @author pangqiren
 * @version 1.0 - 2009-4-23 created by pangqiren
 */
public class UserAuthDbInfoPage extends
		WizardPage {

	public static final String PAGENAME = VolumeInfoPage.class.getName();
	private Table authTable;
	private TableViewer authTableViewer;
	private CubridServer server = null;
	private ServerUserInfo userInfo = null;
	private List<Map<String, Object>> databaseAuthInfoTableList = new ArrayList<Map<String, Object>>();
	private String[] allBrokerPorts = null;
	private String[] allowConnected = { "Yes", "No" };

	/**
	 * The constructor
	 */
	public UserAuthDbInfoPage(CubridServer server, ServerUserInfo userInfo) {
		super(PAGENAME);
		this.userInfo = userInfo;
		this.server = server;
	}

	/**
	 * Creates the controls for this page
	 */
	public void createControl(Composite parent) {
		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		if (userInfo == null)
			whs.setHelp(parent, CubridManagerHelpContextIDs.userAdd);
		else
			whs.setHelp(parent, CubridManagerHelpContextIDs.userEdit);

		Composite composite = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = 10;
		layout.marginWidth = 10;
		composite.setLayout(layout);
		GridData gridData = new GridData(GridData.FILL_BOTH);
		composite.setLayoutData(gridData);

		createTable(composite);

		setTitle(Messages.titleDbAuth);
		setMessage(Messages.msgDbAuth);
		setControl(composite);

	}

	/**
	 * 
	 * Create authoration information table area
	 * 
	 * @param parent
	 */
	private void createTable(Composite parent) {

		Label tipLabel = new Label(parent, SWT.LEFT | SWT.WRAP);
		tipLabel.setText(Messages.msgDbAuthList);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		tipLabel.setLayoutData(gridData);

		final String[] columnNameArr = new String[] { Messages.tblColumnDbName,
				Messages.tblColumnConnected, Messages.tblColumnDbUser,
				Messages.tblColumnBrokerIP, Messages.tblColumnBrokerPort };
		authTableViewer = CommonTool.createCommonTableViewer(parent,
				new TableViewerSorter(), columnNameArr,
				CommonTool.createGridData(GridData.FILL_BOTH, 1, 1, -1, -1));
		initialBrokerPorts();
		initialTableModel();
		authTableViewer.setInput(databaseAuthInfoTableList);
		authTable = authTableViewer.getTable();
		for (int i = 0; i < authTable.getColumnCount(); i++) {
			authTable.getColumn(i).pack();
		}

		authTableViewer.setColumnProperties(columnNameArr);
		CellEditor[] editors = new CellEditor[5];
		editors[0] = null;
		editors[1] = new ComboBoxCellEditor(authTable, allowConnected,
				SWT.READ_ONLY);
		editors[2] = new TextCellEditor(authTable);
		editors[3] = new TextCellEditor(authTable);
		if (allBrokerPorts != null && allBrokerPorts.length > 0)
			editors[4] = new ComboBoxCellEditor(authTable, allBrokerPorts,
					SWT.READ_ONLY);
		else
			editors[4] = new TextCellEditor(authTable);
		authTableViewer.setCellEditors(editors);
		authTableViewer.setCellModifier(new ICellModifier() {
			@SuppressWarnings("unchecked")
			public boolean canModify(Object element, String property) {
				Map<String, String> map = (Map<String, String>) element;
				String str = map.get("1");
				if (property.equals(columnNameArr[2])) {
					return str.equals("Yes");
				} else if (property.equals(columnNameArr[3])) {
					return str.equals("Yes");
				} else if (property.equals(columnNameArr[4])) {
					return str.equals("Yes");
				}
				return true;
			}

			@SuppressWarnings("unchecked")
			public Object getValue(Object element, String property) {
				Map<String, String> map = (Map<String, String>) element;
				if (property.equals(columnNameArr[1])) {
					String str = map.get("1");
					int index = 0;
					if (str != null) {
						for (int i = 0; allowConnected != null
								&& i < allowConnected.length; i++) {
							if (str.equals(allowConnected[i])) {
								index = i;
								break;
							}
						}
					}
					return Integer.valueOf(index);
				} else if (property.equals(columnNameArr[2])) {
					return map.get("2");
				} else if (property.equals(columnNameArr[3])) {
					return map.get("3");
				} else if (property.equals(columnNameArr[4])) {
					String str = map.get("4");
					if (allBrokerPorts != null && allBrokerPorts.length > 0) {
						int index = 0;
						if (str != null) {
							for (int i = 0; allBrokerPorts != null
									&& i < allBrokerPorts.length; i++) {
								if (str.equals(allBrokerPorts[i])) {
									index = i;
									break;
								}
							}
						}
						return Integer.valueOf(index);
					} else {
						return str;
					}
				}
				return null;
			}

			@SuppressWarnings("unchecked")
			public void modify(Object element, String property, Object value) {
				if (element instanceof Item) {
					element = ((Item) element).getData();
				}
				Map<String, String> map = (Map<String, String>) element;
				if (property.equals(columnNameArr[1])) {
					String str = value.toString();
					if (str.matches("^\\d+$")) {
						int index = Integer.parseInt(str);
						if (index >= allowConnected.length) {
							index = 0;
						}
						String yesOrNoStr = allowConnected[index];
						map.put("1", yesOrNoStr);
						if (yesOrNoStr.equals("No")) {
							map.put("2", "");
							map.put("3", "");
							map.put("4", "");
						}else{
							map.put("3", server.getHostAddress());
						}
					}
				} else if (property.equals(columnNameArr[2])) {
					map.put("2", value.toString());
				} else if (property.equals(columnNameArr[3])) {
					map.put("3", value.toString());
				} else if (property.equals(columnNameArr[4])) {
					String str = value.toString();
					if (allBrokerPorts != null && allBrokerPorts.length > 0) {
						if (str.matches("^\\d+$")) {
							int index = Integer.parseInt(str);
							if (allBrokerPorts != null) {
								if (index >= allBrokerPorts.length) {
									index = 0;
								}
								map.put("4", allBrokerPorts[index]);
							}
						}
					} else {
						map.put("4", str);
					}
				}
				authTableViewer.refresh();
				check();
			}
		});

	}

	/**
	 * 
	 * Initial the broker ports
	 * 
	 */
	private void initialBrokerPorts() {
		BrokerInfos brokerInfos = server.getServerInfo().getBrokerInfos();
		if (brokerInfos != null) {
			String brokerStauts = brokerInfos.getBrokerstatus();
			BrokerInfoList brokerInfo = brokerInfos.getBorkerInfoList();
			if (brokerInfo != null) {
				List<BrokerInfo> brokerInfoList = brokerInfo.getBrokerInfoList();
				if (brokerInfoList != null) {
					List<String> brokerPortList = new ArrayList<String>();
					for (int i = 0; i < brokerInfoList.size(); i++) {
						BrokerInfo info = brokerInfoList.get(i);
						if (info.getPort() == null
								|| info.getPort().trim().length() == 0) {
							continue;
						}
						String port = info.getPort();
						String status = "";
						if (brokerStauts == null
								|| !brokerStauts.equalsIgnoreCase("ON")) {
							status = "OFF";
						} else {
							status = info.getState() == null
									|| info.getState().trim().equalsIgnoreCase(
											"OFF") ? "OFF" : "ON";
						}
						brokerPortList.add(info.getName() + "[" + port + "/"
								+ status + "]");
					}
					if (brokerPortList.size() > 0) {
						allBrokerPorts = new String[brokerPortList.size()];
						brokerPortList.toArray(allBrokerPorts);
					}
				}
			}
		}
	}

	/**
	 * 
	 * Initial the table model in user list table
	 * 
	 */
	private void initialTableModel() {
		List<String> databaseList = server.getServerInfo().getAllDatabaseList();
		for (int i = 0; databaseList != null && i < databaseList.size(); i++) {
			Map<String, Object> map = new HashMap<String, Object>();
			String dbName = databaseList.get(i);
			map.put("0", dbName);
			map.put("1", "No");
			map.put("2", "");
			map.put("3", "");
			map.put("4", "");
			databaseAuthInfoTableList.add(map);
		}

		if (userInfo != null) {
			List<DatabaseInfo> databaseInfoList = userInfo.getDatabaseInfoList();
			for (int i = 0; databaseInfoList != null
					&& i < databaseInfoList.size(); i++) {
				DatabaseInfo databaseInfo = databaseInfoList.get(i);
				String dbName = databaseInfo.getDbName();
				for (int j = 0; j < databaseAuthInfoTableList.size(); j++) {
					Map<String, Object> map = databaseAuthInfoTableList.get(j);
					if (map.get("0").equals(dbName)) {
						map.put("0", databaseInfo.getDbName());
						map.put("1", "Yes");
						map.put(
								"2",
								databaseInfo.getAuthLoginedDbUserInfo() != null ? databaseInfo.getAuthLoginedDbUserInfo().getName()
										: "");
						String brokerIP = databaseInfo.getBrokerIP();
						map.put("3", brokerIP == null ? "" : brokerIP);
						String port = databaseInfo.getBrokerPort() != null ? databaseInfo.getBrokerPort()
								: "";
						if (allBrokerPorts != null && allBrokerPorts.length > 0) {
							for (int k = 0; allBrokerPorts != null
									&& k < allBrokerPorts.length; k++) {
								String portInfo = allBrokerPorts[k].substring(
										allBrokerPorts[k].indexOf("[") + 1,
										allBrokerPorts[k].indexOf("/"));
								if (port.equals(portInfo)) {
									port = allBrokerPorts[k];
									break;
								}
							}
							map.put("4", port);
						} else {
							map.put("4", port);
						}
						break;
					}
				}
			}
		}
	}

	/**
	 * 
	 * Check the validation
	 * 
	 */
	private void check() {
		for (int i = 0; i < databaseAuthInfoTableList.size(); i++) {
			Map<String, Object> map = databaseAuthInfoTableList.get(i);
			String dbName = (String) map.get("0");
			String connected = (String) map.get("1");
			String dbUser = (String) map.get("2");
			String brokerIP = (String) map.get("3");
			String brokerPort = (String) map.get("4");
			if (connected.equals("Yes")
					&& (dbUser.trim().length() <= 0
							|| brokerPort.trim().length() <= 0 || brokerIP.trim().length() <= 0)) {
				setErrorMessage(Messages.bind(Messages.errDbAuth, dbName));
				setPageComplete(false);
				return;
			}
		}
		setErrorMessage(null);
		setPageComplete(true);
	}

	/**
	 * 
	 * Get database auth information list
	 * 
	 * @return
	 */
	public List<Map<String, Object>> getDbAuthInfoList() {
		return this.databaseAuthInfoTableList;
	}
}
