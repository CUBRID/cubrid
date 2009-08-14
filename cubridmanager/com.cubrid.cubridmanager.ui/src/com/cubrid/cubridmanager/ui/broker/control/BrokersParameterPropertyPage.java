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
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.dialogs.Dialog;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.preference.PreferencePage;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.ISelectionChangedListener;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.SelectionChangedEvent;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.VerifyEvent;
import org.eclipse.swt.events.VerifyListener;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.broker.task.DeleteBrokerTask;
import com.cubrid.cubridmanager.core.broker.task.SetBrokerConfParameterTask;
import com.cubrid.cubridmanager.core.common.model.AddEditType;
import com.cubrid.cubridmanager.core.common.model.CasAuthType;
import com.cubrid.cubridmanager.core.common.model.ConfConstants;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.broker.dialog.BrokerParameterDialog;
import com.cubrid.cubridmanager.ui.broker.editor.internal.BrokerIntervalSetting;
import com.cubrid.cubridmanager.ui.broker.editor.internal.BrokerIntervalSettingManager;
import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;

/**
 * 
 * CUBRID Broker property page
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-5-4 created by lizhiqiang
 */
public class BrokersParameterPropertyPage extends
		PreferencePage {

	private static final String DELETE_BTN_NAME = Messages.deleteBtnName;
	private static final String EDIT_BTN_NAME = Messages.editBtnName;
	private static final String ADD_BTN_NAME = Messages.addBtnName;
	private final String refreshUnit = Messages.refreshUnit;
	private final String refreshOnLbl = Messages.refreshEnvOnLbl;
	private final String refreshEnvTitle = Messages.refreshEnvTitle;
	private final String portOfBrokerLst = Messages.portOfBrokerLst;
	private final String nameOfBrokerLst = Messages.nameOfBrokerLst;
	private static final String BROKER_LIST = Messages.brokerLstGroupName;
	private static final String GENERAL_INFO = Messages.generalInfoGroupName;
	private static final String REFRESHENVOFTAP = Messages.refreshEnvOfTap;
	private static final String BROKERLSTOFTAP = Messages.brokerLstOfTap;
	private static final String RESTART_BROKER_MSG = Messages.restartBrokerMsg;
	private final String editActionTxt = Messages.editActionTxt;
	private final String addActionTxt = Messages.addActionTxt;
	private final String delActionTxt = Messages.delActionTxt;
	private Text masterShmIdTxt;
	private Text adminlogTxt;
	private ICubridNode node = null;

	private String[] columnNameArrs;

	private Map<String, Map<String, String>> defaultValueMap = new HashMap<String, Map<String, String>>();
	private Button refreshBtn;
	private Text intervalTxt;
	private TableViewer brokersTableViewer;
	private Table brokersTable;
	private List<Map<String, String>> brokerList;
	private Map<String, Map<String, String>> oldConfParaMap;
	private Map<String, Map<String, String>> newConfParaMap;
	private Map<String, BrokerIntervalSetting> oldIntervalSettingMap;
	private Map<String, BrokerIntervalSetting> newIntervalSettingMap;

	private List<String> deletedBrokerLst = new ArrayList<String>();

	private String masterShmIdLblName = "MASTER_SHM_ID:";
	private String adminlogLblName = "ADMIN_LOG_FILE:";
	private Button addBtn;
	private Button editBtn;
	private Button deleteBtn;
	private String serverName;
	private String brokerEnvName;
	private ServerUserInfo userInfo;

	public BrokersParameterPropertyPage(ICubridNode node, String name) {
		super(name, null);
		this.node = node;
		noDefaultAndApplyButton();
		userInfo = node.getServer().getServerInfo().getLoginedUserInfo();
	}

	/**
	 * Creates the page content
	 */
	protected Control createContents(Composite parent) {
		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		whs.setHelp(parent, CubridManagerHelpContextIDs.brokerProperty);

		Composite composite = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		composite.setLayout(layout);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));

		TabFolder tabFolder = new TabFolder(composite, SWT.NONE);
		tabFolder.setLayoutData(new GridData(GridData.FILL_BOTH));
		layout = new GridLayout();
		tabFolder.setLayout(layout);

		TabItem item = new TabItem(tabFolder, SWT.NONE);
		item.setText(BROKERLSTOFTAP);
		item.setControl(createBrokerLstComp(tabFolder));

		item = new TabItem(tabFolder, SWT.NONE);
		item.setText(REFRESHENVOFTAP);
		item.setControl(createRefreshComp(tabFolder));
		initial();
		setAuthority();

		masterShmIdTxt.addModifyListener(new MasterShmIdModifyListener());
		masterShmIdTxt.addVerifyListener(new NumberVerifyListener());
		return composite;
	}

	/*
	 * Sets the authority
	 */
	private void setAuthority() {
		assert (null != userInfo);
		switch (userInfo.getCasAuth()) {
		case AUTH_ADMIN:
			break;
		case AUTH_MONITOR:
			masterShmIdTxt.setEnabled(false);
			adminlogTxt.setEnabled(false);
			addBtn.setEnabled(false);
			deleteBtn.setEnabled(false);
			break;
		default:
		}
	}

	/*
	 * Creates basic group
	 */
	private void createBasicGroup(Composite parent) {
		final Group group = new Group(parent, SWT.NONE);
		group.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
		group.setText(GENERAL_INFO);
		final GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		group.setLayout(gridLayout);

		final Label masterShmIdLbl = new Label(group, SWT.NONE);
		final GridData gd_masterShmIdLbl = new GridData(SWT.CENTER, SWT.CENTER,
				false, false);
		masterShmIdLbl.setLayoutData(gd_masterShmIdLbl);
		masterShmIdLbl.setText(masterShmIdLblName);

		masterShmIdTxt = new Text(group, SWT.BORDER);
		final GridData gd_masterShmIdTxt = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		masterShmIdTxt.setLayoutData(gd_masterShmIdTxt);

		final Label adminlogLbl = new Label(group, SWT.NONE);
		final GridData gd_adminlogLbl = new GridData(SWT.CENTER, SWT.CENTER,
				false, false);
		adminlogLbl.setLayoutData(gd_adminlogLbl);
		adminlogLbl.setText(adminlogLblName);

		adminlogTxt = new Text(group, SWT.BORDER);
		final GridData gd_adminlogTxt = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		adminlogTxt.setLayoutData(gd_adminlogTxt);
	}

	/*
	 * Creates brokers list Composite
	 */
	private Control createBrokerLstComp(Composite parent) {
		Composite composite = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		composite.setLayout(layout);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));

		createBasicGroup(composite);

		Group brokerLstGroup = new Group(composite, SWT.NONE);
		brokerLstGroup.setText(BROKER_LIST);
		brokerLstGroup.setLayout(new GridLayout(2, false));
		brokerLstGroup.setLayoutData(new GridData(GridData.FILL_BOTH));
		columnNameArrs = new String[] { nameOfBrokerLst, portOfBrokerLst };
		brokersTableViewer = CommonTool.createCommonTableViewer(brokerLstGroup,
				null, columnNameArrs, CommonTool.createGridData(
						GridData.FILL_BOTH, 1, 1, -1, 200));
		brokersTable = brokersTableViewer.getTable();
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(150, 150, true));
		tlayout.addColumnData(new ColumnWeightData(150, 150, true));
		brokersTable.setLayout(tlayout);

		createDealButton(brokerLstGroup);
		return composite;
	}

	/*
	 * Creates the refresh composite
	 */
	private Control createRefreshComp(Composite parent) {
		Composite refreshComp = new Composite(parent, SWT.None);
		refreshComp.setLayout(new GridLayout());
		final GridData gd_refreshComp = new GridData(SWT.FILL, SWT.TOP, true,
				false);
		refreshComp.setLayoutData(gd_refreshComp);

		final Label tipLbl = new Label(refreshComp, SWT.NONE);
		final GridData gd_tipLbl = new GridData(SWT.LEFT, SWT.TOP, true, false);
		tipLbl.setText(refreshEnvTitle);
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
		secLbl.setText(refreshUnit);
		tipLbl.setLayoutData(gd_secLbl);

		return refreshComp;
	}

	/*
	 * Initializes the parameters of this dialog
	 */
	private void initial() {
		oldConfParaMap = node.getServer().getServerInfo().getBrokerConfParaMap();
		Map<String, String> map = null;
		if (oldConfParaMap != null) {
			map = oldConfParaMap.get(ConfConstants.broker_sectionName);
		}
		if (map != null) {
			Iterator<Map.Entry<String, String>> it = map.entrySet().iterator();
			Map<String, String> defaultMap = new HashMap<String, String>();
			defaultValueMap.put(ConfConstants.broker_sectionName, defaultMap);
			while (it.hasNext()) {
				Map.Entry<String, String> entry = it.next();
				defaultMap.put(entry.getKey(), entry.getValue());
			}
			if (null != defaultMap.get(ConfConstants.MASTER_SHM_ID)) {
				masterShmIdTxt.setText(defaultMap.get(ConfConstants.MASTER_SHM_ID));
			} else {
				masterShmIdTxt.setText("");
			}
			if (null != defaultMap.get(ConfConstants.ADMIN_LOG_FILE)) {
				adminlogTxt.setText(defaultMap.get(ConfConstants.ADMIN_LOG_FILE));
			} else {
				adminlogTxt.setText("");
			}
		}
		brokerList = new ArrayList<Map<String, String>>();

		if (oldConfParaMap == null) {
			return;
		}
		for (Map.Entry<String, Map<String, String>> brokerPara : oldConfParaMap.entrySet()) {
			if (!brokerPara.getKey().equals(ConfConstants.broker_sectionName)) {
				String name = brokerPara.getKey();
				String port = brokerPara.getValue().get(
						ConfConstants.BROKER_PORT);
				String service = brokerPara.getValue().get(
						ConfConstants.SERVICE);
				String minNumApplServer = brokerPara.getValue().get(
						ConfConstants.MIN_NUM_APPL_SERVER);
				String maxNumApplServer = brokerPara.getValue().get(
						ConfConstants.MAX_NUM_APPL_SERVER);
				String applServerShmId = brokerPara.getValue().get(
						ConfConstants.APPL_SERVER_SHM_ID);
				String logDir = brokerPara.getValue().get(ConfConstants.LOG_DIR);
				String sqlLog = brokerPara.getValue().get(ConfConstants.SQL_LOG);
				String errorLogDir = brokerPara.getValue().get(
						ConfConstants.ERROR_LOG_DIR);
				String timeToKill = brokerPara.getValue().get(
						ConfConstants.TIME_TO_KILL);
				String sessionTimeout = brokerPara.getValue().get(
						ConfConstants.SESSION_TIMEOUT);
				String keepConnection = brokerPara.getValue().get(
						ConfConstants.KEEP_CONNECTION);
				String accessList = brokerPara.getValue().get(
						ConfConstants.ACCESS_LIST);
				String accessLog = brokerPara.getValue().get(
						ConfConstants.ACCESS_LOG);
				String applServerPort = brokerPara.getValue().get(
						ConfConstants.APPL_SERVER_PORT);
				String sqlLogMaxSize = brokerPara.getValue().get(
						ConfConstants.SQL_LOG_MAX_SIZE);
				String maxStringLenght = brokerPara.getValue().get(
						ConfConstants.MAX_STRING_LENGTH);
				String sourceEnv = brokerPara.getValue().get(
						ConfConstants.SOURCE_ENV);
				String statementPooling = brokerPara.getValue().get(
						ConfConstants.STATEMENT_POOLING);
				String longQueryTime = brokerPara.getValue().get(
						ConfConstants.LONG_QUERY_TIME);
				String longTransactionTime = brokerPara.getValue().get(
						ConfConstants.LONG_TRANSACTION_TIME);

				Map<String, String> dataMap = new HashMap<String, String>();
				dataMap.put("0", name);
				dataMap.put("1", port);
				dataMap.put(ConfConstants.BROKER_PORT, port);
				dataMap.put(ConfConstants.SERVICE, service);
				dataMap.put(ConfConstants.MIN_NUM_APPL_SERVER, minNumApplServer);
				dataMap.put(ConfConstants.MAX_NUM_APPL_SERVER, maxNumApplServer);
				dataMap.put(ConfConstants.APPL_SERVER_SHM_ID, applServerShmId);
				dataMap.put(ConfConstants.LOG_DIR, logDir);
				dataMap.put(ConfConstants.ERROR_LOG_DIR, errorLogDir);
				dataMap.put(ConfConstants.SQL_LOG, sqlLog);
				dataMap.put(ConfConstants.TIME_TO_KILL, timeToKill);
				dataMap.put(ConfConstants.SESSION_TIMEOUT, sessionTimeout);
				dataMap.put(ConfConstants.KEEP_CONNECTION, keepConnection);
				dataMap.put(ConfConstants.ACCESS_LIST, accessList);
				dataMap.put(ConfConstants.ACCESS_LOG, accessLog);
				dataMap.put(ConfConstants.APPL_SERVER_PORT, applServerPort);
				dataMap.put(ConfConstants.SQL_LOG_MAX_SIZE, sqlLogMaxSize);
				dataMap.put(ConfConstants.MAX_STRING_LENGTH, maxStringLenght);
				dataMap.put(ConfConstants.SOURCE_ENV, sourceEnv);
				dataMap.put(ConfConstants.STATEMENT_POOLING, statementPooling);
				dataMap.put(ConfConstants.LONG_QUERY_TIME, longQueryTime);
				dataMap.put(ConfConstants.LONG_TRANSACTION_TIME,
						longTransactionTime);

				brokerList.add(dataMap);
			}
		}
		brokersTableViewer.setInput(brokerList);

		// initialize refresh
		BrokerIntervalSettingManager manager = BrokerIntervalSettingManager.getInstance();
		serverName = node.getServer().getLabel();
		brokerEnvName = node.getLabel();
		BrokerIntervalSetting setting = manager.getBrokerIntervalSetting(
				serverName, brokerEnvName);
		oldIntervalSettingMap = new HashMap<String, BrokerIntervalSetting>();

		newIntervalSettingMap = new HashMap<String, BrokerIntervalSetting>();
		if (null != setting) {
			boolean isOn = setting.isOn();
			refreshBtn.setSelection(isOn);
			intervalTxt.setText(setting.getInterval());
			intervalTxt.setEnabled(isOn);
			oldIntervalSettingMap.put(serverName + "_" + brokerEnvName, setting);
		}
		for (Map<String, String> dataMap : brokerList) {
			String brokername = dataMap.get("0");
			setting = manager.getBrokerIntervalSetting(serverName, brokername);
			if (null != setting) {
				oldIntervalSettingMap.put(serverName + "_" + brokername,
						setting);
			}
		}

		for (Map.Entry<String, BrokerIntervalSetting> entry : oldIntervalSettingMap.entrySet()) {
			String aSettingName = entry.getKey();
			BrokerIntervalSetting aSetting = new BrokerIntervalSetting();
			String aBrokerName = entry.getValue().getBrokerName();
			String aServerName = entry.getValue().getServerName();
			String aInterval = entry.getValue().getInterval();
			boolean aOn = entry.getValue().isOn();
			aSetting.setBrokerName(aBrokerName);
			aSetting.setServerName(aServerName);
			aSetting.setInterval(aInterval);
			aSetting.setOn(aOn);
			newIntervalSettingMap.put(aSettingName, aSetting);
		}

		// add Listener
		refreshBtn.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				BrokerIntervalSetting brokerEnvSetting = newIntervalSettingMap.get(serverName
						+ "_" + brokerEnvName);
				if (refreshBtn.getSelection()) {
					intervalTxt.setEnabled(true);
					brokerEnvSetting.setOn(true);
				} else {
					intervalTxt.setEnabled(false);
					brokerEnvSetting.setOn(false);
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
				String newInterval = intervalTxt.getText().trim();
				BrokerIntervalSetting brokerEnvSetting = newIntervalSettingMap.get(serverName
						+ "_" + brokerEnvName);
				brokerEnvSetting.setInterval(newInterval);
			}

		});

	}

	@Override
	protected void performDefaults() {
		initial();
		super.performDefaults();

	}

	@Override
	public boolean performOk() {
		if (adminlogTxt == null || adminlogTxt.isDisposed()) {
			return true;
		}
		// execute delete broker task
		if (isTableChange()) {
			ServerInfo serverInfo = node.getServer().getServerInfo();

			CommonTaskExec taskExec = new CommonTaskExec();
			for (String bname : deletedBrokerLst) {
				DeleteBrokerTask task = new DeleteBrokerTask(serverInfo);
				task.setBname(bname);
				taskExec.addTask(task);
			}
			//remove the default value
			for (Map.Entry<String, Map<String, String>> entry : newConfParaMap.entrySet()) {
				Map<String, String> paraMap = entry.getValue();
				for (String[] brokerPara : ConfConstants.brokerParameters) {
					if (brokerPara[0].equals(ConfConstants.ACCESS_LIST)
							&& brokerPara[2].equals(paraMap.get(ConfConstants.ACCESS_LIST))) {
						paraMap.remove(ConfConstants.ACCESS_LIST);
					} else if (brokerPara[0].equals(ConfConstants.ACCESS_LOG)
							&& brokerPara[2].equals(paraMap.get(ConfConstants.ACCESS_LOG))) {
						paraMap.remove(ConfConstants.ACCESS_LOG);
					} else if (brokerPara[0].equals(ConfConstants.APPL_SERVER_PORT)) {

						String port = paraMap.get(ConfConstants.BROKER_PORT);
						if (null != port) {
							int serverPortValue = Integer.parseInt(paraMap.get(ConfConstants.BROKER_PORT)) + 1;
							String serverPort = paraMap.get(ConfConstants.APPL_SERVER_PORT);
							if (serverPort.equalsIgnoreCase(Integer.toString(serverPortValue))) {
								paraMap.remove(ConfConstants.APPL_SERVER_PORT);
							}
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
			}

			// execute set parameter task
			SetBrokerConfParameterTask setBrokerConfParameterTask = new SetBrokerConfParameterTask(
					serverInfo);
			setBrokerConfParameterTask.setConfParameters(newConfParaMap);
			taskExec.addTask(setBrokerConfParameterTask);
			new ExecTaskWithProgress(taskExec).exec();
			if (taskExec.isSuccess()) {
				CommonTool.openInformationBox(Messages.titleSuccess,
						RESTART_BROKER_MSG);
			}
		}
		// refresh tap
		if (isSettingChange()) {
			boolean isOn = refreshBtn.getSelection();
			String interval = intervalTxt.getText().trim();
			String serverName = node.getServer().getLabel();
			String nodeName = node.getLabel();
			BrokerIntervalSetting setting = new BrokerIntervalSetting(
					serverName, nodeName, interval, isOn);
			newIntervalSettingMap.put(serverName + "_" + brokerEnvName, setting);

			BrokerIntervalSettingManager manager = BrokerIntervalSettingManager.getInstance();
			for (Map.Entry<String, BrokerIntervalSetting> entry : oldIntervalSettingMap.entrySet()) {
				String brokerName = entry.getValue().getBrokerName();
				manager.removeBrokerIntervalSetting(serverName, brokerName);
			}
			for (Map.Entry<String, BrokerIntervalSetting> entry : newIntervalSettingMap.entrySet()) {
				BrokerIntervalSetting newSetting = entry.getValue();
				manager.setBrokerInterval(newSetting);
			}
		}
		return true;
	}

	/*
	 * Judges if there is any change in table
	 */
	private boolean isTableChange() {
		newConfParaMap = new HashMap<String, Map<String, String>>();
		Map<String, String> basicMap = new HashMap<String, String>();
		basicMap.put(ConfConstants.MASTER_SHM_ID, masterShmIdTxt.getText());
		basicMap.put(ConfConstants.ADMIN_LOG_FILE, adminlogTxt.getText());

		newConfParaMap.put(ConfConstants.broker_sectionName, basicMap);

		for (Map<String, String> map : brokerList) {
			Map<String, String> paraMap = new HashMap<String, String>();

			for (String[] brokerParameter : ConfConstants.brokerParameters) {
				if (brokerParameter[0].equalsIgnoreCase(ConfConstants.MASTER_SHM_ID)
						|| brokerParameter[0].equalsIgnoreCase(ConfConstants.ADMIN_LOG_FILE)) {
					continue;
				}
				if (brokerParameter[0].equalsIgnoreCase(ConfConstants.APPL_SERVER_PORT)) {
					if (map.get(brokerParameter[0]) == null) {
						int serverPortValue = Integer.parseInt(map.get(ConfConstants.BROKER_PORT)) + 1;
						paraMap.put(brokerParameter[0],
								Integer.toString(serverPortValue));
						continue;
					}
				}
				if (map.get(brokerParameter[0]) == null) {
					paraMap.put(brokerParameter[0], brokerParameter[2]);
				} else {
					paraMap.put(brokerParameter[0], map.get(brokerParameter[0]));
				}
			}
			newConfParaMap.put(map.get("0"), paraMap);
		}

		if (oldConfParaMap.size() != newConfParaMap.size()) {
			return true;
		}
		for (Map.Entry<String, Map<String, String>> oldEntry : oldConfParaMap.entrySet()) {
			String oldKey = oldEntry.getKey();
			Map<String, String> oldPropMap = oldEntry.getValue();
			if (!"broker".equals(oldKey)) {
				for (String[] brokerParameter : ConfConstants.brokerParameters) {
					if (brokerParameter[0].equalsIgnoreCase(ConfConstants.MASTER_SHM_ID)
							|| brokerParameter[0].equalsIgnoreCase(ConfConstants.ADMIN_LOG_FILE)) {
						continue;
					}
					if (brokerParameter[0].equalsIgnoreCase(ConfConstants.APPL_SERVER_PORT)) {
						if (oldPropMap.get(brokerParameter[0]) == null
								&& null != oldPropMap.get(ConfConstants.BROKER_PORT)
								&& !oldPropMap.get(ConfConstants.BROKER_PORT).equals(
										"")) {
							int serverPortValue = Integer.parseInt(oldPropMap.get(ConfConstants.BROKER_PORT)) + 1;
							oldPropMap.put(brokerParameter[0],
									Integer.toString(serverPortValue));
							continue;
						}
					}

					if (oldPropMap.get(brokerParameter[0]) == null) {

						oldPropMap.put(brokerParameter[0], brokerParameter[2]);
					}
				}
			}
			boolean isExist = false;
			for (Map.Entry<String, Map<String, String>> newEntry : newConfParaMap.entrySet()) {
				if (newEntry.getKey().equals(oldKey)) {
					isExist = true;
					Map<String, String> newPropMap = newEntry.getValue();

					for (Map.Entry<String, String> oldProp : oldPropMap.entrySet()) {
						String propName = oldProp.getKey();
						String oldPropValue = oldProp.getValue();
						String newPropValue = newPropMap.get(propName);
						if (!oldPropValue.equals(newPropValue)) {
							return true;
						}
					}

				}
			}
			if (!isExist) {
				return true;
			}
		}
		return false;
	}

	/*
	 * Judges if there is any change in setting
	 */
	private boolean isSettingChange() {
		if (oldIntervalSettingMap.size() != newIntervalSettingMap.size()) {
			return true;
		}
		for (Map.Entry<String, BrokerIntervalSetting> oldEntry : oldIntervalSettingMap.entrySet()) {
			String oldKey = oldEntry.getKey();
			BrokerIntervalSetting oldSetting = oldEntry.getValue();

			BrokerIntervalSetting newSetting = newIntervalSettingMap.get(oldKey);
			if (null == newSetting) {
				return true;
			} else {
				if (oldSetting.isOn() != newSetting.isOn()) {
					return true;
				}
				if (!oldSetting.getInterval().equals(newSetting.getInterval())) {
					return true;
				}
			}
		}
		return false;
	}

	/*
	 * Creates the button of add, edit, delete
	 */
	private void createDealButton(Composite parent) {
		Composite btnComposite = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		btnComposite.setLayout(layout);
		btnComposite.setLayoutData(new GridData());
		int widthHint = convertHorizontalDLUsToPixels(IDialogConstants.BUTTON_WIDTH);
		GridData data = new GridData(GridData.VERTICAL_ALIGN_CENTER);

		addBtn = new Button(btnComposite, SWT.PUSH);
		addBtn.setText(ADD_BTN_NAME);
		Point minButtonSize = addBtn.computeSize(SWT.DEFAULT, SWT.DEFAULT, true);
		data.widthHint = Math.max(widthHint, minButtonSize.x);
		addBtn.setLayoutData(data);
		addBtn.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				AddAction addAction = new AddAction();
				addAction.run();
			}

		});
		new Label(btnComposite, SWT.NONE);
		editBtn = new Button(btnComposite, SWT.PUSH);
		editBtn.setText(EDIT_BTN_NAME);
		editBtn.setLayoutData(data);
		IStructuredSelection selection = (IStructuredSelection) brokersTableViewer.getSelection();
		if (0 == selection.size()) {
			editBtn.setEnabled(false);
		}
		editBtn.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				EditAction editAction = new EditAction();
				editAction.run();

			}

		});
		new Label(btnComposite, SWT.NONE);
		deleteBtn = new Button(btnComposite, SWT.PUSH);
		deleteBtn.setText(DELETE_BTN_NAME);
		deleteBtn.setLayoutData(data);
		if (0 == selection.size()) {
			deleteBtn.setEnabled(false);
		}
		deleteBtn.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				DeleteAction deleteAction = new DeleteAction();
				deleteAction.run();

			}

		});
		brokersTableViewer.addPostSelectionChangedListener(new ISelectionChangedListener() {

			public void selectionChanged(SelectionChangedEvent event) {
				IStructuredSelection selection = (IStructuredSelection) (event.getSelection());
				if (0 != selection.size()) {
					editBtn.setEnabled(true);
					if (userInfo.getCasAuth() != CasAuthType.AUTH_ADMIN) {
						deleteBtn.setEnabled(false);
					} else {
						deleteBtn.setEnabled(true);
					}
				} else {
					editBtn.setEnabled(false);
					deleteBtn.setEnabled(false);
				}

			}
		});

	}

	/**
	 * * An action that is an inner class in order to execute deleting the
	 * parameter of a broker
	 * 
	 * @author lizhiqiang
	 * @version 1.0 - 2009-5-23 created by lizhiqiang
	 */
	private class DeleteAction extends
			Action {

		private Map<String, String> brokerMap;

		@SuppressWarnings("unchecked")
		public DeleteAction() {
			setText(delActionTxt);
			IStructuredSelection selection = (IStructuredSelection) brokersTableViewer.getSelection();
			brokerMap = (Map<String, String>) (selection.getFirstElement());
			String serverName = node.getServer().getLabel();
			String brokerName = brokerMap.get("0");
			newIntervalSettingMap.remove(serverName + "_" + brokerMap.get("0"));
			deletedBrokerLst.add(brokerName);
		}

		public void run() {
			brokerList.remove(brokerMap);
			brokersTableViewer.remove(brokerMap);
		}

		/*
		 * (non-Javadoc) Method declared on IAction.
		 */
		public boolean isEnabled() {
			if (null == brokerMap) {
				return false;
			}
			return true;
		}
	}

	/**
	 * An action that is an inner class in order to execute editing the
	 * parameter of a broker
	 * 
	 * @author lizhiqiang
	 * @version 1.0 - 2009-5-23 created by lizhiqiang
	 */
	private class EditAction extends
			Action {
		private Map<String, String> brokerMap;
		private BrokerIntervalSetting brokerIntervalSetting;

		@SuppressWarnings("unchecked")
		public EditAction() {
			setText(editActionTxt);
			IStructuredSelection selection = (IStructuredSelection) brokersTableViewer.getSelection();
			brokerMap = (Map<String, String>) (selection.getFirstElement());
			String serverName = node.getServer().getLabel();
			brokerIntervalSetting = newIntervalSettingMap.get(serverName + "_"
					+ brokerMap.get("0"));
		}

		@SuppressWarnings("unchecked")
		public void run() {
			String sMasterShmId = masterShmIdTxt.getText().trim();
			List<Map<String, String>> brokerLst2Dialog = (List<Map<String, String>>) brokersTableViewer.getInput();
			BrokerParameterDialog brokerParameterDialog = new BrokerParameterDialog(
					getShell(), AddEditType.EDIT, node, brokerLst2Dialog,
					sMasterShmId, brokerMap, brokerIntervalSetting);
			if (brokerParameterDialog.open() == Dialog.OK) {
				BrokerIntervalSetting brokerIntervalSetting = brokerParameterDialog.getBrokerIntervalSetting();
				String serverName = node.getServer().getLabel();
				brokerIntervalSetting.setServerName(serverName);
				newIntervalSettingMap.put(serverName + "_"
						+ brokerIntervalSetting.getBrokerName(),
						brokerIntervalSetting);

				brokersTableViewer.refresh(brokerMap);

			}
		}

		/*
		 * (non-Javadoc) Method declared on IAction.
		 */
		public boolean isEnabled() {
			if (null == brokerMap) {
				return false;
			}
			return true;
		}
	}

	/**
	 * An action that is an inner class in order to execute adding the parameter
	 * of a broker
	 * 
	 * @author lizhiqiang
	 * @version 1.0 - 2009-5-23 created by lizhiqiang
	 */
	private class AddAction extends
			Action {

		public AddAction() {
			setText(addActionTxt);
		}

		@SuppressWarnings("unchecked")
		public void run() {
			String sMasterShmId = masterShmIdTxt.getText().trim();
			List<Map<String, String>> brokerLst2Dialog = (List<Map<String, String>>) brokersTableViewer.getInput();
			BrokerParameterDialog brokerParameterDialog = new BrokerParameterDialog(
					getShell(), AddEditType.ADD, node, brokerLst2Dialog,
					sMasterShmId);
			if (brokerParameterDialog.open() == Dialog.OK) {
				Map<String, String> brokerMap = brokerParameterDialog.getBrokerMap();
				BrokerIntervalSetting brokerIntervalSetting = brokerParameterDialog.getBrokerIntervalSetting();
				String serverName = node.getServer().getLabel();
				brokerIntervalSetting.setServerName(serverName);
				newIntervalSettingMap.put(serverName + "_"
						+ brokerIntervalSetting.getBrokerName(),
						brokerIntervalSetting);
				brokerList.add(brokerMap);
				brokersTableViewer.add(brokerMap);
			}
		}
	}

	/**
	 * MasterShmIdModifyListener Response to the modification of Master_shm_id
	 * 
	 * @author cn12978
	 * @version 1.0 - 2009-6-1 created by cn12978
	 */
	private class MasterShmIdModifyListener implements
			ModifyListener {

		/* (non-Javadoc)
		 * @see org.eclipse.swt.events.ModifyListener#modifyText(org.eclipse.swt.events.ModifyEvent)
		 */
		public void modifyText(ModifyEvent e) {
			String sMasterShmId = masterShmIdTxt.getText().trim();
			if (sMasterShmId.length() <= 0) {
				setErrorMessage(Messages.errMasterShmId);
				setValid(false);
				return;
			}
			if (sMasterShmId.length() > 6) {
				setErrorMessage(Messages.errMasterShmId);
				setValid(false);
				return;
			}
			int port = Integer.parseInt(sMasterShmId);
			if (port < 1024 || port > 65535) {
				setErrorMessage(Messages.errMasterShmId);
				setValid(false);
				return;
			}
			for (Map<String, String> map : brokerList) {
				String appServerShmId = map.get(ConfConstants.APPL_SERVER_SHM_ID);
				if (appServerShmId.equals(sMasterShmId)) {
					setErrorMessage(Messages.errMasterShmIdSamePort);
					setValid(false);
					return;
				}
			}
			setErrorMessage(null);
			setValid(true);
		}
	}

	/*
	 * A class that verify the entering of volumeText
	 */
	private class NumberVerifyListener implements
			VerifyListener {

		public void verifyText(VerifyEvent e) {
			if (e.text.equals("")) {
				return;
			}
			if (!ValidateUtil.isNumber(e.text)) {
				e.doit = false;
			} else {
				e.doit = true;
			}
		}
	}

}
