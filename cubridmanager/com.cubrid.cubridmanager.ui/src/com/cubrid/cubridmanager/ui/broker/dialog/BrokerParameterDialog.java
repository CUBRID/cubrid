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
package com.cubrid.cubridmanager.ui.broker.dialog;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.eclipse.jface.dialogs.IDialogConstants;
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
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.model.AddEditType;
import com.cubrid.cubridmanager.core.common.model.CasAuthType;
import com.cubrid.cubridmanager.core.common.model.ConfConstants;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.OsInfoType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.broker.Messages;
import com.cubrid.cubridmanager.ui.broker.editor.internal.BrokerIntervalSetting;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.SWTResourceManager;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * A dialog that shows all brokers information and users can <code>add</code>,<code>edit</code>,
 * <code>delete</code> broker.
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-3-31 created by lizhiqiang
 */
public class BrokerParameterDialog extends
		CMTitleAreaDialog {

	private Text nameTxt;
	private Button refreshBtn;
	private Text intervalTxt;
	private String[] columnNameArrs;
	private TableViewer paraTableViewer;
	private Table paraTable;
	private Map<String, String> brokerMap;
	private BrokerIntervalSetting brokerIntervalSetting;
	private ICubridNode cubridNode;
	private boolean isOkenable[];
	private List<Map<String, String>> brokerLst;

	private AddEditType operation;
	private ServerUserInfo userInfo;
	private String editTitle;
	private String addTitle = Messages.addTitle;
	private String editMsg;
	private String addMsg = Messages.addMsg;
	private String tblParameter = Messages.paraTblParameter;
	private String tblValueType = Messages.paraTblValueType;
	private String tblParamValue = Messages.paraTblParamValue;
	private String brokerNameLbl = Messages.brokerNameLbl;
	private static final String REFRESH_INTERVAL_SETTING = Messages.paraRefreshNameOfTap;
	private static final String BROKER_PARAMETER = Messages.paraParameterNameOfTap;
	protected static final String REDUPLICATED_BROKER_NAME = Messages.errReduplicateName;
	protected static final String ERROR_BROKER_NAME = Messages.errBrokerName;
	private String errReduplicatePort = Messages.errReduplicatePort;
	private String shellEditTitle = Messages.shellEditTitle;
	private String shellAddTitle = Messages.shellAddTitle;
	private String masterShmId;

	/**
	 * The Constructor
	 * 
	 * @param parentShell
	 */
	public BrokerParameterDialog(Shell parentShell, AddEditType operation,
			ICubridNode node, List<Map<String, String>> brokerLst,
			String masterShmId) {
		super(parentShell);
		this.operation = operation;
		isOkenable = new boolean[3];
		this.masterShmId = masterShmId;
		cubridNode = node;
		this.brokerLst = brokerLst;
		userInfo = cubridNode.getServer().getServerInfo().getLoginedUserInfo();
		assert (null != userInfo);
	}

	/**
	 * The Constructor
	 * 
	 * @param parentShell
	 */
	public BrokerParameterDialog(Shell parentShell, AddEditType operation,
			ICubridNode node, List<Map<String, String>> brokerLst,
			String masterShmId, Map<String, String> map,
			BrokerIntervalSetting brokerIntervalSetting) {
		this(parentShell, operation, node, brokerLst, masterShmId);
		this.brokerMap = map;
		if (null == brokerIntervalSetting) {
			brokerIntervalSetting = new BrokerIntervalSetting();
			brokerIntervalSetting.setBrokerName(brokerMap.get("0"));
		}
		this.brokerIntervalSetting = brokerIntervalSetting;
	}

	/**
	 * 
	 * @param parentShell
	 */
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp,
				CubridManagerHelpContextIDs.brokerProperty);

		Composite composite = new Composite(parentComp, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);

		GridData gd_composite = new GridData(SWT.FILL, SWT.FILL, true, true);
		gd_composite.heightHint = 380;
		composite.setLayoutData(gd_composite);

		Composite nameComp = new Composite(composite, SWT.NONE);
		nameComp.setLayout(new GridLayout(2, false));
		nameComp.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, false));

		final Label brokerNameLabel = new Label(nameComp, SWT.NONE);
		brokerNameLabel.setText(brokerNameLbl);

		nameTxt = new Text(nameComp, SWT.BORDER);
		final GridData gd_nameTxt = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		nameTxt.setLayoutData(gd_nameTxt);

		TabFolder tabFolder = new TabFolder(composite, SWT.NONE);
		final GridData gd_tabFolder = new GridData(SWT.FILL, SWT.FILL, true,
				true);
		tabFolder.setLayoutData(gd_tabFolder);

		tabFolder.setLayout(new GridLayout());

		TabItem item = new TabItem(tabFolder, SWT.NONE);
		item.setText(BROKER_PARAMETER);
		item.setControl(createParamComp(tabFolder));

		item = new TabItem(tabFolder, SWT.NONE);
		item.setText(REFRESH_INTERVAL_SETTING);
		item.setControl(createRefreshComp(tabFolder));
		initial();
		if (operation == AddEditType.ADD) {
			setTitle(addTitle);
			setMessage(addMsg);
			nameTxt.setEnabled(true);
			getShell().setText(shellAddTitle);
			brokerMap = new HashMap<String, String>();
			isOkenable[0] = false;
			isOkenable[1] = false;
			isOkenable[2] = false;
		} else {
			editTitle = Messages.bind(Messages.editTitle, brokerMap.get("0"));
			editMsg = Messages.bind(Messages.editMsg, brokerMap.get("0"));
			setTitle(editTitle);
			setMessage(editMsg);
			getShell().setText(shellEditTitle);
			nameTxt.setEnabled(false);
			isOkenable[0] = true;
			isOkenable[1] = true;
			isOkenable[2] = true;
		}

		nameTxt.addModifyListener(new ModifyListener() {

			public void modifyText(ModifyEvent e) {
				String content = ((Text) e.widget).getText().trim();
				if (content.length() == 0) {
					isOkenable[0] = false;
				} else if (content.equalsIgnoreCase("broker")) {
					setErrorMessage(ERROR_BROKER_NAME);
					isOkenable[0] = false;
				} else {
					boolean hasName = false;
					if (brokerLst != null) {
						for (Map<String, String> brokerInfo : brokerLst) {
							if (content.equalsIgnoreCase(brokerInfo.get("0"))) {
								hasName = true;
								break;
							}
						}
					}
					if (hasName) {
						isOkenable[0] = false;
						setErrorMessage(REDUPLICATED_BROKER_NAME);
					} else {
						isOkenable[0] = true;
						setErrorMessage(null);
					}
				}
				enableOk();
			}

		});
		return parentComp;

	}

	/*
	 * Initializes the parameters of dialog
	 */
	private void initial() {
		if (null != brokerMap) {
			nameTxt.setText(brokerMap.get("0"));
		}
		OsInfoType osInfoType = cubridNode.getServer().getServerInfo().getServerOsInfo();
		List<Map<String, String>> parameterList = new ArrayList<Map<String, String>>();
		for (int i = 2; i < ConfConstants.brokerParameters.length; i++) {
			Map<String, String> dataMap = new HashMap<String, String>();
			String para = ConfConstants.brokerParameters[i][0];
			String type = ConfConstants.brokerParameters[i][1];
			String defaultValue = ConfConstants.brokerParameters[i][2];
			String bgColor = "grey";
			if (para.equals(ConfConstants.APPL_SERVER_PORT)) {
				if (osInfoType != OsInfoType.NT) {
					continue;
				} else {
					if (brokerMap != null
							&& brokerMap.get(ConfConstants.BROKER_PORT) != null) {
						int serverPortValue = Integer.parseInt(brokerMap.get(ConfConstants.BROKER_PORT)) + 1;
						String newValue = Integer.toString(serverPortValue);
						defaultValue = brokerMap.get(para);
						if (null != defaultValue) {
							if (!newValue.equals(defaultValue)) {
								bgColor = "white";
							}
						}
						defaultValue = newValue;
					}
				}
			}

			if (brokerMap != null && brokerMap.get(para) != null) {
				String newValue = brokerMap.get(para);
				if (i > 11
						&& !defaultValue.equals(newValue)
						&& !para.equalsIgnoreCase(ConfConstants.APPL_SERVER_PORT)) {
					bgColor = "white";
				}
				defaultValue = newValue;
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

		//initialize refresh
		if (operation == AddEditType.EDIT) {
			boolean state = brokerIntervalSetting.isOn();
			String interval = brokerIntervalSetting.getInterval();
			if (state) {
				refreshBtn.setSelection(true);
				intervalTxt.setText(interval);
			} else {
				refreshBtn.setSelection(false);
			}
		}

	}

	/*
	 * Makes a certain column of table can be edited
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
						CommonTool.openErrorBox(Messages.bind(
								Messages.errBrokerPortAndShmId, paramName));
						isValid = false;
					}
					if (isValid) {
						String paramValue = value.toString().trim();
						for (Map<String, String> brokerMap : brokerLst) {
							if (operation == AddEditType.EDIT
									&& brokerMap.get("0").equals(
											nameTxt.getText())) {
								isOkenable[1] = true;
								continue;
							}
							String otherPort = brokerMap.get(ConfConstants.BROKER_PORT);
							if (paramValue.equalsIgnoreCase(otherPort)) {
								isOkenable[1] = false;
								CommonTool.openErrorBox(errReduplicatePort);
								isValid = false;
								break;
							} else {
								isOkenable[1] = true;
								isValid = true;
							}
						}
						enableOk();
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
					int shmId = Integer.parseInt(value.toString());
					if (shmId < 1024 || shmId > 65535) {
						CommonTool.openErrorBox(Messages.bind(
								Messages.errBrokerPortAndShmId, paramName));
						isValid = false;
						isOkenable[2] = false;
					}
					if (isValid) {
						String paramValue = value.toString().trim();
						if (paramValue.equals(masterShmId)) {
							isValid = false;
							CommonTool.openErrorBox(Messages.bind(
									Messages.errUseMasterShmId, paramValue));
						}
						for (Map<String, String> brokerMap : brokerLst) {
							if (operation == AddEditType.EDIT
									&& brokerMap.get("0").equals(
											nameTxt.getText())) {
								isOkenable[2] = true;
								continue;
							}
							String otherPort = brokerMap.get(ConfConstants.APPL_SERVER_SHM_ID);
							if (paramValue.equalsIgnoreCase(otherPort)) {
								isOkenable[2] = false;
								CommonTool.openErrorBox(Messages.errReduplicateShmId);
								isValid = false;
								break;
							} else {
								isOkenable[2] = true;
								isValid = true;
							}
						}
						enableOk();
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
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
	}

	/*
	 * Creates the parameter composite
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
		tipLbl.setText(Messages.refreshTitle);
		tipLbl.setLayoutData(gd_tipLbl);

		final Composite radioComp = new Composite(refreshComp, SWT.None);
		final GridData gd_radioComp = new GridData(SWT.FILL, SWT.TOP, true,
				false);
		radioComp.setLayoutData(gd_radioComp);
		radioComp.setLayout(new GridLayout(3, false));

		refreshBtn = new Button(radioComp, SWT.CHECK);
		refreshBtn.setText(Messages.refreshOnLbl);
		refreshBtn.setSelection(false);

		intervalTxt = new Text(radioComp, SWT.BORDER | SWT.RIGHT);
		final GridData gd_intervalTxt = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		intervalTxt.setLayoutData(gd_intervalTxt);
		intervalTxt.setText("1");
		intervalTxt.setEnabled(false);

		final Label secLbl = new Label(radioComp, SWT.NONE);
		final GridData gd_secLbl = new GridData(SWT.LEFT, SWT.TOP, true, false);
		secLbl.setText(Messages.refreshUnitLbl);
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

	@Override
	public void okPressed() {
		performOk();
		super.okPressed();
	}

	/*
	 * Executes tasks when "ok" button pressed 
	 */
	private void performOk() {
		TableItem[] items = paraTable.getItems();

		for (TableItem item : items) {
			String key = item.getText(0).trim();
			if (key.length() != 0) {
				brokerMap.put(key, item.getText(2).trim());
			}
		}

		brokerMap.put("0", nameTxt.getText().trim());
		brokerMap.put("1", brokerMap.get(ConfConstants.BROKER_PORT));

		String brokerName = nameTxt.getText().trim();
		boolean state = refreshBtn.getSelection();
		String interval = intervalTxt.getText().trim();
		if (null == brokerIntervalSetting) {
			brokerIntervalSetting = new BrokerIntervalSetting();
			brokerIntervalSetting.setBrokerName(brokerName);
		}
		brokerIntervalSetting.setInterval(interval);
		brokerIntervalSetting.setOn(state);

	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		super.createButtonsForButtonBar(parent);
		if (operation.equals(AddEditType.ADD))
			getButton(IDialogConstants.OK_ID).setEnabled(false);
	}

	/*
	 * Enable the "OK" button
	 * 
	 */
	private void enableOk() {
		boolean is = true;
		for (int i = 0; i < isOkenable.length; i++) {
			is = is && isOkenable[i];
		}
		if (is) {
			getButton(IDialogConstants.OK_ID).setEnabled(true);
		} else {
			getButton(IDialogConstants.OK_ID).setEnabled(false);
		}
	}

	/**
	 * Gets the brokerMap;
	 */
	public Map<String, String> getBrokerMap() {
		return brokerMap;
	}

	/**
	 * Sets the brokerMap
	 * 
	 * @param broker
	 */
	public void setBrokerMap(Map<String, String> brokerMap) {
		this.brokerMap = brokerMap;
	}

	/**
	 * Gets the brokerIntervalSetting;
	 * 
	 * @return
	 */
	public BrokerIntervalSetting getBrokerIntervalSetting() {
		return brokerIntervalSetting;
	}

}
