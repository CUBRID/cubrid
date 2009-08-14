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

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.viewers.CheckStateChangedEvent;
import org.eclipse.jface.viewers.CheckboxTableViewer;
import org.eclipse.jface.viewers.ICheckStateListener;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DbUnloadInfo;
import com.cubrid.cubridmanager.core.cubrid.database.task.LoadDbTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.OsInfoType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.FileNameUtils;
import com.cubrid.cubridmanager.ui.spi.TableViewerSorter;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * Load database will use this dialog to fill in the information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class LoadDatabaseDialog extends
		CMTitleAreaDialog implements
		ModifyListener {
	private Text databaseNameText = null;
	private CubridDatabase database = null;
	private Table unloadInfoTable;
	private Composite selectLoadFileFromSysComposite;
	private Button selectLoadFileFromListButton;
	private Combo databaseCombo;
	private Group unloadFileGroup;
	private Text userNameText;
	private Text loadSchemaText;
	private Button loadSchemaFileSearchButton;
	private Button loadSchemaButton;
	private Button loadObjButton;
	private Text loadObjText;
	private Button loadObjFileSearchButton;
	private Button loadIndexButton;
	private Text loadIndexText;
	private Button loadIndexFileSearchButton;
	private Button loadTriggerButton;
	private Text loadTriggerText;
	private Button loadTriggerFileSearchButton;
	private Button checkSyntaxButton;
	private Button noCheckSyntaxButton;
	private Button commitPeriodButton;
	private Text commitPeriodText;
	private Button estimatedSizeButton;
	private Text estimatedSizeText;
	private Button oidButton;
	private Composite composite;
	private Button selectUnloadFileFromSysButton;
	private List<DbUnloadInfo> dbUnloadInfoList;
	private CheckboxTableViewer tableViewer;
	private String loadDbRusultStr = "";
	private Button useErrorControlFileButton;
	private Text errorControlFileText;
	private Button selectErrorControlFileButton;
	private String dbDir;
	private Button ignoreClassFileButton;
	private Text ignoredClassFileText;
	private Button selectIgnoreClassFileButton;

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public LoadDatabaseDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		getHelpSystem().setHelp(parent,
				CubridManagerHelpContextIDs.databaseLoad);

		Composite parentComp = (Composite) super.createDialogArea(parent);
		composite = new Composite(parentComp, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);
		GridData gridData = new GridData(GridData.FILL_BOTH);
		composite.setLayoutData(gridData);

		createDatabaseInfoGruop(composite);
		createLoadTargetInfoGroup(composite);
		createLoadOptionGruop(composite);
		setTitle(Messages.titleLoadDbDialog);
		setMessage(Messages.msgLoadDbDialog);
		initial();
		return parentComp;
	}

	/**
	 * 
	 * Create target database information group
	 * 
	 * @param parent
	 */
	private void createDatabaseInfoGruop(Composite parent) {
		Group databaseInfoGroup = new Group(parent, SWT.NONE);
		databaseInfoGroup.setText(Messages.grpDbInfo);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		databaseInfoGroup.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		layout.numColumns = 3;
		databaseInfoGroup.setLayout(layout);

		Label databaseNameLabel = new Label(databaseInfoGroup, SWT.LEFT
				| SWT.WRAP);
		databaseNameLabel.setText(Messages.lblTargetDbName);
		databaseNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		databaseNameText = new Text(databaseInfoGroup, SWT.BORDER);
		databaseNameText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		databaseNameText.setEditable(false);

		Label userNameLabel = new Label(databaseInfoGroup, SWT.LEFT | SWT.CHECK);
		userNameLabel.setText(Messages.lblUserName);
		userNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		userNameText = new Text(databaseInfoGroup, SWT.BORDER);
		userNameText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		userNameText.setEditable(false);
	}

	/**
	 * 
	 * Create unload file information group
	 * 
	 * @param parent
	 */
	private void createLoadTargetInfoGroup(Composite parent) {
		unloadFileGroup = new Group(parent, SWT.NONE);
		unloadFileGroup.setText(Messages.grpLoadFile);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		unloadFileGroup.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		layout.numColumns = 2;
		unloadFileGroup.setLayout(layout);

		selectLoadFileFromListButton = new Button(unloadFileGroup, SWT.RADIO
				| SWT.LEFT);
		selectLoadFileFromListButton.setText(Messages.btnSelectFileFromList);
		selectLoadFileFromListButton.setLayoutData(CommonTool.createGridData(1,
				1, -1, -1));
		selectLoadFileFromListButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (selectLoadFileFromListButton.getSelection()) {
					selectUnloadFileFromSysButton.setSelection(false);
					databaseCombo.setEnabled(true);
					unloadInfoTable.setEnabled(true);

					loadSchemaButton.setSelection(false);
					loadSchemaButton.setEnabled(false);
					loadSchemaText.setEnabled(false);
					loadSchemaText.setText("");
					loadSchemaFileSearchButton.setEnabled(false);

					loadObjButton.setSelection(false);
					loadObjButton.setEnabled(false);
					loadObjText.setEnabled(false);
					loadObjText.setText("");
					loadObjFileSearchButton.setEnabled(false);

					loadIndexButton.setSelection(false);
					loadIndexButton.setEnabled(false);
					loadIndexText.setEnabled(false);
					loadIndexText.setText("");
					loadIndexFileSearchButton.setEnabled(false);

					loadTriggerButton.setSelection(false);
					loadTriggerButton.setEnabled(false);
					loadTriggerText.setEnabled(false);
					loadTriggerText.setText("");
					loadTriggerFileSearchButton.setEnabled(false);
				} else {
					selectUnloadFileFromSysButton.setSelection(true);
					databaseCombo.setEnabled(false);
					unloadInfoTable.setEnabled(false);
					for (int i = 0, n = unloadInfoTable.getItemCount(); i < n; i++) {
						unloadInfoTable.getItem(i).setChecked(false);
					}

					loadSchemaButton.setEnabled(true);
					loadObjButton.setEnabled(true);
					loadIndexButton.setEnabled(true);
					loadTriggerButton.setEnabled(true);
				}
				valid();
			}
		});

		databaseCombo = new Combo(unloadFileGroup, SWT.NONE | SWT.READ_ONLY);
		databaseCombo.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		databaseCombo.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setTableModel(databaseCombo.getText());
				valid();
			}
		});
		createDbUnloadInfoTable(unloadFileGroup);
		createUnLoadInfoComp(unloadFileGroup);
	}

	/**
	 * 
	 * Create database unload information table
	 * 
	 * @param parent
	 */
	private void createDbUnloadInfoTable(Composite parent) {
		final String[] columnNameArr = new String[] {
				Messages.tblColumnLoadType, Messages.tblColumnPath,
				Messages.tblColumnDate };
		tableViewer = (CheckboxTableViewer) CommonTool.createCheckBoxTableViewer(
				parent, new TableViewerSorter(), columnNameArr,
				CommonTool.createGridData(GridData.FILL_HORIZONTAL, 3, 1, -1,
						100));
		tableViewer.addCheckStateListener(new ICheckStateListener() {
			@SuppressWarnings("unchecked")
			public void checkStateChanged(CheckStateChangedEvent event) {
				Map<String, String> map = (Map<String, String>) event.getElement();
				String checkedType = map.get("0");
				String checkedPath = map.get("1");
				String checkedDate = map.get("2");
				if (event.getChecked()) {
					for (int i = 0, n = unloadInfoTable.getItemCount(); i < n; i++) {
						if (unloadInfoTable.getItem(i).getChecked()) {
							String type = unloadInfoTable.getItem(i).getText(0);
							String path = unloadInfoTable.getItem(i).getText(1);
							String date = unloadInfoTable.getItem(i).getText(2);
							if (checkedType.equals(type)
									&& checkedPath.equals(path)
									&& checkedDate.equals(date)) {
								continue;
							}
							if (checkedType.equals(type)) {
								unloadInfoTable.getItem(i).setChecked(false);
							}
						}
					}
				}
				valid();
			}
		});
		unloadInfoTable = tableViewer.getTable();
	}

	/**
	 * Create unload information composite
	 * 
	 * @param parent
	 */
	private void createUnLoadInfoComp(Composite parent) {

		selectLoadFileFromSysComposite = new Composite(parent, SWT.NONE);
		selectLoadFileFromSysComposite.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));
		GridLayout layout = new GridLayout();
		layout.numColumns = 3;
		selectLoadFileFromSysComposite.setLayout(layout);

		selectUnloadFileFromSysButton = new Button(
				selectLoadFileFromSysComposite, SWT.RADIO | SWT.LEFT);
		selectUnloadFileFromSysButton.setText(Messages.btnSelectFileFromSys);
		selectUnloadFileFromSysButton.setLayoutData(CommonTool.createGridData(
				3, 1, -1, -1));
		selectUnloadFileFromSysButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (selectUnloadFileFromSysButton.getSelection()) {
					selectLoadFileFromListButton.setSelection(false);
					databaseCombo.setEnabled(false);
					unloadInfoTable.setEnabled(false);
					loadSchemaButton.setEnabled(true);
					loadObjButton.setEnabled(true);
					loadIndexButton.setEnabled(true);
					loadTriggerButton.setEnabled(true);
					for (int i = 0, n = unloadInfoTable.getItemCount(); i < n; i++) {
						unloadInfoTable.getItem(i).setChecked(false);
					}
				} else {
					selectLoadFileFromListButton.setSelection(true);
					databaseCombo.setEnabled(true);
					unloadInfoTable.setEnabled(true);
					loadSchemaButton.setEnabled(false);
					loadObjButton.setEnabled(false);
					loadIndexButton.setEnabled(false);
					loadTriggerButton.setEnabled(false);
				}
				valid();
			}
		});

		//add load schema comp
		loadSchemaButton = new Button(selectLoadFileFromSysComposite, SWT.CHECK
				| SWT.LEFT);
		loadSchemaButton.setText(Messages.btnLoadSchema);
		loadSchemaButton.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		loadSchemaButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (loadSchemaButton.getSelection()) {
					loadSchemaText.setEnabled(true);
					if (database.getServer().getServerInfo().isLocalServer()) {
						loadSchemaFileSearchButton.setEnabled(true);
					}
				} else {
					loadSchemaText.setText("");
					loadSchemaText.setEnabled(false);
					loadSchemaFileSearchButton.setEnabled(false);
				}
				valid();
			}
		});

		loadSchemaText = new Text(selectLoadFileFromSysComposite, SWT.BORDER);
		loadSchemaText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		loadSchemaText.setEnabled(false);
		loadSchemaText.addModifyListener(this);

		loadSchemaFileSearchButton = new Button(selectLoadFileFromSysComposite,
				SWT.NONE);
		loadSchemaFileSearchButton.setText(Messages.btnBrowse);
		loadSchemaFileSearchButton.setLayoutData(CommonTool.createGridData(1,
				1, 80, -1));
		loadSchemaFileSearchButton.setEnabled(false);
		loadSchemaFileSearchButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				FileDialog dlg = new FileDialog(getShell(), SWT.OPEN);
				dlg.setFilterPath(database.getDatabaseInfo().getDbDir());
				dlg.setText(Messages.msgSelectFile);
				String file = dlg.open();
				if (file != null) {
					loadSchemaText.setText(file);
				}
			}
		});
		//add load object comp
		loadObjButton = new Button(selectLoadFileFromSysComposite, SWT.CHECK
				| SWT.LEFT);
		loadObjButton.setText(Messages.btnLoadObj);
		loadObjButton.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		loadObjButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (loadObjButton.getSelection()) {
					loadObjText.setEnabled(true);
					if (database.getServer().getServerInfo().isLocalServer()) {
						loadObjFileSearchButton.setEnabled(true);
					}
				} else {
					loadObjText.setText("");
					loadObjText.setEnabled(false);
					loadObjFileSearchButton.setEnabled(false);
				}
				valid();
			}
		});

		loadObjText = new Text(selectLoadFileFromSysComposite, SWT.BORDER);
		loadObjText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		loadObjText.setEnabled(false);
		loadObjText.addModifyListener(this);

		loadObjFileSearchButton = new Button(selectLoadFileFromSysComposite,
				SWT.NONE);
		loadObjFileSearchButton.setText(Messages.btnBrowse);
		loadObjFileSearchButton.setLayoutData(CommonTool.createGridData(1, 1,
				80, -1));
		loadObjFileSearchButton.setEnabled(false);
		loadObjFileSearchButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				FileDialog dlg = new FileDialog(getShell(), SWT.OPEN);
				dlg.setFilterPath(database.getDatabaseInfo().getDbDir());
				dlg.setText(Messages.msgSelectFile);
				String file = dlg.open();
				if (file != null) {
					loadObjText.setText(file);
				}
			}
		});

		//add load index comp
		loadIndexButton = new Button(selectLoadFileFromSysComposite, SWT.CHECK
				| SWT.LEFT);
		loadIndexButton.setText(Messages.btnLoadIndex);
		loadIndexButton.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		loadIndexButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (loadIndexButton.getSelection()) {
					loadIndexText.setEnabled(true);
					if (database.getServer().getServerInfo().isLocalServer()) {
						loadIndexFileSearchButton.setEnabled(true);
					}
				} else {
					loadIndexText.setText("");
					loadIndexText.setEnabled(false);
					loadIndexFileSearchButton.setEnabled(false);
				}
				valid();
			}
		});

		loadIndexText = new Text(selectLoadFileFromSysComposite, SWT.BORDER);
		loadIndexText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		loadIndexText.setEnabled(false);
		loadIndexText.addModifyListener(this);

		loadIndexFileSearchButton = new Button(selectLoadFileFromSysComposite,
				SWT.NONE);
		loadIndexFileSearchButton.setText(Messages.btnBrowse);
		loadIndexFileSearchButton.setLayoutData(CommonTool.createGridData(1, 1,
				80, -1));
		loadIndexFileSearchButton.setEnabled(false);
		loadIndexFileSearchButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				FileDialog dlg = new FileDialog(getShell(), SWT.OPEN);
				dlg.setFilterPath(database.getDatabaseInfo().getDbDir());
				dlg.setText(Messages.msgSelectFile);
				String file = dlg.open();
				if (file != null) {
					loadIndexText.setText(file);
				}
			}
		});
		//add load trigger comp
		loadTriggerButton = new Button(selectLoadFileFromSysComposite,
				SWT.CHECK | SWT.LEFT);
		loadTriggerButton.setText(Messages.btnLoadTrigger);
		loadTriggerButton.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		loadTriggerButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (loadTriggerButton.getSelection()) {
					loadTriggerText.setEnabled(true);
					if (database.getServer().getServerInfo().isLocalServer()) {
						loadTriggerFileSearchButton.setEnabled(true);
					}
				} else {
					loadTriggerText.setText("");
					loadTriggerText.setEnabled(false);
					loadTriggerFileSearchButton.setEnabled(false);
				}
				valid();
			}
		});

		loadTriggerText = new Text(selectLoadFileFromSysComposite, SWT.BORDER);
		loadTriggerText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		loadTriggerText.setEnabled(false);
		loadTriggerText.addModifyListener(this);

		loadTriggerFileSearchButton = new Button(
				selectLoadFileFromSysComposite, SWT.NONE);
		loadTriggerFileSearchButton.setText(Messages.btnBrowse);
		loadTriggerFileSearchButton.setLayoutData(CommonTool.createGridData(1,
				1, 80, -1));
		loadTriggerFileSearchButton.setEnabled(false);
		loadTriggerFileSearchButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				FileDialog dlg = new FileDialog(getShell(), SWT.OPEN);
				dlg.setFilterPath(database.getDatabaseInfo().getDbDir());
				dlg.setText(Messages.msgSelectFile);
				String file = dlg.open();
				if (file != null) {
					loadTriggerText.setText(file);
				}
			}
		});

	}

	/**
	 * 
	 * Create load option information group
	 * 
	 * @param parent
	 */
	private void createLoadOptionGruop(Composite parent) {
		Group unloadOptionGroup = new Group(parent, SWT.NONE);
		unloadOptionGroup.setText(Messages.grpLoadOption);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		unloadOptionGroup.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		layout.numColumns = 3;
		unloadOptionGroup.setLayout(layout);

		checkSyntaxButton = new Button(unloadOptionGroup, SWT.LEFT | SWT.RADIO);
		checkSyntaxButton.setText(Messages.btnCheckSyntax);
		checkSyntaxButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));
		checkSyntaxButton.setSelection(true);

		noCheckSyntaxButton = new Button(unloadOptionGroup, SWT.LEFT
				| SWT.RADIO);
		noCheckSyntaxButton.setText(Messages.btnLoadDataNoCheckSyntax);
		noCheckSyntaxButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));

		estimatedSizeButton = new Button(unloadOptionGroup, SWT.LEFT
				| SWT.CHECK);
		estimatedSizeButton.setText(Messages.btnNumOfInstances);
		estimatedSizeButton.setLayoutData(CommonTool.createGridData(1, 1, -1,
				-1));
		estimatedSizeButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (estimatedSizeButton.getSelection()) {
					estimatedSizeText.setText("5000");
					estimatedSizeText.setEditable(true);
				} else {
					estimatedSizeText.setText("");
					estimatedSizeText.setEditable(false);
				}
				valid();
			}
		});
		estimatedSizeText = new Text(unloadOptionGroup, SWT.BORDER);
		estimatedSizeText.setTextLimit(8);
		estimatedSizeText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		estimatedSizeText.setEditable(false);
		estimatedSizeText.addModifyListener(this);

		commitPeriodButton = new Button(unloadOptionGroup, SWT.LEFT | SWT.CHECK);
		commitPeriodButton.setText(Messages.btnInsCount);
		commitPeriodButton.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		commitPeriodButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (commitPeriodButton.getSelection()) {
					commitPeriodText.setText("10000");
					commitPeriodText.setEditable(true);
				} else {
					commitPeriodText.setText("");
					commitPeriodText.setEditable(false);
				}
				valid();
			}
		});
		commitPeriodText = new Text(unloadOptionGroup, SWT.BORDER);
		commitPeriodText.setTextLimit(8);
		commitPeriodText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		commitPeriodText.setEditable(false);
		commitPeriodText.addModifyListener(this);

		oidButton = new Button(unloadOptionGroup, SWT.LEFT | SWT.CHECK);
		oidButton.setText(Messages.btnNoUseOid);
		oidButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));
		//error control file
		useErrorControlFileButton = new Button(unloadOptionGroup, SWT.LEFT
				| SWT.CHECK);
		useErrorControlFileButton.setText(Messages.btnUseErrorFile);
		useErrorControlFileButton.setLayoutData(CommonTool.createGridData(1, 1,
				-1, -1));
		useErrorControlFileButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (useErrorControlFileButton.getSelection()) {
					errorControlFileText.setEnabled(true);
					ServerInfo serverInfo = database.getServer().getServerInfo();
					if (serverInfo != null && !serverInfo.isLocalServer()) {
						selectErrorControlFileButton.setEnabled(false);
					} else {
						selectErrorControlFileButton.setEnabled(true);
					}

				} else {
					errorControlFileText.setText("");
					errorControlFileText.setEnabled(false);
					selectErrorControlFileButton.setEnabled(false);
				}
				valid();
			}
		});
		errorControlFileText = new Text(unloadOptionGroup, SWT.BORDER);
		errorControlFileText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		errorControlFileText.setEnabled(false);
		errorControlFileText.addModifyListener(this);

		selectErrorControlFileButton = new Button(unloadOptionGroup, SWT.NONE);
		selectErrorControlFileButton.setText(Messages.btnBrowse);
		selectErrorControlFileButton.setLayoutData(CommonTool.createGridData(1,
				1, 80, -1));
		selectErrorControlFileButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				FileDialog dlg = new FileDialog(getShell(), SWT.OPEN);
				if (dbDir != null && dbDir.trim().length() > 0)
					dlg.setFilterPath(dbDir);
				dlg.setText(Messages.msgSelectFile);
				String dir = dlg.open();
				if (dir != null) {
					errorControlFileText.setText(dir);
				}
			}
		});
		selectErrorControlFileButton.setEnabled(false);
		//ignore class file
		ignoreClassFileButton = new Button(unloadOptionGroup, SWT.LEFT
				| SWT.CHECK);
		ignoreClassFileButton.setText(Messages.btnIgnoreClassFile);
		ignoreClassFileButton.setLayoutData(CommonTool.createGridData(1, 1, -1,
				-1));
		ignoreClassFileButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (ignoreClassFileButton.getSelection()) {
					ignoredClassFileText.setEnabled(true);
					ServerInfo serverInfo = database.getServer().getServerInfo();
					if (serverInfo != null && !serverInfo.isLocalServer()) {
						selectIgnoreClassFileButton.setEnabled(false);
					} else {
						selectIgnoreClassFileButton.setEnabled(true);
					}

				} else {
					ignoredClassFileText.setText("");
					ignoredClassFileText.setEnabled(false);
					selectIgnoreClassFileButton.setEnabled(false);
				}
				valid();
			}
		});
		ignoredClassFileText = new Text(unloadOptionGroup, SWT.BORDER);
		ignoredClassFileText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		ignoredClassFileText.setEnabled(false);
		ignoredClassFileText.addModifyListener(this);

		selectIgnoreClassFileButton = new Button(unloadOptionGroup, SWT.NONE);
		selectIgnoreClassFileButton.setText(Messages.btnBrowse);
		selectIgnoreClassFileButton.setLayoutData(CommonTool.createGridData(1,
				1, 80, -1));
		selectIgnoreClassFileButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				FileDialog dlg = new FileDialog(getShell(), SWT.OPEN);
				if (dbDir != null && dbDir.trim().length() > 0)
					dlg.setFilterPath(dbDir);
				dlg.setText(Messages.msgSelectFile);
				String dir = dlg.open();
				if (dir != null) {
					ignoredClassFileText.setText(dir);
				}
			}
		});
		selectIgnoreClassFileButton.setEnabled(false);
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		this.getShell().setSize(600, 750);
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleLoadDbDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);
		getButton(IDialogConstants.OK_ID).setEnabled(false);
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			if (valid()) {
				loadDb(buttonId);
			}
		} else {
			super.buttonPressed(buttonId);
		}
	}

	/**
	 * 
	 * Execute task and load database
	 * 
	 * @param buttonId
	 */
	private void loadDb(final int buttonId) {
		TaskExecutor taskExcutor = new TaskExecutor() {
			public boolean exec(final IProgressMonitor monitor) {
				Display display = getShell().getDisplay();
				if (monitor.isCanceled()) {
					return false;
				}
				for (ITask task : taskList) {
					task.execute();
					final String msg = task.getErrorMsg();
					if (monitor.isCanceled()) {
						return false;
					}
					if (msg != null && msg.length() > 0
							&& !monitor.isCanceled()) {
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
					if (task instanceof LoadDbTask) {
						LoadDbTask loadDbTask = (LoadDbTask) task;
						String[] result = loadDbTask.getLoadResult();
						if (result != null && result.length > 0) {
							for (int i = 0; i < result.length; i++) {
								loadDbRusultStr += result[i]
										+ CommonTool.getLineSeparator();
							}
						}
					}
					if (monitor.isCanceled()) {
						return false;
					}
				}
				if (!monitor.isCanceled()) {
					display.syncExec(new Runnable() {
						public void run() {
							LoadDatabaseResultDialog dialog = new LoadDatabaseResultDialog(
									getShell());
							dialog.setResultInfoStr(loadDbRusultStr);
							dialog.open();
							setReturnCode(buttonId);
							close();
						}
					});
				}
				return true;
			}
		};

		LoadDbTask loadDbTask1 = new LoadDbTask(
				database.getServer().getServerInfo());
		LoadDbTask loadDbTask2 = new LoadDbTask(
				database.getServer().getServerInfo());
		loadDbTask1.setDbName(databaseNameText.getText());
		loadDbTask2.setDbName(databaseNameText.getText());
		loadDbTask1.setDbUser(userNameText.getText());
		loadDbTask2.setDbUser(userNameText.getText());
		if (checkSyntaxButton.getSelection()) {
			loadDbTask1.setCheckOption("both");
			loadDbTask2.setCheckOption("both");
		} else if (noCheckSyntaxButton.getSelection()) {
			loadDbTask1.setCheckOption("load");
			loadDbTask2.setCheckOption("load");
		}
		if (commitPeriodButton.getSelection()) {
			loadDbTask1.setUsedPeriod(true, commitPeriodText.getText());
			loadDbTask2.setUsedPeriod(true, commitPeriodText.getText());
		} else {
			loadDbTask1.setUsedPeriod(false, "");
			loadDbTask2.setUsedPeriod(false, "");
		}
		if (estimatedSizeButton.getSelection()) {
			loadDbTask1.setUsedEstimatedSize(true, estimatedSizeText.getText());
			loadDbTask2.setUsedEstimatedSize(true, estimatedSizeText.getText());
		} else {
			loadDbTask1.setUsedEstimatedSize(false, "");
			loadDbTask2.setUsedEstimatedSize(false, "");
		}
		if (useErrorControlFileButton.getSelection()) {
			loadDbTask1.setUsedErrorContorlFile(true,
					errorControlFileText.getText());
			loadDbTask2.setUsedErrorContorlFile(true,
					errorControlFileText.getText());
		} else {
			loadDbTask1.setUsedErrorContorlFile(false, "");
			loadDbTask2.setUsedErrorContorlFile(false, "");
		}
		if (ignoreClassFileButton.getSelection()) {
			loadDbTask1.setUsedIgnoredClassFile(true,
					ignoredClassFileText.getText());
			loadDbTask2.setUsedIgnoredClassFile(true,
					ignoredClassFileText.getText());
		} else {
			loadDbTask1.setUsedIgnoredClassFile(false, "");
			loadDbTask2.setUsedIgnoredClassFile(false, "");
		}
		loadDbTask1.setNoUsedOid(oidButton.getSelection());
		loadDbTask2.setNoUsedOid(oidButton.getSelection());
		loadDbTask1.setNoUsedLog(false);
		loadDbTask2.setNoUsedLog(false);
		if (selectLoadFileFromListButton.getSelection()) {
			String schemaPath = "";
			String objectPath = "";
			String indexPath = "";
			String triggerPath = "";
			for (int i = 0, n = unloadInfoTable.getItemCount(); i < n; i++) {
				if (unloadInfoTable.getItem(i).getChecked()) {
					String type = unloadInfoTable.getItem(i).getText(0);
					String path = unloadInfoTable.getItem(i).getText(1);
					if (type != null && type.trim().equals("schema")) {
						schemaPath = path;
					}
					if (type != null && type.trim().equals("object")) {
						objectPath = path;
					}
					if (type != null && type.trim().equals("index")) {
						indexPath = path;
					}
					if (type != null && type.trim().equals("trigger")) {
						triggerPath = path;
					}
				}
			}
			boolean isAddTask1 = false;
			if (schemaPath != null && schemaPath.trim().length() > 0) {
				loadDbTask1.setSchemaPath(schemaPath);
				isAddTask1 = true;
			} else {
				loadDbTask1.setSchemaPath("none");
			}
			if (objectPath != null && objectPath.trim().length() > 0) {
				loadDbTask1.setObjectPath(objectPath);
				isAddTask1 = true;
			} else {
				loadDbTask1.setObjectPath("none");
			}
			if (indexPath != null && indexPath.trim().length() > 0) {
				loadDbTask1.setIndexPath(indexPath);
				isAddTask1 = true;
			} else {
				loadDbTask1.setIndexPath("none");
			}
			boolean isAddTask2 = false;
			if (triggerPath != null && triggerPath.trim().length() > 0) {
				loadDbTask2.setSchemaPath(triggerPath);
				isAddTask2 = true;
			}
			if (isAddTask1) {
				taskExcutor.addTask(loadDbTask1);
			}
			if (isAddTask2) {
				taskExcutor.addTask(loadDbTask2);
			}

		} else if (selectUnloadFileFromSysButton.getSelection()) {
			boolean isAddTask1 = false;
			if (loadSchemaButton.getSelection()) {
				String schemaPath = loadSchemaText.getText();
				loadDbTask1.setSchemaPath(schemaPath);
				isAddTask1 = true;
			} else {
				loadDbTask1.setSchemaPath("none");
			}
			if (loadObjButton.getSelection()) {
				String objPath = loadObjText.getText();
				loadDbTask1.setObjectPath(objPath);
				isAddTask1 = true;
			} else {
				loadDbTask1.setObjectPath("none");
			}
			if (loadIndexButton.getSelection()) {
				String indexPath = loadIndexText.getText();
				loadDbTask1.setIndexPath(indexPath);
				isAddTask1 = true;
			} else {
				loadDbTask1.setIndexPath("none");
			}
			boolean isAddTask2 = false;
			if (loadTriggerButton.getSelection()) {
				String triggerPath = loadTriggerText.getText();
				loadDbTask2.setSchemaPath(triggerPath);
				isAddTask2 = true;
			}
			if (isAddTask1) {
				taskExcutor.addTask(loadDbTask1);
			}
			if (isAddTask2) {
				taskExcutor.addTask(loadDbTask2);
			}
		}
		new ExecTaskWithProgress(taskExcutor).exec(true, true);
	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {
		databaseNameText.setText(database.getLabel());
		userNameText.setText(database.getUserName());
		dbDir = database.getDatabaseInfo().getDbDir();
		if (dbUnloadInfoList != null && dbUnloadInfoList.size() > 0) {
			int index = 0;
			for (int i = 0; i < dbUnloadInfoList.size(); i++) {
				DbUnloadInfo dbUnloadInfo = dbUnloadInfoList.get(i);
				databaseCombo.add(dbUnloadInfo.getDbName());
				if (dbUnloadInfo.getDbName().equals(database.getLabel())) {
					index = i;
				}
			}
			databaseCombo.select(index);
			setTableModel(databaseCombo.getText());
			selectUnloadFileFromSysButton.setSelection(false);
			selectLoadFileFromListButton.setSelection(true);
			databaseCombo.setEnabled(true);
		} else {
			selectUnloadFileFromSysButton.setSelection(true);
			selectLoadFileFromListButton.setSelection(false);
			databaseCombo.setEnabled(false);
		}
	}

	/**
	 * 
	 * Set tableViewer input model of some database
	 * 
	 * @param dbName
	 */
	private void setTableModel(String dbName) {
		List<Map<String, String>> dataList = new ArrayList<Map<String, String>>();
		if (dbUnloadInfoList != null && dbUnloadInfoList.size() > 0) {
			DbUnloadInfo dbUnloadInfo = null;
			for (int i = 0; i < dbUnloadInfoList.size(); i++) {
				dbUnloadInfo = dbUnloadInfoList.get(i);
				if (dbUnloadInfo.getDbName().equals(dbName)) {
					break;
				}
			}
			if (dbUnloadInfo != null) {
				List<String> pathList = dbUnloadInfo.getSchemaPathList();
				List<String> dateList = dbUnloadInfo.getSchemaDateList();
				for (int i = 0; i < pathList.size() && i < dateList.size(); i++) {
					String path = pathList.get(i);
					String date = dateList.get(i);
					if (path != null && path.trim().length() > 0) {
						Map<String, String> map = new HashMap<String, String>();
						if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
							path = FileNameUtils.separatorsToWindows(path);
						}
						map.put("0", "schema");
						map.put("1", path);
						map.put("2", date);
						dataList.add(map);
					}
				}
				pathList = dbUnloadInfo.getObjectPathList();
				dateList = dbUnloadInfo.getObjectDateList();
				for (int i = 0; i < pathList.size() && i < dateList.size(); i++) {
					String path = pathList.get(i);
					String date = dateList.get(i);
					if (path != null && path.trim().length() > 0) {
						Map<String, String> map = new HashMap<String, String>();
						if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
							path = FileNameUtils.separatorsToWindows(path);
						}
						map.put("0", "object");
						map.put("1", path);
						map.put("2", date);
						dataList.add(map);
					}
				}
				pathList = dbUnloadInfo.getIndexPathList();
				dateList = dbUnloadInfo.getIndexDateList();
				for (int i = 0; i < pathList.size() && i < dateList.size(); i++) {
					String path = pathList.get(i);
					String date = dateList.get(i);
					if (path != null && path.trim().length() > 0) {
						Map<String, String> map = new HashMap<String, String>();
						if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
							path = FileNameUtils.separatorsToWindows(path);
						}
						map.put("0", "index");
						map.put("1", path);
						map.put("2", date);
						dataList.add(map);
					}
				}
				pathList = dbUnloadInfo.getTriggerPathList();
				dateList = dbUnloadInfo.getTriggerDateList();
				for (int i = 0; i < pathList.size() && i < dateList.size(); i++) {
					String path = pathList.get(i);
					String date = dateList.get(i);
					if (path != null && path.trim().length() > 0) {
						Map<String, String> map = new HashMap<String, String>();
						if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
							path = FileNameUtils.separatorsToWindows(path);
						}
						map.put("0", "trigger");
						map.put("1", path);
						map.put("2", date);
						dataList.add(map);
					}
				}
			}
		}
		if (dataList.size() > 0) {
			tableViewer.setInput(dataList);
			tableViewer.refresh();
			for (int i = 0; i < unloadInfoTable.getColumnCount(); i++) {
				unloadInfoTable.getColumn(i).pack();
			}
		}
	}

	/**
	 * 
	 * Check the validation
	 * 
	 * @return
	 */
	private boolean valid() {
		boolean isValidSchemaPath = true;
		boolean isValidObjPath = true;
		boolean isValidIndexPath = true;
		boolean isValidTriggerPath = true;
		boolean isValidFileSystem = true;
		if (selectUnloadFileFromSysButton.getSelection()) {
			isValidFileSystem = false;
			if (loadSchemaButton.getSelection()) {
				String schemaPath = loadSchemaText.getText();
				isValidSchemaPath = ValidateUtil.isValidPathName(schemaPath);
				if (isValidSchemaPath
						&& database.getServer().getServerInfo().isLocalServer()) {
					File file = new File(schemaPath);
					if (!file.exists()) {
						isValidSchemaPath = false;
					}
				}
				isValidFileSystem = true;
			}
			if (loadObjButton.getSelection()) {
				String objPath = loadObjText.getText();
				isValidObjPath = ValidateUtil.isValidPathName(objPath);
				if (isValidObjPath
						&& database.getServer().getServerInfo().isLocalServer()) {
					File file = new File(objPath);
					if (!file.exists()) {
						isValidObjPath = false;
					}
				}
				isValidFileSystem = true;
			}
			if (loadIndexButton.getSelection()) {
				String indexPath = loadIndexText.getText();
				isValidIndexPath = ValidateUtil.isValidPathName(indexPath);
				if (isValidIndexPath
						&& database.getServer().getServerInfo().isLocalServer()) {
					File file = new File(indexPath);
					if (!file.exists()) {
						isValidObjPath = false;
					}
				}
				isValidFileSystem = true;
			}
			if (loadTriggerButton.getSelection()) {
				String triggerPath = loadTriggerText.getText();
				isValidTriggerPath = ValidateUtil.isValidPathName(triggerPath);
				if (isValidTriggerPath
						&& database.getServer().getServerInfo().isLocalServer()) {
					File file = new File(triggerPath);
					if (!file.exists()) {
						isValidTriggerPath = false;
					}
				}
				isValidFileSystem = true;
			}
		}
		boolean isValidUnLoadDb = true;
		boolean isSelectedDbPath = true;
		if (selectLoadFileFromListButton.getSelection()) {
			isSelectedDbPath = false;
			String dbName = databaseCombo.getText();
			if (dbName == null || dbName.trim().length() <= 0) {
				isValidUnLoadDb = false;
			}
			for (int i = 0, n = unloadInfoTable.getItemCount(); i < n
					&& isValidUnLoadDb; i++) {
				if (unloadInfoTable.getItem(i).getChecked()) {
					isSelectedDbPath = true;
					break;
				}
			}
		}
		boolean isValidCommitPeriod = true;
		if (commitPeriodButton.getSelection()) {
			String period = commitPeriodText.getText();
			isValidCommitPeriod = ValidateUtil.isNumber(period)
					&& Integer.parseInt(period) > 0;
		}
		boolean isValidEstimatedSize = true;
		if (estimatedSizeButton.getSelection()) {
			String size = estimatedSizeText.getText();
			isValidEstimatedSize = ValidateUtil.isNumber(size)
					&& Integer.parseInt(size) > 0;
		}
		boolean isValidErrorControlFile = true;
		if (useErrorControlFileButton.getSelection()) {
			String filePath = errorControlFileText.getText();
			isValidErrorControlFile = ValidateUtil.isValidPathName(filePath);
			if (isValidErrorControlFile
					&& database.getServer().getServerInfo().isLocalServer()) {
				File file = new File(filePath);
				if (!file.exists()) {
					isValidErrorControlFile = false;
				}
			}
		}
		boolean isValidIgnoreClassFile = true;
		if (ignoreClassFileButton.getSelection()) {
			String filePath = ignoredClassFileText.getText();
			isValidIgnoreClassFile = ValidateUtil.isValidPathName(filePath);
			if (isValidIgnoreClassFile
					&& database.getServer().getServerInfo().isLocalServer()) {
				File file = new File(filePath);
				if (!file.exists()) {
					isValidIgnoreClassFile = false;
				}
			}
		}
		if (!isValidUnLoadDb) {
			setErrorMessage(Messages.errLoadFileFromList);
		} else if (!isSelectedDbPath) {
			setErrorMessage(Messages.errNoSelectedPath);
		} else if (!isValidFileSystem) {
			setErrorMessage(Messages.errLoadFileFromSys);
		} else if (!isValidSchemaPath) {
			setErrorMessage(Messages.errLoadSchema);
		} else if (!isValidObjPath) {
			setErrorMessage(Messages.errLoadOjbects);
		} else if (!isValidIndexPath) {
			setErrorMessage(Messages.errLoadIndex);
		} else if (!isValidTriggerPath) {
			setErrorMessage(Messages.errLoadTrigger);
		} else if (!isValidEstimatedSize) {
			setErrorMessage(Messages.errNumOfInstances);
		} else if (!isValidCommitPeriod) {
			setErrorMessage(Messages.errInsertCount);
		} else if (!isValidErrorControlFile) {
			setErrorMessage(Messages.errControlFile);
		} else if (!isValidIgnoreClassFile) {
			setErrorMessage(Messages.errClassFile);
		} else {
			setErrorMessage(null);
		}
		boolean isEnabled = isValidSchemaPath && isValidObjPath
				&& isValidIndexPath && isValidTriggerPath && isValidFileSystem
				&& isValidUnLoadDb && isSelectedDbPath && isValidCommitPeriod
				&& isValidEstimatedSize && isValidErrorControlFile
				&& isValidIgnoreClassFile;
		if (getButton(IDialogConstants.OK_ID) != null)
			getButton(IDialogConstants.OK_ID).setEnabled(isEnabled);
		return isEnabled;
	}

	public void modifyText(ModifyEvent e) {
		valid();
	}

	/**
	 * 
	 * Get added CubridDatabase
	 * 
	 * @return
	 */
	public CubridDatabase getDatabase() {
		return database;
	}

	/**
	 * 
	 * Set edited CubridDatabase
	 * 
	 * @param database
	 */
	public void setDatabase(CubridDatabase database) {
		this.database = database;
	}

	public void setDbUnloadInfoList(List<DbUnloadInfo> dbUnloadInfoList) {
		this.dbUnloadInfoList = dbUnloadInfoList;
	}
}
