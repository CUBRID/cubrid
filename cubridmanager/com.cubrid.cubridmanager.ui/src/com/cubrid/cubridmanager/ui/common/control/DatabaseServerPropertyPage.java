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
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import org.eclipse.jface.preference.PreferencePage;
import org.eclipse.jface.viewers.CellEditor;
import org.eclipse.jface.viewers.ICellModifier;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.jface.viewers.TextCellEditor;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
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

import com.cubrid.cubridmanager.core.common.model.ConfConstants;
import com.cubrid.cubridmanager.core.common.task.SetCubridConfParameterTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.OsInfoType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;

/**
 * 
 * CUBRID Database property page for cubrid.conf configuration file
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-4 created by pangqiren
 */
public class DatabaseServerPropertyPage extends
		PreferencePage implements
		ModifyListener {

	private ICubridNode node = null;
	private boolean isAdmin = false;
	private TableViewer advancedOptionTableViewer;
	private Table advancedOptionTable;
	private String[] columnNameArrs;
	private Text dataBufferPageText;
	private Text sortBufferPageText;
	private Text logBufferPageText;
	private Text lockEscalationText;
	private Text lockTimeOutText;
	private Text deadLockDetectIntervalText;
	private Text checkPointIntervalText;
	private Combo isolationLevelCombo;
	private Text maxClientsText;
	private Button autoRestartServerButton;
	private Button replicationButton;
	private Button jspButton;
	private Text cubridPortIdText;
	private boolean isChanged = false;
	private boolean isApply = false;
	private boolean isCommonProperty = true;
	private Map<String, Map<String, String>> initialValueMap = new HashMap<String, Map<String, String>>();
	private Map<String, String> dbBaseParameterValueMap = new HashMap<String, String>();

	/**
	 * The constructor
	 * 
	 * @param node
	 * @param name
	 * @param isCommonProperty
	 */
	public DatabaseServerPropertyPage(ICubridNode node, String name,
			boolean isCommonProperty) {
		super(name, null);
		noDefaultAndApplyButton();
		this.node = node;
		this.isCommonProperty = isCommonProperty;
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
		whs.setHelp(parent, CubridManagerHelpContextIDs.serverProperty);

		Composite composite = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		composite.setLayout(layout);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));

		TabFolder tabFolder = new TabFolder(composite, SWT.NONE);
		tabFolder.setLayoutData(new GridData(GridData.FILL_BOTH));
		layout = new GridLayout();
		tabFolder.setLayout(layout);

		TabItem item = new TabItem(tabFolder, SWT.NONE);
		item.setText(Messages.tabItemGeneral);
		item.setControl(createGeneralComp(tabFolder));

		item = new TabItem(tabFolder, SWT.NONE);
		item.setText(Messages.tabItemAdvanceOptions);
		item.setControl(createAdvancedComp(tabFolder));

		initial();
		return composite;
	}

	/**
	 * 
	 * Create general tabItem composite
	 * 
	 * @param parent
	 * @return
	 */
	private Composite createGeneralComp(Composite parent) {
		Composite composite = new Composite(parent, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.numColumns = 2;
		composite.setLayout(layout);

		Label dataBufferPageLabel = new Label(composite, SWT.LEFT);
		dataBufferPageLabel.setText(ConfConstants.data_buffer_pages + ":");
		dataBufferPageLabel.setLayoutData(CommonTool.createGridData(1, 1, -1,
				-1));
		dataBufferPageText = new Text(composite, SWT.LEFT | SWT.BORDER);
		dataBufferPageText.setTextLimit(8);
		dataBufferPageText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (!isAdmin) {
			dataBufferPageText.setEditable(false);
		}

		Label sortBufferPageLabel = new Label(composite, SWT.LEFT);
		sortBufferPageLabel.setText(ConfConstants.sort_buffer_pages + ":");
		sortBufferPageLabel.setLayoutData(CommonTool.createGridData(1, 1, -1,
				-1));
		sortBufferPageText = new Text(composite, SWT.LEFT | SWT.BORDER);
		sortBufferPageText.setTextLimit(8);
		sortBufferPageText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (!isAdmin) {
			sortBufferPageText.setEditable(false);
		}

		Label logBufferPageLabel = new Label(composite, SWT.LEFT);
		logBufferPageLabel.setText(ConfConstants.log_buffer_pages + ":");
		logBufferPageLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		logBufferPageText = new Text(composite, SWT.LEFT | SWT.BORDER);
		logBufferPageText.setTextLimit(8);
		logBufferPageText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (!isAdmin) {
			logBufferPageText.setEditable(false);
		}

		Label lockEscalationLabel = new Label(composite, SWT.LEFT);
		lockEscalationLabel.setText(ConfConstants.lock_escalation + ":");
		lockEscalationLabel.setLayoutData(CommonTool.createGridData(1, 1, -1,
				-1));
		lockEscalationText = new Text(composite, SWT.LEFT | SWT.BORDER);
		lockEscalationText.setTextLimit(8);
		lockEscalationText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (!isAdmin) {
			lockEscalationText.setEditable(false);
		}

		Label lockTimeOutLabel = new Label(composite, SWT.LEFT);
		lockTimeOutLabel.setText(ConfConstants.lock_timeout_in_secs + ":");
		lockTimeOutLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		lockTimeOutText = new Text(composite, SWT.LEFT | SWT.BORDER);
		lockTimeOutText.setTextLimit(8);
		lockTimeOutText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (!isAdmin) {
			lockTimeOutText.setEditable(false);
		}

		Label deadLockDetectIntervalLabel = new Label(composite, SWT.LEFT);
		deadLockDetectIntervalLabel.setText(ConfConstants.deadlock_detection_interval_in_secs
				+ ":");
		deadLockDetectIntervalLabel.setLayoutData(CommonTool.createGridData(1,
				1, -1, -1));
		deadLockDetectIntervalText = new Text(composite, SWT.LEFT | SWT.BORDER);
		deadLockDetectIntervalText.setTextLimit(8);
		deadLockDetectIntervalText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (!isAdmin) {
			deadLockDetectIntervalText.setEditable(false);
		}

		Label checkPointIntervalLabel = new Label(composite, SWT.LEFT);
		checkPointIntervalLabel.setText(ConfConstants.checkpoint_interval_in_mins
				+ ":");
		checkPointIntervalLabel.setLayoutData(CommonTool.createGridData(1, 1,
				-1, -1));
		checkPointIntervalText = new Text(composite, SWT.LEFT | SWT.BORDER);
		checkPointIntervalText.setTextLimit(8);
		checkPointIntervalText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (!isAdmin) {
			checkPointIntervalText.setEditable(false);
		}

		Label isolationLevelLabel = new Label(composite, SWT.LEFT);
		isolationLevelLabel.setText(ConfConstants.isolation_level + ":");
		isolationLevelLabel.setLayoutData(CommonTool.createGridData(1, 1, -1,
				-1));
		isolationLevelCombo = new Combo(composite, SWT.LEFT | SWT.BORDER
				| SWT.READ_ONLY);
		isolationLevelCombo.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (!isAdmin) {
			isolationLevelCombo.setEnabled(false);
		}

		Label cubridPortIdLabel = new Label(composite, SWT.LEFT);
		cubridPortIdLabel.setText(ConfConstants.cubrid_port_id + ":");
		cubridPortIdLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		cubridPortIdText = new Text(composite, SWT.LEFT | SWT.BORDER);
		cubridPortIdText.setTextLimit(8);
		cubridPortIdText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (!isAdmin) {
			cubridPortIdText.setEditable(false);
		}

		Label maxClientsLabel = new Label(composite, SWT.LEFT);
		maxClientsLabel.setText(ConfConstants.max_clients + ":");
		maxClientsLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		maxClientsText = new Text(composite, SWT.LEFT | SWT.BORDER);
		maxClientsText.setTextLimit(8);
		maxClientsText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		if (!isAdmin) {
			maxClientsText.setEditable(false);
		}

		autoRestartServerButton = new Button(composite, SWT.LEFT | SWT.CHECK);
		autoRestartServerButton.setText(ConfConstants.auto_restart_server);
		autoRestartServerButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		if (!isAdmin) {
			autoRestartServerButton.setEnabled(false);
		}

		replicationButton = new Button(composite, SWT.LEFT | SWT.CHECK);
		replicationButton.setText(ConfConstants.replication);
		replicationButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		if (!isAdmin) {
			replicationButton.setEnabled(false);
		}
		if (this.node.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
			replicationButton.setLayoutData(CommonTool.createGridData(
					GridData.FILL_HORIZONTAL, 2, 1, 0, 0));
			replicationButton.setEnabled(false);
			replicationButton.setVisible(false);
		}

		jspButton = new Button(composite, SWT.LEFT | SWT.CHECK);
		jspButton.setText(ConfConstants.java_stored_procedure);
		jspButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		if (!isAdmin) {
			jspButton.setEnabled(false);
		}
		return composite;
	}

	/**
	 * 
	 * Create the advanced tabItem composite
	 * 
	 * @param parent
	 * @return
	 */
	private Composite createAdvancedComp(Composite parent) {
		columnNameArrs = new String[] { Messages.tblColumnParameterName,
				Messages.tblColumnParameterType, Messages.tblColumnValueType,
				Messages.tblColumnParameterValue };
		advancedOptionTableViewer = CommonTool.createCommonTableViewer(parent,
				null, columnNameArrs, CommonTool.createGridData(
						GridData.FILL_BOTH, 1, 1, -1, 200));
		advancedOptionTable = advancedOptionTableViewer.getTable();
		if (isAdmin) {
			linkEditorForTable();
		}
		return advancedOptionTable;
	}

	/**
	 * 
	 * Link editor for table
	 * 
	 */
	private void linkEditorForTable() {
		advancedOptionTableViewer.setColumnProperties(columnNameArrs);
		CellEditor[] editors = new CellEditor[4];
		editors[0] = null;
		editors[1] = null;
		editors[2] = null;
		editors[3] = new TextCellEditor(advancedOptionTable);
		advancedOptionTableViewer.setCellEditors(editors);
		advancedOptionTableViewer.setCellModifier(new ICellModifier() {
			@SuppressWarnings("unchecked")
			public boolean canModify(Object element, String property) {
				if (property.equals(columnNameArrs[3])) {
					return true;
				}
				return false;
			}

			@SuppressWarnings("unchecked")
			public Object getValue(Object element, String property) {
				Map<String, String> map = (Map<String, String>) element;
				if (property.equals(columnNameArrs[3])) {
					return map.get("3");
				}
				return null;
			}

			@SuppressWarnings("unchecked")
			public void modify(Object element, String property, Object value) {
				if (element instanceof Item) {
					element = ((Item) element).getData();
				}
				Map<String, String> map = (Map<String, String>) element;
				String parameter = map.get("0");
				String type = map.get("2");
				String paraValue = map.get("3");
				boolean isValid = true;
				String errorMsg = null;
				if (type.startsWith("bool")) {
					if (!value.toString().equalsIgnoreCase("yes")
							&& !value.toString().equalsIgnoreCase("no")) {
						isValid = false;
						errorMsg = Messages.bind(Messages.errYesNoParameter,
								new String[] { parameter });
					}
				} else if (type.startsWith("int")) {
					boolean isInt = true;
					String paraVal = value.toString();
					if (parameter.equals(ConfConstants.backup_volume_max_size_bytes)
							|| parameter.equals(ConfConstants.thread_stacksize)) {
						String[] valArr = paraVal.split("\\*");
						int specialValue = 1;
						if (valArr == null) {
							isInt = false;
						} else {
							for (int i = 0; i < valArr.length; i++) {
								if (!ValidateUtil.isInteger(valArr[i])
										|| valArr[i].length() > 8) {
									isInt = false;
									break;
								} else {
									specialValue = specialValue
											* Integer.parseInt(valArr[i]);
								}
							}
						}
						if (isInt)
							paraVal = String.valueOf(specialValue);
					} else {
						isInt = ValidateUtil.isInteger(paraVal);
					}
					if (paraVal.length() > 8) {
						isValid = false;
					}
					if (!isInt || !isValid) {
						errorMsg = Messages.bind(Messages.errOnlyInteger,
								new String[] { parameter });
						isValid = false;
					} else {
						int intValue = Integer.parseInt(paraVal);
						if (parameter.equals(ConfConstants.backup_volume_max_size_bytes)) {
							if (intValue != -1 && intValue < 32 * 1024) {
								isValid = false;
								errorMsg = Messages.bind(
										Messages.errBackupVolumeMaxSize,
										new String[] { parameter });
							}
						} else if (parameter.equals(ConfConstants.csql_history_num)) {
							if (intValue < 1 || intValue > 200) {
								isValid = false;
								errorMsg = Messages.bind(
										Messages.errCsqlHistoryNum,
										new String[] { parameter });
							}
						} else if (parameter.equals(ConfConstants.group_commit_interval_in_msecs)) {
							if (intValue < 0) {
								isValid = false;
								errorMsg = Messages.bind(
										Messages.errGroupCommitInterval,
										new String[] { parameter });
							}
						} else if (parameter.equals(ConfConstants.index_scan_oid_buffer_pages)) {
							if (intValue < 1 || intValue > 16) {
								isValid = false;
								errorMsg = Messages.bind(
										Messages.errIndexScanInOidBuffPage,
										new String[] { parameter });
							}
						} else if (parameter.equals(ConfConstants.insert_execution_mode)) {
							if (intValue < 1 || intValue > 7) {
								isValid = false;
								errorMsg = Messages.bind(
										Messages.errInsertExeMode,
										new String[] { parameter });
							}
						} else if (parameter.equals(ConfConstants.lock_timeout_message_type)) {
							if (intValue < 0 || intValue > 2) {
								isValid = false;
								errorMsg = Messages.bind(
										Messages.errLockTimeOutMessageType,
										new String[] { parameter });
							}
						} else if (parameter.equals(ConfConstants.query_cache_mode)) {
							if (intValue < 0 || intValue > 2) {
								isValid = false;
								errorMsg = Messages.bind(
										Messages.errQueryCachMode,
										new String[] { parameter });
							}
						} else if (parameter.equals(ConfConstants.temp_file_memory_size_in_pages)) {
							if (intValue < 0 || intValue > 20) {
								isValid = false;
								errorMsg = Messages.bind(
										Messages.errTempFileMemorySize,
										new String[] { parameter });
							}
						} else if (parameter.equals(ConfConstants.thread_stacksize)) {
							if (intValue < 64 * 1024) {
								isValid = false;
								errorMsg = Messages.bind(
										Messages.errThreadStackSize,
										new String[] { parameter });
							}
						}
					}
				} else if (type.startsWith("float")) {
					boolean isFloat = ValidateUtil.isDouble(value.toString());
					if (!isFloat) {
						isValid = false;
						errorMsg = Messages.bind(Messages.errOnlyFloat,
								new String[] { parameter });
					} else {
						float fValue = Float.parseFloat(value.toString());
						if (parameter.equals(ConfConstants.unfill_factor)) {
							if (fValue < 0 || fValue > 0.3) {
								isValid = false;
								errorMsg = Messages.bind(
										Messages.errUnfillFactor,
										new String[] { parameter });
							}
						}
					}
				} else if (type.startsWith("string")) {
					String valueStr = value.toString().trim();
					int start = type.indexOf("(");
					int end = type.lastIndexOf(")");
					if (start > 0 && start < end) {
						String valueStrs = type.substring(start + 1, end);
						String[] values = valueStrs.split("\\|");
						boolean isExist = false;
						for (String val : values) {
							if (valueStr.equalsIgnoreCase(val)) {
								isExist = true;
								break;
							}
						}
						if (!isExist) {
							isValid = false;
							errorMsg = Messages.bind(
									Messages.errParameterValue,
									new String[] { parameter });
						}
					}
				}
				if (!isValid && errorMsg != null) {
					CommonTool.openErrorBox(errorMsg);
				}
				if (isValid && property.equals(columnNameArrs[3])) {
					if (!paraValue.equals(value)) {
						isChanged = true;
						map.put("3", value.toString());
						Map<String, Map<String, String>> confParaMap = node.getServer().getServerInfo().getCubridConfParaMap();
						Map<String, String> parameterMap = null;
						if (confParaMap == null) {
							confParaMap = new HashMap<String, Map<String, String>>();
							node.getServer().getServerInfo().setCubridConfParaMap(
									confParaMap);
						}
						String sectionName = "";
						if (isCommonProperty) {
							sectionName = ConfConstants.common_section_name;
							parameterMap = confParaMap.get(ConfConstants.common_section_name);
						} else {
							sectionName = "[@" + node.getLabel() + "]";
							parameterMap = confParaMap.get(sectionName);
						}
						if (parameterMap == null) {
							parameterMap = new HashMap<String, String>();
							confParaMap.put(sectionName, parameterMap);
						}
						if (isChanged(parameter, value.toString())) {
							String str = value.toString();
							if (parameter.equals(ConfConstants.backup_volume_max_size_bytes)
									|| parameter.equals(ConfConstants.thread_stacksize)) {
								String[] valArr = str.split("\\*");
								int intValue = 1;
								for (int i = 0; valArr != null
										&& i < valArr.length; i++) {
									intValue = intValue
											* Integer.parseInt(valArr[i]);
								}
								str = String.valueOf(intValue);
							}
							if (str.trim().length() == 0) {
								parameterMap.remove(parameter);
							} else {
								parameterMap.put(parameter, str);
							}
						}
					}
				}
				advancedOptionTableViewer.refresh();
			}
		});
	}

	/**
	 * 
	 * initial the page content
	 * 
	 */
	private void initial() {
		Map<String, Map<String, String>> confParaMap = node.getServer().getServerInfo().getCubridConfParaMap();
		if (confParaMap != null) {
			Iterator<Map.Entry<String, Map<String, String>>> confParaMapIt = confParaMap.entrySet().iterator();
			while (confParaMapIt.hasNext()) {
				Map.Entry<String, Map<String, String>> entry = confParaMapIt.next();
				String key = entry.getKey();
				Map<String, String> map = entry.getValue();
				if (map != null) {
					Map<String, String> sectionMap = new HashMap<String, String>();
					Iterator<Map.Entry<String, String>> mapIt = map.entrySet().iterator();
					while (mapIt.hasNext()) {
						Map.Entry<String, String> mapEntry = mapIt.next();
						String mapKey = mapEntry.getKey();
						String mapValue = mapEntry.getValue();
						sectionMap.put(mapKey, mapValue);
					}
					initialValueMap.put(key, sectionMap);
				}
			}
		}
		for (int i = 0; i < ConfConstants.dbBaseParameters.length; i++) {
			String key = ConfConstants.dbBaseParameters[i][0];
			String value = ConfConstants.dbBaseParameters[i][1];
			dbBaseParameterValueMap.put(key, value);
		}
		defaultValue();
		dataBufferPageText.addModifyListener(this);
		sortBufferPageText.addModifyListener(this);
		logBufferPageText.addModifyListener(this);
		lockEscalationText.addModifyListener(this);
		lockTimeOutText.addModifyListener(this);
		deadLockDetectIntervalText.addModifyListener(this);
		checkPointIntervalText.addModifyListener(this);
		cubridPortIdText.addModifyListener(this);
		maxClientsText.addModifyListener(this);
	}

	/**
	 * 
	 * Restore the default value
	 * 
	 */
	private void defaultValue() {
		isolationLevelCombo.setItems(new String[] {
				ConfConstants.TRAN_SERIALIZABLE,
				ConfConstants.TRAN_REP_CLASS_REP_INSTANCE,
				ConfConstants.TRAN_REP_CLASS_COMMIT_INSTANCE,
				ConfConstants.TRAN_REP_CLASS_UNCOMMIT_INSTANCE,
				ConfConstants.TRAN_COMMIT_CLASS_COMMIT_INSTANCE,
				ConfConstants.TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE });
		Map<String, String> paraMap = null;
		Map<String, String> comonParaMap = initialValueMap.get(ConfConstants.common_section_name);
		if (isCommonProperty) {
			paraMap = comonParaMap;
		} else {
			paraMap = initialValueMap.get("[@" + node.getLabel() + "]");
		}

		dataBufferPageText.setText(dbBaseParameterValueMap.get(ConfConstants.data_buffer_pages));
		if (comonParaMap != null) {
			String str = comonParaMap.get(ConfConstants.data_buffer_pages);
			if (str != null && str.trim().length() > 0) {
				dataBufferPageText.setText(str);
			}
		}
		sortBufferPageText.setText(dbBaseParameterValueMap.get(ConfConstants.sort_buffer_pages));
		if (comonParaMap != null) {
			String str = comonParaMap.get(ConfConstants.sort_buffer_pages);
			if (str != null && str.trim().length() > 0) {
				sortBufferPageText.setText(str);
			}
		}
		logBufferPageText.setText(dbBaseParameterValueMap.get(ConfConstants.log_buffer_pages));
		if (comonParaMap != null) {
			String str = comonParaMap.get(ConfConstants.log_buffer_pages);
			if (str != null && str.trim().length() > 0) {
				logBufferPageText.setText(str);
			}
		}
		lockEscalationText.setText(dbBaseParameterValueMap.get(ConfConstants.lock_escalation));
		if (comonParaMap != null) {
			String str = comonParaMap.get(ConfConstants.lock_escalation);
			if (str != null && str.trim().length() > 0) {
				lockEscalationText.setText(str);
			}
		}
		lockTimeOutText.setText(dbBaseParameterValueMap.get(ConfConstants.lock_timeout_in_secs));
		if (comonParaMap != null) {
			String str = comonParaMap.get(ConfConstants.lock_timeout_in_secs);
			if (str != null && str.trim().length() > 0) {
				lockTimeOutText.setText(str);
			}
		}
		deadLockDetectIntervalText.setText(dbBaseParameterValueMap.get(ConfConstants.deadlock_detection_interval_in_secs));
		if (comonParaMap != null) {
			String str = comonParaMap.get(ConfConstants.deadlock_detection_interval_in_secs);
			if (str != null && str.trim().length() > 0) {
				deadLockDetectIntervalText.setText(str);
			}
		}
		checkPointIntervalText.setText(dbBaseParameterValueMap.get(ConfConstants.checkpoint_interval_in_mins));
		if (comonParaMap != null) {
			String str = comonParaMap.get(ConfConstants.checkpoint_interval_in_mins);
			if (str != null && str.trim().length() > 0) {
				checkPointIntervalText.setText(str);
			}
		}
		isolationLevelCombo.setText(dbBaseParameterValueMap.get(ConfConstants.isolation_level));
		if (comonParaMap != null) {
			String str = comonParaMap.get(ConfConstants.isolation_level);
			if (str != null && str.trim().length() > 0) {
				str = str.replaceAll("\"", "");
				isolationLevelCombo.setText(str);
			}
		}
		cubridPortIdText.setText(dbBaseParameterValueMap.get(ConfConstants.cubrid_port_id));
		if (comonParaMap != null) {
			String str = comonParaMap.get(ConfConstants.cubrid_port_id);
			if (str != null && str.trim().length() > 0) {
				cubridPortIdText.setText(str);
			}
		}
		maxClientsText.setText(dbBaseParameterValueMap.get(ConfConstants.max_clients));
		if (comonParaMap != null) {
			String str = comonParaMap.get(ConfConstants.max_clients);
			if (str != null && str.trim().length() > 0) {
				maxClientsText.setText(str);
			}
		}
		autoRestartServerButton.setSelection(dbBaseParameterValueMap.get(
				ConfConstants.auto_restart_server).equals("yes"));
		if (comonParaMap != null) {
			String str = comonParaMap.get(ConfConstants.auto_restart_server);
			if (str != null && str.trim().length() > 0) {
				if (str.equals("yes"))
					autoRestartServerButton.setSelection(true);
				else
					autoRestartServerButton.setSelection(false);
			}
		}
		replicationButton.setSelection(dbBaseParameterValueMap.get(
				ConfConstants.replication).equals("yes"));
		if (comonParaMap != null) {
			String str = comonParaMap.get(ConfConstants.replication);
			if (str != null && str.trim().length() > 0) {
				if (str.equals("yes"))
					replicationButton.setSelection(true);
				else
					replicationButton.setSelection(false);
			}
		}
		jspButton.setSelection(dbBaseParameterValueMap.get(
				ConfConstants.java_stored_procedure).equals("yes"));
		if (comonParaMap != null) {
			String str = comonParaMap.get(ConfConstants.java_stored_procedure);
			if (str != null && str.trim().length() > 0) {
				if (str.equals("yes"))
					jspButton.setSelection(true);
				else
					jspButton.setSelection(false);
			}
		}

		if (paraMap != null && !isCommonProperty) {
			String str = paraMap.get(ConfConstants.data_buffer_pages);
			if (str != null && str.trim().length() > 0) {
				dataBufferPageText.setText(str);
			}

			str = paraMap.get(ConfConstants.sort_buffer_pages);
			if (str != null && str.trim().length() > 0) {
				sortBufferPageText.setText(str);
			}

			str = paraMap.get(ConfConstants.log_buffer_pages);
			if (str != null && str.trim().length() > 0) {
				logBufferPageText.setText(str);
			}

			str = paraMap.get(ConfConstants.lock_escalation);
			if (str != null && str.trim().length() > 0) {
				lockEscalationText.setText(str);
			}

			str = paraMap.get(ConfConstants.lock_timeout_in_secs);
			if (str != null && str.trim().length() > 0) {
				lockTimeOutText.setText(str);
			}

			str = paraMap.get(ConfConstants.deadlock_detection_interval_in_secs);
			if (str != null && str.trim().length() > 0) {
				deadLockDetectIntervalText.setText(str);
			}

			str = paraMap.get(ConfConstants.checkpoint_interval_in_mins);
			if (str != null && str.trim().length() > 0) {
				checkPointIntervalText.setText(str);
			}

			str = paraMap.get(ConfConstants.isolation_level);
			if (str != null && str.trim().length() > 0) {
				str = str.replaceAll("\"", "");
				isolationLevelCombo.setText(str);
			}

			str = paraMap.get(ConfConstants.cubrid_port_id);
			if (str != null && str.trim().length() > 0) {
				cubridPortIdText.setText(str);
			}

			str = paraMap.get(ConfConstants.max_clients);
			if (str != null && str.trim().length() > 0) {
				maxClientsText.setText(str);
			}

			str = paraMap.get(ConfConstants.auto_restart_server);
			if (str != null && str.trim().length() > 0) {
				if (str.equals("yes"))
					autoRestartServerButton.setSelection(true);
				else
					autoRestartServerButton.setSelection(false);
			}

			str = paraMap.get(ConfConstants.replication);
			if (str != null && str.trim().length() > 0) {
				if (str.equals("yes"))
					replicationButton.setSelection(true);
				else
					replicationButton.setSelection(false);
			}

			str = paraMap.get(ConfConstants.java_stored_procedure);
			if (str != null && str.trim().length() > 0) {
				if (str.equals("yes"))
					jspButton.setSelection(true);
				else
					jspButton.setSelection(false);
			}
		}
		List<Map<String, String>> advancedParameterList = new ArrayList<Map<String, String>>();
		for (int i = 0; i < ConfConstants.dbAdvancedParameters.length; i++) {
			Map<String, String> dataMap = new HashMap<String, String>();
			String para = ConfConstants.dbAdvancedParameters[i][0];
			String valueType = ConfConstants.dbAdvancedParameters[i][1];
			String defaultValue = ConfConstants.dbAdvancedParameters[i][2];
			String paraType = ConfConstants.dbAdvancedParameters[i][3];
			if (comonParaMap != null && comonParaMap.get(para) != null) {
				defaultValue = comonParaMap.get(para);
			}
			if (paraMap != null && paraMap.get(para) != null)
				defaultValue = paraMap.get(para);
			dataMap.put("0", para);
			dataMap.put("1", paraType);
			dataMap.put("2", valueType);
			dataMap.put("3", defaultValue);
			advancedParameterList.add(dataMap);
		}
		advancedOptionTableViewer.setInput(advancedParameterList);
		for (int i = 0; i < advancedOptionTable.getColumnCount(); i++) {
			advancedOptionTable.getColumn(i).pack();
		}
	}

	/**
	 * When modify page content and check the validation
	 */
	public void modifyText(ModifyEvent e) {
		String dataBufferPage = dataBufferPageText.getText();
		boolean isValidDataBufferPage = ValidateUtil.isInteger(dataBufferPage);
		if (isValidDataBufferPage) {
			int intValue = Integer.parseInt(dataBufferPage);
			if (intValue < 1) {
				isValidDataBufferPage = false;
			}
		}
		if (!isValidDataBufferPage) {
			setErrorMessage(Messages.errDataBufferPages);
			setValid(false);
			return;
		}
		String sortBufferPage = sortBufferPageText.getText();
		boolean isValidSortBufferPage = ValidateUtil.isInteger(sortBufferPage);
		if (isValidSortBufferPage) {
			int intValue = Integer.parseInt(sortBufferPage);
			if (intValue < 1) {
				isValidSortBufferPage = false;
			}
		}
		if (!isValidSortBufferPage) {
			setErrorMessage(Messages.errSortBufferPages);
			setValid(false);
			return;
		}
		String logBufferPage = logBufferPageText.getText();
		boolean isValidLogBufferPage = ValidateUtil.isInteger(logBufferPage);
		if (isValidLogBufferPage) {
			int intValue = Integer.parseInt(logBufferPage);
			if (intValue < 3) {
				isValidLogBufferPage = false;
			}
		}
		if (!isValidLogBufferPage) {
			setErrorMessage(Messages.errLogBufferPages);
			setValid(false);
			return;
		}
		String lockEscalation = lockEscalationText.getText();
		boolean isValidLockEscalation = ValidateUtil.isInteger(lockEscalation);
		if (isValidLockEscalation) {
			int intValue = Integer.parseInt(lockEscalation);
			if (intValue < 5) {
				isValidLockEscalation = false;
			}
		}
		if (!isValidLockEscalation) {
			setErrorMessage(Messages.errLockEscalation);
			setValid(false);
			return;
		}
		String lockTimeOut = lockTimeOutText.getText();
		boolean isValidLockTimeOut = ValidateUtil.isInteger(lockTimeOut);
		if (isValidLockTimeOut) {
			int intValue = Integer.parseInt(lockTimeOut);
			if (intValue < -1) {
				isValidLockTimeOut = false;
			}
		}
		if (!isValidLockTimeOut) {
			setErrorMessage(Messages.errLockTimeout);
			setValid(false);
			return;
		}
		String deadLockDetectInterval = deadLockDetectIntervalText.getText();
		boolean isValidDeadLockDetectInterval = ValidateUtil.isInteger(deadLockDetectInterval);
		if (isValidDeadLockDetectInterval) {
			int intValue = Integer.parseInt(deadLockDetectInterval);
			if (intValue < 1) {
				isValidDeadLockDetectInterval = false;
			}
		}
		if (!isValidDeadLockDetectInterval) {
			setErrorMessage(Messages.errDeadLock);
			setValid(false);
			return;
		}
		String checkPointInterval = checkPointIntervalText.getText();
		boolean isValidCheckPointInterval = ValidateUtil.isInteger(checkPointInterval);
		if (isValidCheckPointInterval) {
			int intValue = Integer.parseInt(checkPointInterval);
			if (intValue < 1) {
				isValidCheckPointInterval = false;
			}
		}
		if (!isValidCheckPointInterval) {
			setErrorMessage(Messages.errCheckpoint);
			setValid(false);
			return;
		}
		String cubridPortId = cubridPortIdText.getText();
		boolean isValidCubridPortId = ValidateUtil.isInteger(cubridPortId);
		if (isValidCubridPortId) {
			int intValue = Integer.parseInt(cubridPortId);
			if (intValue < 1) {
				isValidCubridPortId = false;
			}
		}
		if (!isValidCubridPortId) {
			setErrorMessage(Messages.errCubridPortId);
			setValid(false);
			return;
		}
		String maxClients = maxClientsText.getText();
		boolean isValidMaxClients = ValidateUtil.isInteger(maxClients);
		if (isValidMaxClients) {
			int intValue = Integer.parseInt(maxClients);
			if (intValue < 10) {
				isValidMaxClients = false;
			}
		}
		if (!isValidMaxClients) {
			setErrorMessage(Messages.errMaxClients);
			setValid(false);
			return;
		}
		boolean isValid = isValidDataBufferPage && isValidSortBufferPage
				&& isValidLogBufferPage && isValidLockEscalation
				&& isValidLockTimeOut && isValidDeadLockDetectInterval
				&& isValidCheckPointInterval && isValidCubridPortId
				&& isValidMaxClients;
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
		if (dataBufferPageText == null || dataBufferPageText.isDisposed()) {
			return true;
		}
		if (!isAdmin) {
			return true;
		}
		Map<String, Map<String, String>> confParaMap = node.getServer().getServerInfo().getCubridConfParaMap();
		if (confParaMap == null) {
			confParaMap = new HashMap<String, Map<String, String>>();
			node.getServer().getServerInfo().setCubridConfParaMap(confParaMap);
		}
		Map<String, String> paraMap = null;
		String sectionName = "";
		if (isCommonProperty) {
			sectionName = ConfConstants.common_section_name;
			paraMap = confParaMap.get(ConfConstants.common_section_name);
		} else {
			sectionName = "[@" + node.getLabel() + "]";
			paraMap = confParaMap.get(sectionName);
		}
		if (paraMap == null) {
			paraMap = new HashMap<String, String>();
			confParaMap.put(sectionName, paraMap);
		}

		if (paraMap != null) {
			if (isChanged(ConfConstants.data_buffer_pages,
					dataBufferPageText.getText())) {
				paraMap.put(ConfConstants.data_buffer_pages,
						dataBufferPageText.getText());
				isChanged = true;
			}
			if (isChanged(ConfConstants.sort_buffer_pages,
					sortBufferPageText.getText())) {
				paraMap.put(ConfConstants.sort_buffer_pages,
						sortBufferPageText.getText());
				isChanged = true;
			}
			if (isChanged(ConfConstants.log_buffer_pages,
					logBufferPageText.getText())) {
				paraMap.put(ConfConstants.log_buffer_pages,
						logBufferPageText.getText());
				isChanged = true;
			}
			if (isChanged(ConfConstants.lock_escalation,
					lockEscalationText.getText())) {
				paraMap.put(ConfConstants.lock_escalation,
						lockEscalationText.getText());
				isChanged = true;
			}
			if (isChanged(ConfConstants.lock_timeout_in_secs,
					lockTimeOutText.getText())) {
				paraMap.put(ConfConstants.lock_timeout_in_secs,
						lockTimeOutText.getText());
				isChanged = true;
			}
			if (isChanged(ConfConstants.deadlock_detection_interval_in_secs,
					deadLockDetectIntervalText.getText())) {
				paraMap.put(ConfConstants.deadlock_detection_interval_in_secs,
						deadLockDetectIntervalText.getText());
				isChanged = true;
			}
			if (isChanged(ConfConstants.checkpoint_interval_in_mins,
					checkPointIntervalText.getText())) {
				paraMap.put(ConfConstants.checkpoint_interval_in_mins,
						checkPointIntervalText.getText());
				isChanged = true;
			}
			if (isChanged(ConfConstants.isolation_level, "\""
					+ isolationLevelCombo.getText() + "\"")) {
				paraMap.put(ConfConstants.isolation_level, "\""
						+ isolationLevelCombo.getText() + "\"");
				isChanged = true;
			}
			if (isChanged(ConfConstants.max_clients, maxClientsText.getText())) {
				paraMap.put(ConfConstants.max_clients, maxClientsText.getText());
				isChanged = true;
			}
			if (isChanged(ConfConstants.auto_restart_server,
					autoRestartServerButton.getSelection() ? "yes" : "no")) {
				paraMap.put(ConfConstants.auto_restart_server,
						autoRestartServerButton.getSelection() ? "yes" : "no");
				isChanged = true;
			}
			if (isChanged(ConfConstants.replication,
					replicationButton.getSelection() ? "yes" : "no")) {
				paraMap.put(ConfConstants.replication,
						replicationButton.getSelection() ? "yes" : "no");
				isChanged = true;
			}
			if (isChanged(ConfConstants.java_stored_procedure,
					jspButton.getSelection() ? "yes" : "no")) {
				paraMap.put(ConfConstants.java_stored_procedure,
						jspButton.getSelection() ? "yes" : "no");
				isChanged = true;
			}
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
	 * Check the page content is changed
	 * 
	 * @param paraName
	 * @param uiValue
	 * @return
	 */
	private boolean isChanged(String paraName, String uiValue) {
		String defaultValue = this.dbBaseParameterValueMap.get(paraName);
		if (defaultValue == null) {
			for (int i = 0; i < ConfConstants.dbAdvancedParameters.length; i++) {
				String key = ConfConstants.dbAdvancedParameters[i][0];
				String value = ConfConstants.dbAdvancedParameters[i][2];
				if (key.equals(paraName)) {
					defaultValue = value;
					break;
				}
			}
		}
		Map<String, Map<String, String>> confParaMap = node.getServer().getServerInfo().getCubridConfParaMap();
		if (confParaMap == null && !uiValue.equals(defaultValue)) {
			return true;
		} else if (confParaMap == null && uiValue.equals(defaultValue)) {
			return false;
		} else if (confParaMap == null) {
			return true;
		}
		Map<String, String> paraMap = null;
		Map<String, String> commonParaMap = confParaMap.get(ConfConstants.common_section_name);
		if (!isCommonProperty) {
			paraMap = confParaMap.get("[@" + node.getLabel() + "]");
		}
		if (isCommonProperty) {
			String commonValue = commonParaMap != null ? commonParaMap.get(paraName)
					: null;
			if (commonValue == null && !uiValue.equals(defaultValue)) {
				return true;
			}
			if (commonValue != null && !uiValue.equals(commonValue)) {
				return true;
			}
		} else if (!isCommonProperty) {
			String paraValue = paraMap != null ? paraMap.get(paraName) : null;
			String commonValue = commonParaMap != null ? commonParaMap.get(paraName)
					: null;
			if (paraValue == null && commonValue == null
					&& !uiValue.equals(defaultValue)) {
				return true;
			}
			if (paraValue == null && commonValue != null
					&& !uiValue.equals(commonValue)) {
				return true;
			}
			if (paraValue != null && !uiValue.equals(paraValue)) {
				return true;
			}
		}
		return false;
	}

	/**
	 * 
	 * Perform the task and set cubrid.conf file parameter
	 * 
	 * @param confParaMap
	 */
	private void perform(Map<String, Map<String, String>> confParaMap) {
		SetCubridConfParameterTask task = new SetCubridConfParameterTask(
				node.getServer().getServerInfo());
		task.setConfParameters(confParaMap);
		CommonTaskExec taskExcutor = new CommonTaskExec();
		taskExcutor.addTask(task);
		new ExecTaskWithProgress(taskExcutor).exec(true, true);
		if (taskExcutor.isSuccess()) {
			CommonTool.openInformationBox(Messages.titleSuccess,
					Messages.msgChangeServerParaSuccess);
		}
	}
}
