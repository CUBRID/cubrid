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

import java.text.NumberFormat;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.eclipse.core.runtime.IProgressMonitor;
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
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.DirectoryDialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Item;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.EnvInfo;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.database.task.CheckDirTask;
import com.cubrid.cubridmanager.core.cubrid.database.task.RenameDbTask;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfoList;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.VolumeType;
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
 * Rename database will use this dialog to fill in the information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class RenameDatabaseDialog extends
		CMTitleAreaDialog implements
		ModifyListener {
	private Text databaseNameText = null;
	private CubridDatabase database = null;
	private Button exVolumePathButton;
	private Text exVolumePathText;
	private Button forceDelBackupVolumeButton;
	private Button renameVolumeButton;
	private Table volumeTable;
	private DbSpaceInfoList dbSpaceInfoList = null;
	private List<Map<String, String>> spaceInfoList = null;
	private TableViewer volumeTableViewer;
	private String databasesDir = "";
	private boolean isCanFinished = true;
	private Button selectVolumeDirectoryButton;

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public RenameDatabaseDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		getHelpSystem().setHelp(parent,
				CubridManagerHelpContextIDs.databaseRename);

		Composite parentComp = (Composite) super.createDialogArea(parent);
		Composite composite = new Composite(parentComp, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);
		GridData gridData = new GridData(GridData.FILL_BOTH);
		composite.setLayoutData(gridData);

		createNewDatabaseInfoComp(composite);
		setTitle(Messages.titleRenameDbDialog);
		setMessage(Messages.msgRenameDbDialog);
		initial();
		return parentComp;
	}

	/**
	 * 
	 * Create database name group
	 * 
	 * @param parent
	 */
	private void createNewDatabaseInfoComp(Composite parent) {
		Composite comp = new Composite(parent, SWT.NONE);
		GridData gridData = new GridData(GridData.FILL_BOTH);
		comp.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		layout.numColumns = 3;
		comp.setLayout(layout);

		Label databaseNameLabel = new Label(comp, SWT.LEFT | SWT.WRAP);
		databaseNameLabel.setText(Messages.lblNewDbName);
		gridData = new GridData();
		gridData.widthHint = 150;
		databaseNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		databaseNameText = new Text(comp, SWT.BORDER);
		databaseNameText.setTextLimit(ValidateUtil.MAX_DB_NAME_LENGTH);
		databaseNameText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));

		forceDelBackupVolumeButton = new Button(comp, SWT.LEFT | SWT.CHECK);
		forceDelBackupVolumeButton.setText(Messages.btnForceDelBackupVolume);
		forceDelBackupVolumeButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));

		exVolumePathButton = new Button(comp, SWT.LEFT | SWT.CHECK);
		exVolumePathButton.setText(Messages.btnExtendedVolumePath);
		exVolumePathButton.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		exVolumePathButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (exVolumePathButton.getSelection()) {
					exVolumePathText.setEditable(true);
					renameVolumeButton.setSelection(false);
					renameVolumeButton.setEnabled(false);
					volumeTable.setEnabled(false);
					ServerInfo serverInfo = database.getServer().getServerInfo();
					if (serverInfo != null && serverInfo.isLocalServer()) {
						selectVolumeDirectoryButton.setEnabled(true);
					}
				} else {
					exVolumePathText.setEditable(false);
					renameVolumeButton.setEnabled(true);
					volumeTable.setEnabled(true);
					selectVolumeDirectoryButton.setEnabled(false);
				}
			}
		});

		exVolumePathText = new Text(comp, SWT.BORDER);
		exVolumePathText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		exVolumePathText.setEditable(false);

		selectVolumeDirectoryButton = new Button(comp, SWT.NONE);
		selectVolumeDirectoryButton.setText(Messages.btnBrowse);
		selectVolumeDirectoryButton.setLayoutData(CommonTool.createGridData(1,
				1, 80, -1));
		selectVolumeDirectoryButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				DirectoryDialog dlg = new DirectoryDialog(getShell());
				if (databasesDir != null && databasesDir.trim().length() > 0)
					dlg.setFilterPath(databasesDir);
				dlg.setText(Messages.msgSelectDir);
				dlg.setMessage(Messages.msgSelectDir);
				String dir = dlg.open();
				if (dir != null) {
					exVolumePathText.setText(dir);
				}
			}
		});
		selectVolumeDirectoryButton.setEnabled(false);

		renameVolumeButton = new Button(comp, SWT.LEFT | SWT.CHECK);
		renameVolumeButton.setText(Messages.btnRenameIndiVolume);
		renameVolumeButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));
		renameVolumeButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (renameVolumeButton.getSelection()) {
					volumeTable.setEnabled(true);
					exVolumePathButton.setSelection(false);
				} else {
					volumeTable.setEnabled(false);
				}
			}
		});

		final String[] columnNameArr = new String[] {
				Messages.tblColumnCurrVolName, Messages.tblColumnNewVolName,
				Messages.tblColumnCurrDirPath, Messages.tblColumnNewDirPath };
		volumeTableViewer = CommonTool.createCommonTableViewer(comp,
				new TableViewerSorter(), columnNameArr,
				CommonTool.createGridData(GridData.FILL_BOTH, 3, 1, -1, 200));
		volumeTable = volumeTableViewer.getTable();
		volumeTable.setEnabled(false);
		volumeTableViewer.setColumnProperties(columnNameArr);
		CellEditor[] editors = new CellEditor[4];
		editors[0] = null;
		editors[1] = new TextCellEditor(volumeTable);
		editors[2] = null;
		editors[3] = new TextCellEditor(volumeTable);
		volumeTableViewer.setCellEditors(editors);
		volumeTableViewer.setCellModifier(new ICellModifier() {
			@SuppressWarnings("unchecked")
			public boolean canModify(Object element, String property) {
				Map<String, String> map = (Map<String, String>) element;
				String name = map.get("0");
				if (property.equals(columnNameArr[0])
						|| property.equals(columnNameArr[2])) {
					return false;
				} else if (property.equals(columnNameArr[1])
						&& name.equals(database.getName())) {
					return false;
				}
				return true;
			}

			@SuppressWarnings("unchecked")
			public Object getValue(Object element, String property) {
				Map<String, String> map = (Map<String, String>) element;
				if (property.equals(columnNameArr[1])) {
					return map.get("1");
				} else if (property.equals(columnNameArr[3])) {
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
				if (property.equals(columnNameArr[1])) {
					map.put("1", value.toString());
				} else if (property.equals(columnNameArr[3])) {
					map.put("3", value.toString());
				}
				volumeTableViewer.refresh();
			}
		});
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleRenameDbDialog);
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
			renameDb(buttonId);
		} else {
			super.buttonPressed(buttonId);
		}
	}

	/**
	 * 
	 * Execute task and rename database
	 * 
	 * @param buttonId
	 */
	private void renameDb(final int buttonId) {
		isCanFinished = true;
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
					if (task instanceof CheckDirTask) {
						CheckDirTask checkDirTask = (CheckDirTask) task;
						final String[] dirs = checkDirTask.getNoExistDirectory();
						if (dirs != null && dirs.length > 0) {
							display.syncExec(new Runnable() {
								public void run() {
									CreateDirDialog dialog = new CreateDirDialog(
											getShell());
									dialog.setDirs(dirs);
									if (dialog.open() != IDialogConstants.OK_ID) {
										isCanFinished = false;
									}
								}
							});
						}
					}
					if (!isCanFinished || monitor.isCanceled()) {
						return false;
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

		CheckDirTask checkDirTask = new CheckDirTask(
				database.getServer().getServerInfo());
		RenameDbTask renameDbTask = new RenameDbTask(
				database.getServer().getServerInfo());
		renameDbTask.setDbName(database.getLabel());
		renameDbTask.setNewDbName(databaseNameText.getText());
		if (exVolumePathButton.getSelection()) {
			checkDirTask.setDirectory(new String[] { exVolumePathText.getText() });
			renameDbTask.setExVolumePath(exVolumePathText.getText());
			renameDbTask.setAdvanced(false);
		} else if (renameVolumeButton.getSelection()) {
			List<String> pathList = new ArrayList<String>();
			List<String> volumeChangedList = new ArrayList<String>();
			for (int i = 0; spaceInfoList != null && i < spaceInfoList.size(); i++) {
				Map<String, String> map = spaceInfoList.get(i);
				String oldName = map.get("0");
				String newName = map.get("1");
				String oldPath = map.get("2");
				String newPath = map.get("3");
				addVolumePath(pathList, newPath);
				oldPath = oldPath.replaceAll(":", "|");
				newPath = newPath.replaceAll(":", "|");
				volumeChangedList.add(oldPath + "/" + oldName + ":" + newPath
						+ "/" + newName);
			}
			String[] checkedDirs = new String[pathList.size()];
			pathList.toArray(checkedDirs);
			checkDirTask.setDirectory(checkedDirs);
			renameDbTask.setAdvanced(true);
			renameDbTask.setIndividualVolume(volumeChangedList);
		}
		if (forceDelBackupVolumeButton.getSelection()) {
			renameDbTask.setForceDel(true);
		} else {
			renameDbTask.setForceDel(false);
		}
		taskExcutor.addTask(checkDirTask);
		taskExcutor.addTask(renameDbTask);
		new ExecTaskWithProgress(taskExcutor).exec(true, true);
	}

	/**
	 * 
	 * Add volume path into list
	 * 
	 * @param checkedList
	 * @param volumePath
	 */
	private void addVolumePath(List<String> checkedList, String volumePath) {
		boolean isExist = false;
		for (int i = 0; i < checkedList.size(); i++) {
			String volPath = checkedList.get(i);
			if (volumePath.equals(volPath)) {
				isExist = true;
				break;
			}
		}
		if (!isExist) {
			checkedList.add(volumePath);
		}
	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {
		EnvInfo envInfo = database.getServer().getServerInfo().getEnvInfo();
		if (envInfo != null) {
			databasesDir = envInfo.getDatabaseDir();
			if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
				databasesDir = FileNameUtils.separatorsToWindows(databasesDir);
			}
			exVolumePathText.setText(databasesDir);
		}
		if (spaceInfoList == null) {
			spaceInfoList = new ArrayList<Map<String, String>>();
			if (this.dbSpaceInfoList != null) {
				List<DbSpaceInfo> list = this.dbSpaceInfoList.getSpaceinfo();
				for (int i = 0; list != null && i < list.size(); i++) {
					Map<String, String> map = new HashMap<String, String>();
					DbSpaceInfo spaceInfo = list.get(i);
					String type = spaceInfo.getType();
					if (!VolumeType.GENERIC.getText().equals(type)
							&& !VolumeType.DATA.getText().equals(type)
							&& !VolumeType.INDEX.getText().equals(type)
							&& !VolumeType.TEMP.getText().equals(type))
						continue;
					map.put("0", spaceInfo.getSpacename());
					map.put("1", spaceInfo.getSpacename());
					String location = spaceInfo.getLocation();
					if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
						location = FileNameUtils.separatorsToWindows(location);
					}
					map.put("2", location);
					map.put("3", location);
					spaceInfoList.add(map);
				}
			}
		}
		volumeTableViewer.setInput(spaceInfoList);
		for (int i = 0; i < volumeTable.getColumnCount(); i++) {
			volumeTable.getColumn(i).pack();
		}
		databaseNameText.addModifyListener(this);
		exVolumePathText.addModifyListener(this);
	}

	public void modifyText(ModifyEvent e) {

		String databaseName = databaseNameText.getText();
		String volumePath = exVolumePathText.getText();
		boolean isValidDatabaseName = true;
		boolean isValidDatabaseNameLength = true;
		boolean isValidVolumePath = true;

		isValidDatabaseName = ValidateUtil.isValidDBName(databaseName);
		DatabaseInfo databaseInfo = database.getServer().getServerInfo().getLoginedUserInfo().getDatabaseInfo(
				databaseName);
		boolean isDatabaseNameAlrExist = databaseInfo != null;
		isValidVolumePath = ValidateUtil.isValidPathName(volumePath);
		isValidDatabaseNameLength = ValidateUtil.isValidDbNameLength(databaseName);

		if (!isValidDatabaseName) {
			setErrorMessage(Messages.errDbName);
		} else if (!isValidDatabaseNameLength) {
			setErrorMessage(Messages.bind(
					Messages.errDbNameLength,
					new String[] { String.valueOf(ValidateUtil.MAX_DB_NAME_LENGTH - 1) }));
		} else if (isDatabaseNameAlrExist) {
			setErrorMessage(Messages.errDbExist);
		} else if (!isValidVolumePath) {
			setErrorMessage(Messages.errExtendedVolPath);
		} else {
			setErrorMessage(null);
		}
		if (e.widget == databaseNameText && isValidDatabaseName) {
			String newPath = databasesDir
					+ database.getServer().getServerInfo().getPathSeparator()
					+ databaseName;
			exVolumePathText.setText(newPath);
			int count = 1;
			for (int i = 0; spaceInfoList != null && i < spaceInfoList.size(); i++) {
				Map<String, String> map = spaceInfoList.get(i);
				String name = database.getLabel();
				if (name.equals(map.get("0"))) {
					map.put("1", databaseName);
					map.put("3", newPath);
				} else {
					NumberFormat nf = NumberFormat.getInstance();
					nf.setMinimumIntegerDigits(3);
					map.put("1", databaseName + "_x" + nf.format(count));
					map.put("3", newPath);
					count++;
				}
			}
			if (volumeTableViewer != null) {
				volumeTableViewer.refresh();
			}
		}
		boolean isEnabled = isValidVolumePath && isValidDatabaseName
				&& isValidDatabaseNameLength && !isDatabaseNameAlrExist;
		if (getButton(IDialogConstants.OK_ID) != null)
			getButton(IDialogConstants.OK_ID).setEnabled(isEnabled);
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

	public void setDbSpaceInfoList(DbSpaceInfoList dbSpaceInfoList) {
		this.dbSpaceInfoList = dbSpaceInfoList;
	}
}
