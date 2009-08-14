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
package com.cubrid.cubridmanager.ui.broker.control;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.preference.PreferencePage;
import org.eclipse.jface.viewers.CellEditor;
import org.eclipse.jface.viewers.ICellModifier;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.jface.viewers.TextCellEditor;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.VerifyEvent;
import org.eclipse.swt.events.VerifyListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Item;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.broker.task.SetBrokerConfParameterTask;
import com.cubrid.cubridmanager.core.common.model.CasAuthType;
import com.cubrid.cubridmanager.core.common.model.ConfConstants;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.OsInfoType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.broker.Messages;
import com.cubrid.cubridmanager.ui.broker.editor.internal.BrokerIntervalSetting;
import com.cubrid.cubridmanager.ui.broker.editor.internal.BrokerIntervalSettingManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.SWTResourceManager;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;

/**
 * 
 * A property page which is responsible for editing or just showing a certain
 * broker property
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-3-27 created by lizhiqiang
 */
public class BrokerParameterPropertyPage extends
		PreferencePage {

	private ICubridNode node;
	private Button refreshBtn;
	private Text intervalTxt;
	private Table paraTable;
	private Map<String, String> brokerMap;
	private Map<String, Map<String, String>> confParaMap;
	private String[] columnNameArrs;
	private TableViewer paraTableViewer;
	private String serverName;
	private String brokerName;
	private BrokerIntervalSetting setting;

	private static final String REFRESH_INTERVAL_SETTING = Messages.refreshNameOfTap;
	private static final String BROKER_PARAMETER = Messages.parameterNameOfTap;
	private static final String RESTART_BROKER_MSG = Messages.restartBrokerMsg;
	private String refreshTitle = Messages.refreshTitle;
	private String tblParameter = Messages.tblParameter;
	private String tblValueType = Messages.tblValueType;
	private String tblParamValue = Messages.tblParamValue;
	private String refreshOnLbl = Messages.refreshOnLbl;
	private String refreshUnitLbl = Messages.refreshUnitLbl;
	private String errReduplicatePort = Messages.errReduplicatePort;
	private ServerUserInfo userInfo;
	private Map<String, Map<String, String>> oldConfParaMap;

	/*
	 * The constructor
	 */
	public BrokerParameterPropertyPage(ICubridNode node, String title) {
		super(title, null);
		noDefaultAndApplyButton();
		this.node = node;
		userInfo = node.getServer().getServerInfo().getLoginedUserInfo();
		oldConfParaMap = node.getParent().getServer().getServerInfo().getBrokerConfParaMap();

	}

	@Override
	protected Control createContents(Composite parent) {
		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		whs.setHelp(parent, CubridManagerHelpContextIDs.brokerProperty);

		parent.setLayout(initGridLayout(new GridLayout(), true));
		TabFolder tabFolder = new TabFolder(parent, SWT.NONE);
		tabFolder.setLayoutData(new GridData(GridData.FILL_BOTH));

		tabFolder.setLayout(new GridLayout());

		TabItem item = new TabItem(tabFolder, SWT.NONE);
		item.setText(BROKER_PARAMETER);
		item.setControl(createParamComp(tabFolder));

		item = new TabItem(tabFolder, SWT.NONE);
		item.setText(REFRESH_INTERVAL_SETTING);
		item.setControl(createRefreshComp(tabFolder));
		initial();

		return parent;

	}

	/*
	 * Creates the parameter table
	 */
	private Control createParamComp(Composite parent) {

		columnNameArrs = new String[] { tblParameter, tblValueType,
				tblParamValue };
		paraTableViewer = CommonTool.createCommonTableViewer(parent, null,
				columnNameArrs, CommonTool.createGridData(GridData.FILL_BOTH,
						1, 1, -1, 200));
		paraTable = paraTableViewer.getTable();

		return paraTable;

	}

	/*
	 * Creates the refresh Composite
	 */
	private Control createRefreshComp(Composite parent) {
		Composite refreshComp = new Composite(parent, SWT.None);
		refreshComp.setLayout(new GridLayout());
		final GridData gd_refreshComp = new GridData(SWT.FILL, SWT.TOP, true,
				false);
		refreshComp.setLayoutData(gd_refreshComp);

		final Label tipLbl = new Label(refreshComp, SWT.NONE);
		final GridData gd_tipLbl = new GridData(SWT.LEFT, SWT.TOP, true, false);
		tipLbl.setText(refreshTitle);
		tipLbl.setLayoutData(gd_tipLbl);

		final Composite radioComp = new Composite(refreshComp, SWT.None);
		final GridData gd_radioComp = new GridData(SWT.FILL, SWT.TOP, true,
				false);
		radioComp.setLayoutData(gd_radioComp);
		radioComp.setLayout(new GridLayout(3, false));

		refreshBtn = new Button(radioComp, SWT.CHECK);
		refreshBtn.setText(refreshOnLbl);
		refreshBtn.setSelection(false);

		intervalTxt = new Text(radioComp, SWT.BORDER | SWT.RIGHT);
		final GridData gd_intervalTxt = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		intervalTxt.setLayoutData(gd_intervalTxt);
		intervalTxt.setText("1");
		intervalTxt.setEnabled(false);

		final Label secLbl = new Label(radioComp, SWT.NONE);
		final GridData gd_secLbl = new GridData(SWT.LEFT, SWT.TOP, true, false);
		secLbl.setText(refreshUnitLbl);
		tipLbl.setLayoutData(gd_secLbl);

		refreshBtn.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (refreshBtn.getSelection()) {
					intervalTxt.setEnabled(true);
				} else {
					intervalTxt.setEnabled(false);
				}

			}

		});

		intervalTxt.addVerifyListener(new VerifyListener() {
			public void verifyText(VerifyEvent e) {
				if (!"".equals(e.text) && !ValidateUtil.isNumber(e.text)) {
					e.doit = false;
					return;
				}
			}

		});
		intervalTxt.addModifyListener(new ModifyListener() {

			public void modifyText(ModifyEvent e) {
				if (intervalTxt.getText().trim().length() == 0) {

				}

			}

		});

		return refreshComp;
	}

	/*
	 * Initialize the value of all controls
	 * 
	 */
	private void initial() {
		serverName = node.getServer().getLabel();
		OsInfoType osInfoType = node.getServer().getServerInfo().getServerOsInfo();
		brokerName = node.getLabel().toLowerCase();// if need
		// initialize table
		confParaMap = node.getParent().getServer().getServerInfo().getBrokerConfParaMap();

		brokerMap = confParaMap.get(brokerName);
		List<Map<String, String>> parameterList = new ArrayList<Map<String, String>>();
		for (int i = 2; i < ConfConstants.brokerParameters.length; i++) {
			Map<String, String> dataMap = new HashMap<String, String>();
			String para = ConfConstants.brokerParameters[i][0];
			String type = ConfConstants.brokerParameters[i][1];
			String defaultValue = ConfConstants.brokerParameters[i][2];

			if (para.equals(ConfConstants.APPL_SERVER_PORT)) {
				if (osInfoType != OsInfoType.NT) {
					continue;
				} else {
					if (brokerMap != null
							&& brokerMap.get(ConfConstants.BROKER_PORT) != null) {
						int serverPortValue = Integer.parseInt(brokerMap.get(ConfConstants.BROKER_PORT)) + 1;
						defaultValue = Integer.toString(serverPortValue);
					}
				}
			}
			String bgColor = "grey";
			if (brokerMap != null && brokerMap.get(para) != null) {
				defaultValue = brokerMap.get(para);
				bgColor = "white";
			}

			dataMap.put("0", para);
			dataMap.put("1", type);
			dataMap.put("2", defaultValue);
			dataMap.put("bgColor", bgColor);
			parameterList.add(dataMap);
		}
		paraTableViewer.setInput(parameterList);
		for (int k = 12; k < paraTable.getItemCount(); k++) {
			if (parameterList.get(k).get("bgColor").equalsIgnoreCase("grey")) {
				paraTable.getItem(k).setBackground(
						SWTResourceManager.getColor(200, 200, 200));
			}
		}

		for (int i = 0; i < paraTable.getColumnCount(); i++) {
			paraTable.getColumn(i).pack();
		}

		if (userInfo.getCasAuth() == CasAuthType.AUTH_ADMIN) {
			linkEditorForTable();
		}
		// initialize refresh
		BrokerIntervalSettingManager manager = BrokerIntervalSettingManager.getInstance();

		setting = manager.getBrokerIntervalSetting(serverName, brokerName);
		boolean state = setting.isOn();
		String interval = setting.getInterval();
		if (state) {
			refreshBtn.setSelection(true);
			intervalTxt.setEnabled(true);
			intervalTxt.setText(interval);
		} else {
			refreshBtn.setSelection(false);
			intervalTxt.setEnabled(false);
		}
	}

	/*
	 * Links the editable column of table
	 * 
	 */
	private void linkEditorForTable() {
		paraTableViewer.setColumnProperties(columnNameArrs);
		CellEditor[] editors = new CellEditor[3];
		editors[0] = null;
		editors[1] = null;
		editors[2] = new TextCellEditor(paraTable);
		paraTableViewer.setCellEditors(editors);
		paraTableViewer.setCellModifier(new ICellModifier() {

			@SuppressWarnings("unchecked")
			public boolean canModify(Object element, String property) {
				if (property.equals(columnNameArrs[2])) {
					return true;
				}
				return false;
			}

			@SuppressWarnings("unchecked")
			public Object getValue(Object element, String property) {
				Map<String, String> map = (Map<String, String>) element;
				if (property.equals(columnNameArrs[2])) {
					return map.get("2");
				}
				return null;
			}

			@SuppressWarnings("unchecked")
			public void modify(Object element, String property, Object value) {
				if (element instanceof Item) {
					element = ((Item) element).getData();
				}
				Map<String, String> map = (Map<String, String>) element;
				String paramName = map.get("0");
				String type = map.get("1");
				boolean isValid = true;
				if (type.indexOf("int") >= 0) {
					boolean isInt = ValidateUtil.isInteger(value.toString());
					if (!isInt) {
						isValid = false;
					}
				} else if (type.startsWith("string")) {
					String valueStr = value.toString().trim();
					int start = type.indexOf("(");
					int end = type.indexOf(")");
					if (start > 0) {
						String valueStrs = type.substring(start + 1, end);
						String[] values = valueStrs.split("\\|");
						boolean isExist = false;
						for (String val : values) {
							if (valueStr.equalsIgnoreCase(val)) {
								isExist = true;
							}
						}
						if (!isExist) {
							isValid = false;
						}
					}
				}
				if (!isValid) {
					CommonTool.openErrorBox(Messages.bind(
							Messages.errParameterValue,
							new Object[] { paramName }));
				}
				if (type.indexOf("int") >= 0 && isValid) {
					int intValue = Integer.parseInt(value.toString());
					if (paramName.equalsIgnoreCase(ConfConstants.MAX_STRING_LENGTH)) {
						if (intValue == 0 || intValue < -1) {
							isValid = false;
							CommonTool.openErrorBox(Messages.bind(
									Messages.errMaxStringLengthValue,
									new Object[] { paramName }));
						}
					} else {
						if (intValue <= 0) {
							isValid = false;
							CommonTool.openErrorBox(Messages.bind(
									Messages.errPositiveValue,
									new Object[] { paramName }));
						}
						List<Map<String, String>> parameterList = (List<Map<String, String>>) paraTableViewer.getInput();
						if (paramName.equalsIgnoreCase(ConfConstants.MIN_NUM_APPL_SERVER)) {
							Map<String, String> dataMap = parameterList.get(3);
							String maxNumApplServer = dataMap.get("2");
							if (maxNumApplServer.trim().length() > 0) {
								if (intValue > Integer.parseInt(maxNumApplServer.trim())) {
									isValid = false;
									CommonTool.openErrorBox(Messages.bind(
											Messages.errMinNumApplServerValue,
											new Object[] { paramName }));
								}
							}
						}
						if (paramName.equalsIgnoreCase(ConfConstants.MAX_NUM_APPL_SERVER)) {
							Map<String, String> dataMap = parameterList.get(2);
							String minNumApplServer = dataMap.get("2");
							if (minNumApplServer.trim().length() > 0) {
								if (intValue < Integer.parseInt(minNumApplServer.trim())) {
									isValid = false;
									CommonTool.openErrorBox(Messages.bind(
											Messages.errMaxNumApplServeValue,
											new Object[] { paramName }));
								}
							}
						}

					}
				}
				if (paramName.equalsIgnoreCase(ConfConstants.BROKER_PORT)
						&& isValid) {
					int port = Integer.parseInt(value.toString());
					if (port < 1024 || port > 65535) {
						isValid = false;
						CommonTool.openErrorBox(Messages.bind(
								Messages.errBrokerPortAndShmId, paramName));
					}
					if (isValid) {
						String paramValue = value.toString().trim();
						for (Map.Entry<String, Map<String, String>> entry : oldConfParaMap.entrySet()) {
							if (entry.getKey().equals(node.getLabel())) {
								isValid = true;
								continue;
							} else {
								String otherPort = entry.getValue().get(
										ConfConstants.BROKER_PORT);
								if (paramValue.equalsIgnoreCase(otherPort)) {
									isValid = false;
									CommonTool.openErrorBox(errReduplicatePort);
									break;
								} else {
									isValid = true;
								}
							}
						}
						Map<String, String> brokerSectionMap = oldConfParaMap.get(ConfConstants.broker_sectionName);
						String masterShmId = brokerSectionMap.get(ConfConstants.MASTER_SHM_ID);
						if (paramValue.equals(masterShmId)) {
							isValid = false;
							CommonTool.openErrorBox(Messages.bind(
									Messages.errUseMasterShmId, paramValue));
						}
					}
					if (isValid) {
						String paramValue = value.toString().trim();
						int intServerPortValue = Integer.parseInt(paramValue) + 1;
						List<Map<String, String>> parameterList = (List<Map<String, String>>) paraTableViewer.getInput();
						for (Map<String, String> dataMap : parameterList) {
							if (dataMap.get("0").equalsIgnoreCase(
									ConfConstants.APPL_SERVER_PORT)) {
								String serverPortValue = Integer.toString(intServerPortValue);
								dataMap.put("2", serverPortValue);
							}
						}
					}
				}
				if (paramName.equalsIgnoreCase(ConfConstants.APPL_SERVER_SHM_ID)
						&& isValid) {
					int port = Integer.parseInt(value.toString());
					if (port < 1024 || port > 65535) {
						isValid = false;
						CommonTool.openErrorBox(Messages.bind(
								Messages.errBrokerPortAndShmId, paramName));
					}
					if (isValid) {
						String paramValue = value.toString().trim();
						for (Map.Entry<String, Map<String, String>> entry : oldConfParaMap.entrySet()) {
							if (entry.getKey().equals(node.getLabel())) {
								isValid = true;
								continue;
							} else {
								String otherPort = entry.getValue().get(
										ConfConstants.APPL_SERVER_SHM_ID);
								if (paramValue.equalsIgnoreCase(otherPort)) {
									isValid = false;
									CommonTool.openErrorBox(Messages.errReduplicateShmId);
									break;
								} else {
									isValid = true;
								}
							}
						}
						Map<String, String> brokerSectionMap = oldConfParaMap.get(ConfConstants.broker_sectionName);
						String masterShmId = brokerSectionMap.get(ConfConstants.MASTER_SHM_ID);
						if (paramValue.equals(masterShmId)) {
							isValid = false;
							CommonTool.openErrorBox(Messages.bind(
									Messages.errUseMasterShmId, paramValue));
						}
					}

				}
				if (isValid) {
					map.put("2", value.toString());
				}
				paraTableViewer.refresh();
			}
		});
	}

	@Override
	public boolean performOk() {
		// execute set parameter task
		if (isTableChange()) {
			Map<String, String> paraMap = confParaMap.get(node.getLabel());
			for (String[] brokerPara : ConfConstants.brokerParameters) {
				if (brokerPara[0].equals(ConfConstants.ACCESS_LIST)
						&& brokerPara[2].equals(paraMap.get(ConfConstants.ACCESS_LIST))) {
					paraMap.remove(ConfConstants.ACCESS_LIST);
				} else if (brokerPara[0].equals(ConfConstants.ACCESS_LOG)
						&& brokerPara[2].equals(paraMap.get(ConfConstants.ACCESS_LOG))) {
					paraMap.remove(ConfConstants.ACCESS_LOG);
				} else if (brokerPara[0].equals(ConfConstants.APPL_SERVER_PORT)) {
					int serverPortValue = Integer.parseInt(paraMap.get(ConfConstants.BROKER_PORT)) + 1;
					String serverPort = paraMap.get(ConfConstants.APPL_SERVER_PORT);
					if (serverPort.equalsIgnoreCase(Integer.toString(serverPortValue))) {
						paraMap.remove(ConfConstants.APPL_SERVER_PORT);
					}
				} else if (brokerPara[0].equals(ConfConstants.LOG_BACKUP)
						&& brokerPara[2].equals(paraMap.get(ConfConstants.LOG_BACKUP))) {
					paraMap.remove(ConfConstants.LOG_BACKUP);
				} else if (brokerPara[0].equals(ConfConstants.SQL_LOG_MAX_SIZE)
						&& brokerPara[2].equals(paraMap.get(ConfConstants.SQL_LOG_MAX_SIZE))) {
					paraMap.remove(ConfConstants.SQL_LOG_MAX_SIZE);
				} else if (brokerPara[0].equals(ConfConstants.MAX_STRING_LENGTH)
						&& brokerPara[2].equals(paraMap.get(ConfConstants.MAX_STRING_LENGTH))) {
					paraMap.remove(ConfConstants.MAX_STRING_LENGTH);
				} else if (brokerPara[0].equals(ConfConstants.SOURCE_ENV)
						&& brokerPara[2].equals(paraMap.get(ConfConstants.SOURCE_ENV))) {
					paraMap.remove(ConfConstants.SOURCE_ENV);
				} else if (brokerPara[0].equals(ConfConstants.STATEMENT_POOLING)
						&& brokerPara[2].equals(paraMap.get(ConfConstants.STATEMENT_POOLING))) {
					paraMap.remove(ConfConstants.STATEMENT_POOLING);
				} else if (brokerPara[0].equals(ConfConstants.LONG_QUERY_TIME)
						&& brokerPara[2].equals(paraMap.get(ConfConstants.LONG_QUERY_TIME))) {
					paraMap.remove(ConfConstants.LONG_QUERY_TIME);
				} else if (brokerPara[0].equals(ConfConstants.LONG_TRANSACTION_TIME)
						&& brokerPara[2].equals(paraMap.get(ConfConstants.LONG_TRANSACTION_TIME))) {
					paraMap.remove(ConfConstants.LONG_TRANSACTION_TIME);
				}
			}

			ServerInfo serverInfo = node.getServer().getServerInfo();
			SetBrokerConfParameterTask setBrokerConfParameterTask = new SetBrokerConfParameterTask(
					serverInfo);
			setBrokerConfParameterTask.setConfParameters(confParaMap);

			CommonTaskExec taskExec = new CommonTaskExec();
			taskExec.addTask(setBrokerConfParameterTask);
			new ExecTaskWithProgress(taskExec).exec();
			if (taskExec.isSuccess()) {
				CommonTool.openInformationBox(
						com.cubrid.cubridmanager.ui.common.Messages.titleSuccess,
						RESTART_BROKER_MSG);
			} else {
				return true;
			}
		}
		// refresh tap
		if (isSettingChange()) {
			boolean isOn = refreshBtn.getSelection();
			String interval = intervalTxt.getText();
			String serverName = node.getServer().getLabel();
			String nodeName = node.getLabel();
			BrokerIntervalSetting newSetting = new BrokerIntervalSetting(
					serverName, nodeName, interval, isOn);
			BrokerIntervalSettingManager manager = BrokerIntervalSettingManager.getInstance();
			manager.removeBrokerIntervalSetting(serverName, brokerName);
			manager.setBrokerInterval(newSetting);
		}
		return true;
	}

	/*
	 * Initiates a grid layout
	 * 
	 * @param layout an instance of GridLayout @param margins if true,sets the
	 * marginWidth and marginHeight,or else,using default value @return
	 */
	private GridLayout initGridLayout(GridLayout layout, boolean margins) {
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		if (margins) {
			layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
			layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		} else {
			layout.marginWidth = 0;
			layout.marginHeight = 0;
		}
		return layout;
	}

	/*
	 * Judge if there is change on table
	 * 
	 * @return
	 */
	@SuppressWarnings("unchecked")
	private boolean isTableChange() {

		List<Map<String, String>> paramList = (List<Map<String, String>>) paraTableViewer.getInput();
		Map<String, String> paraMap = new HashMap<String, String>();
		for (Map<String, String> map : paramList) {
			if (map.get("0").equals(ConfConstants.BROKER_PORT)) {
				paraMap.put(ConfConstants.BROKER_PORT, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.SERVICE)) {
				paraMap.put(ConfConstants.SERVICE, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.BROKER_PORT)) {
				paraMap.put(ConfConstants.BROKER_PORT, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.MIN_NUM_APPL_SERVER)) {
				paraMap.put(ConfConstants.MIN_NUM_APPL_SERVER, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.MAX_NUM_APPL_SERVER)) {
				paraMap.put(ConfConstants.MAX_NUM_APPL_SERVER, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.APPL_SERVER_SHM_ID)) {
				paraMap.put(ConfConstants.APPL_SERVER_SHM_ID, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.LOG_DIR)) {
				paraMap.put(ConfConstants.LOG_DIR, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.ERROR_LOG_DIR)) {
				paraMap.put(ConfConstants.ERROR_LOG_DIR, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.SQL_LOG)) {
				paraMap.put(ConfConstants.SQL_LOG, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.TIME_TO_KILL)) {
				paraMap.put(ConfConstants.TIME_TO_KILL, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.SESSION_TIMEOUT)) {
				paraMap.put(ConfConstants.SESSION_TIMEOUT, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.KEEP_CONNECTION)) {
				paraMap.put(ConfConstants.KEEP_CONNECTION, map.get("2"));
			}

			if (map.get("0").equals(ConfConstants.ACCESS_LIST)) {
				paraMap.put(ConfConstants.ACCESS_LIST, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.ACCESS_LOG)) {
				paraMap.put(ConfConstants.ACCESS_LOG, map.get("2"));
			}

			if (map.get("0").equals(ConfConstants.APPL_SERVER_PORT)) {
				paraMap.put(ConfConstants.APPL_SERVER_PORT, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.LOG_BACKUP)) {
				paraMap.put(ConfConstants.LOG_BACKUP, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.SQL_LOG_MAX_SIZE)) {
				paraMap.put(ConfConstants.SQL_LOG_MAX_SIZE, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.MAX_STRING_LENGTH)) {
				paraMap.put(ConfConstants.MAX_STRING_LENGTH, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.SOURCE_ENV)) {
				paraMap.put(ConfConstants.SOURCE_ENV, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.STATEMENT_POOLING)) {
				paraMap.put(ConfConstants.STATEMENT_POOLING, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.LONG_QUERY_TIME)) {
				paraMap.put(ConfConstants.LONG_QUERY_TIME, map.get("2"));
			}
			if (map.get("0").equals(ConfConstants.LONG_TRANSACTION_TIME)) {
				paraMap.put(ConfConstants.LONG_TRANSACTION_TIME, map.get("2"));
			}

		}
		confParaMap.put(node.getLabel(), paraMap);
		//complete the default value in brokerMap
		for (String[] brokerParameter : ConfConstants.brokerParameters) {
			if (brokerParameter[0].equalsIgnoreCase(ConfConstants.MASTER_SHM_ID)
					|| brokerParameter[0].equalsIgnoreCase(ConfConstants.ADMIN_LOG_FILE)) {
				continue;
			}
			if (brokerParameter[0].equalsIgnoreCase(ConfConstants.APPL_SERVER_PORT)) {
				if (brokerMap.get(brokerParameter[0]) == null) {
					int serverPortValue = Integer.parseInt(brokerMap.get(ConfConstants.BROKER_PORT)) + 1;
					brokerMap.put(brokerParameter[0],
							Integer.toString(serverPortValue));
					continue;
				}
			}
			if (brokerMap.get(brokerParameter[0]) == null) {
				brokerMap.put(brokerParameter[0], brokerParameter[2]);
			}
		}
		for (Map.Entry<String, String> entry : brokerMap.entrySet()) {
			if (null != paraMap.get(entry.getKey())) {
				if (!paraMap.get(entry.getKey()).equals(entry.getValue())) {
					return true;
				}
			}
		}
		return false;
	}

	/*
	 * Judge if there is change on interval setting
	 * 
	 * @return
	 */
	private boolean isSettingChange() {
		boolean state = setting.isOn();
		String interval = setting.getInterval();
		if (refreshBtn.getSelection() != state) {
			return true;
		} else {
			if (!intervalTxt.getText().trim().equals(interval)) {
				return true;
			}
		}
		return false;
	}

}
