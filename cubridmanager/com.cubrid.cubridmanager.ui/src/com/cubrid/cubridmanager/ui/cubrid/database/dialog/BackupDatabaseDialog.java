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
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.DirectoryDialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Spinner;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DbBackupHistoryInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DbBackupInfo;
import com.cubrid.cubridmanager.core.cubrid.database.task.BackupDbTask;
import com.cubrid.cubridmanager.core.cubrid.database.task.CheckDirTask;
import com.cubrid.cubridmanager.core.cubrid.database.task.CheckFileTask;
import com.cubrid.cubridmanager.core.cubrid.database.task.GetBackupVolInfoTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.OsInfoType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.FileNameUtils;
import com.cubrid.cubridmanager.ui.spi.TableViewerSorter;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTrayDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * Backup database will use this dialog to fill in the information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class BackupDatabaseDialog extends
		CMTrayDialog {

	private Text databaseNameText = null;
	private Text volumeNameText = null;
	private Text backupDirText = null;
	private CubridDatabase database = null;
	private TabFolder tabFolder;
	private Combo backupLevelCombo;
	private Spinner spnThreadNum;
	private Button consistentButton;
	private Button archiveLogButton;
	private Button useCompressButton;
	private DbBackupInfo dbBackupInfo = null;
	private boolean isCanFinished = true;
	private String backupDir;
	private Button safeBackupButton;
	private boolean isReplication = false;

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public BackupDatabaseDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		getHelpSystem().setHelp(parent,
				CubridManagerHelpContextIDs.databaseBackup);

		Composite parentComp = (Composite) super.createDialogArea(parent);
		tabFolder = new TabFolder(parentComp, SWT.NONE);
		tabFolder.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		tabFolder.setLayout(layout);

		TabItem item = new TabItem(tabFolder, SWT.NONE);
		item.setText(Messages.grpBackuInfo);
		Composite composite = createBackupInfoComp();
		item.setControl(composite);

		item = new TabItem(tabFolder, SWT.NONE);
		item.setText(Messages.grpBackupHistoryInfo);
		composite = createBackupHistoryComp();
		item.setControl(composite);
		initial();
		return parentComp;
	}

	/**
	 * 
	 * Create backup information tab composite
	 * 
	 * @return
	 */
	private Composite createBackupInfoComp() {
		Composite composite = new Composite(tabFolder, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.numColumns = 4;
		composite.setLayout(layout);

		Label databaseNameLabel = new Label(composite, SWT.LEFT);
		databaseNameLabel.setText(Messages.lblDbName);
		databaseNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		databaseNameText = new Text(composite, SWT.LEFT | SWT.BORDER);
		databaseNameText.setEditable(false);
		if (database == null)
			return composite;
		databaseNameText.setText(database.getLabel());
		databaseNameText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));

		Label volumeNameLabel = new Label(composite, SWT.LEFT);
		volumeNameLabel.setText(Messages.lblVolName);
		volumeNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		volumeNameText = new Text(composite, SWT.LEFT | SWT.BORDER);
		volumeNameText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));

		Label backupLevelLabel = new Label(composite, SWT.LEFT | SWT.WRAP);
		backupLevelLabel.setText(Messages.lblBackupLevel);
		backupLevelLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		backupLevelCombo = new Combo(composite, SWT.DROP_DOWN | SWT.READ_ONLY);
		backupLevelCombo.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));
		backupLevelCombo.addSelectionListener(new SelectionListener() {
			public void widgetSelected(SelectionEvent e) {
				changeVolumeName();
			}

			public void widgetDefaultSelected(SelectionEvent e) {
				changeVolumeName();
			}
		});

		Label backupDirLabel = new Label(composite, SWT.LEFT);
		backupDirLabel.setText(Messages.lblBackupDir);
		backupDirLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		backupDirText = new Text(composite, SWT.LEFT | SWT.BORDER);
		backupDirText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));

		Button selectTargetDirectoryButton = new Button(composite, SWT.NONE);
		selectTargetDirectoryButton.setText(Messages.btnBrowse);
		selectTargetDirectoryButton.setLayoutData(CommonTool.createGridData(1,
				1, 80, -1));
		selectTargetDirectoryButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				DirectoryDialog dlg = new DirectoryDialog(getShell());
				if (backupDir != null && backupDir.trim().length() > 0)
					dlg.setFilterPath(backupDir);
				dlg.setText(Messages.msgSelectDir);
				dlg.setMessage(Messages.msgSelectDir);
				String dir = dlg.open();
				if (dir != null) {
					backupDirText.setText(dir);
				}
			}
		});

		ServerInfo serverInfo = database.getServer().getServerInfo();
		if (serverInfo != null && !serverInfo.isLocalServer()) {
			selectTargetDirectoryButton.setEnabled(false);
		}

		Label threadNumLabel = new Label(composite, SWT.LEFT);
		threadNumLabel.setText(Messages.lblParallelBackup);
		threadNumLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		spnThreadNum = new Spinner(composite, SWT.BORDER);
		spnThreadNum.setMaximum(Integer.MAX_VALUE);
		spnThreadNum.setLayoutData(CommonTool.createGridData(2, 1, -1, -1));
		new Label(composite, SWT.NONE);

		consistentButton = new Button(composite, SWT.NONE | SWT.CHECK);
		consistentButton.setText(Messages.btnCheckConsistency);
		consistentButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 4, 1, -1, -1));

		archiveLogButton = new Button(composite, SWT.NONE | SWT.CHECK);
		archiveLogButton.setText(Messages.btnDeleteLog);
		archiveLogButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 4, 1, -1, -1));
		archiveLogButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (database.getServer().getServerInfo().getServerOsInfo() != OsInfoType.NT
						&& isReplication() && archiveLogButton.getSelection()) {
					safeBackupButton.setSelection(true);
				} else {
					safeBackupButton.setSelection(false);
				}
			}
		});

		useCompressButton = new Button(composite, SWT.NONE | SWT.CHECK);
		useCompressButton.setText(Messages.btnCompressVol);
		useCompressButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 4, 1, -1, -1));

		safeBackupButton = new Button(composite, SWT.NONE | SWT.CHECK);
		safeBackupButton.setText(Messages.btnSafeBackup);
		safeBackupButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 4, 1, -1, -1));
		safeBackupButton.setEnabled(false);
		if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
			safeBackupButton.setVisible(false);
		}
		return composite;
	}

	/**
	 * 
	 * Create backup history information tab composite
	 * 
	 * @return
	 */
	private Composite createBackupHistoryComp() {
		Composite composite = new Composite(tabFolder, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		composite.setLayout(layout);

		Label tipLabel = new Label(composite, SWT.LEFT | SWT.WRAP);
		tipLabel.setText(Messages.msgBackupHistoryList);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		tipLabel.setLayoutData(gridData);

		final String[] columnNameArr = new String[] {
				Messages.tblColumnBackupLevel, Messages.tblColumnBackupDate,
				Messages.tblColumnSize, Messages.tblColumnBackupPath };
		TableViewer historyTableViewer = CommonTool.createCommonTableViewer(
				composite, new TableViewerSorter(), columnNameArr,
				CommonTool.createGridData(GridData.FILL_BOTH, 1, 1, 400, 200));
		historyTableViewer.setInput(getBackupHistoryInfoList());
		Table historyTable = historyTableViewer.getTable();
		for (int i = 0; i < historyTable.getColumnCount(); i++) {
			historyTable.getColumn(i).pack();
		}
		return composite;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleBackupDbDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			if (!CommonTool.openConfirmBox(null, Messages.msgConfirmBackupDb))
				return;
			if (valid(true)) {
				backupDb(buttonId);
			}
		} else {
			super.buttonPressed(buttonId);
		}
	}

	/**
	 * 
	 * Execute task and backup database
	 * 
	 * @param buttonId
	 */
	private void backupDb(final int buttonId) {
		isCanFinished = true;
		TaskExecutor taskExcutor = new TaskExecutor() {
			public boolean exec(final IProgressMonitor monitor) {
				Display display = getShell().getDisplay();
				if (monitor.isCanceled()) {
					return false;
				}
				for (ITask task : taskList) {
					if (!(task instanceof GetBackupVolInfoTask)
							|| database.getRunningType() != DbRunningType.CS) {
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
					} else if (task instanceof CheckFileTask) {
						CheckFileTask checkFileTask = (CheckFileTask) task;
						final String[] files = checkFileTask.getExistFiles();
						if (files != null && files.length > 0) {
							display.syncExec(new Runnable() {
								public void run() {
									OverrideFileDialog dialog = new OverrideFileDialog(
											getShell());
									dialog.setFiles(files);
									if (dialog.open() != IDialogConstants.OK_ID) {
										isCanFinished = false;
									}
								}
							});
						}
					} else if (task instanceof GetBackupVolInfoTask) {
						if (database.getRunningType() == DbRunningType.CS) {
							display.syncExec(new Runnable() {
								public void run() {
									CommonTool.openInformationBox(getShell(),
											Messages.titleSuccess,
											Messages.msgBackupSuccess);
								}
							});
						} else {
							GetBackupVolInfoTask getBackupVolInfoTask = (GetBackupVolInfoTask) task;
							final String backupVolInfo = getBackupVolInfoTask.getDbBackupVolInfo();
							if (backupVolInfo != null
									&& backupVolInfo.length() > 0) {
								display.syncExec(new Runnable() {
									public void run() {
										BackupDbVolumeInfoDialog backupDbResultInfoDialog = new BackupDbVolumeInfoDialog(
												getShell());
										backupDbResultInfoDialog.setResultInfoStr(backupVolInfo);
										backupDbResultInfoDialog.open();
									}
								});
							}
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
		String backupDir = backupDirText.getText();
		CheckDirTask checkDirTask = new CheckDirTask(
				database.getServer().getServerInfo());
		checkDirTask.setDirectory(new String[] { backupDir });

		CheckFileTask checkFileTask = new CheckFileTask(
				database.getServer().getServerInfo());
		String fileName = backupDirText.getText()
				+ database.getServer().getServerInfo().getPathSeparator()
				+ volumeNameText.getText();
		checkFileTask.setFile(new String[] { fileName });

		String databaseName = databaseNameText.getText();
		String level = backupLevelCombo.getText().replaceAll("Level", "");
		String volName = volumeNameText.getText();
		boolean isRemoveLog = archiveLogButton.getSelection();
		boolean isCheckDbCons = consistentButton.getSelection();
		boolean isZip = useCompressButton.getSelection();
		boolean isSafeReplication = safeBackupButton.getSelection();
		int threadNum = spnThreadNum.getSelection();

		BackupDbTask backupDbTask = new BackupDbTask(
				database.getServer().getServerInfo());
		backupDbTask.setDbName(databaseName);
		backupDbTask.setLevel(level);
		backupDbTask.setVolumeName(volName);
		backupDbTask.setBackupDir(backupDir);
		backupDbTask.setRemoveLog(isRemoveLog);
		backupDbTask.setCheckDatabaseConsist(isCheckDbCons);
		backupDbTask.setThreadCount(String.valueOf(threadNum));
		backupDbTask.setZiped(isZip);
		backupDbTask.setSafeReplication(isSafeReplication);

		GetBackupVolInfoTask getBackupVolInfoTask = new GetBackupVolInfoTask(
				database.getServer().getServerInfo());
		getBackupVolInfoTask.setDbName(databaseName);

		taskExcutor.addTask(checkDirTask);
		taskExcutor.addTask(checkFileTask);
		taskExcutor.addTask(backupDbTask);
		taskExcutor.addTask(getBackupVolInfoTask);
		new ExecTaskWithProgress(taskExcutor).exec(true, true);
	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {
		backupLevelCombo.add("Level0");
		if (dbBackupInfo != null) {
			List<DbBackupHistoryInfo> dbBackupHistoryInfoList = dbBackupInfo.getBackupHistoryList();
			if (dbBackupHistoryInfoList != null) {
				int size = dbBackupHistoryInfoList.size();
				for (int i = 1; i < size + 1 && i < 3; i++) {
					backupLevelCombo.add("Level" + i);
				}
			}
			backupDir = dbBackupInfo.getDbDir();
			if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
				backupDir = FileNameUtils.separatorsToWindows(backupDir);
			}
			backupDirText.setText(backupDir);
		}
		backupLevelCombo.select(backupLevelCombo.getItemCount() - 1);
		consistentButton.setSelection(true);
		useCompressButton.setSelection(true);
		if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
			safeBackupButton.setSelection(false);
		} else {
			if (isReplication() && archiveLogButton.getSelection()) {
				safeBackupButton.setSelection(true);
			} else {
				safeBackupButton.setSelection(false);
			}
		}
		changeVolumeName();
	}

	/**
	 * 
	 * Get backup history information list
	 * 
	 * @return
	 */
	private List<Map<String, String>> getBackupHistoryInfoList() {
		List<Map<String, String>> list = new ArrayList<Map<String, String>>();
		if (dbBackupInfo != null) {
			List<DbBackupHistoryInfo> dbBackupHistoryInfoList = dbBackupInfo.getBackupHistoryList();
			if (dbBackupHistoryInfoList != null) {
				int size = dbBackupHistoryInfoList.size();
				for (int i = 0; i < size; i++) {
					DbBackupHistoryInfo historyInfo = dbBackupHistoryInfoList.get(i);
					Map<String, String> map = new HashMap<String, String>();
					map.put("0", historyInfo.getLevel());
					String dateStr = historyInfo.getDate();
					if (dateStr != null && dateStr.trim().length() > 0) {
						String[] dateStrArr = dateStr.split("\\.");
						if (dateStrArr.length == 5) {
							dateStr = dateStrArr[0] + "." + dateStrArr[1] + "."
									+ dateStrArr[2] + " " + dateStrArr[3] + ":"
									+ dateStrArr[4];
						}
						map.put("1", dateStr);
					}
					String sizeStr = historyInfo.getSize();
					NumberFormat nf = NumberFormat.getInstance();
					nf.setMaximumFractionDigits(2);
					if (sizeStr != null && sizeStr.matches("^\\d+$")) {
						map.put("2", nf.format(Integer.parseInt(sizeStr)
								/ (1024 * 1024)));
					}
					String path = historyInfo.getPath();
					if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
						path = FileNameUtils.separatorsToWindows(path);
					}
					map.put("3", path);
					list.add(map);
				}
			}
		}
		return list;
	}

	/**
	 * 
	 * Change volume name
	 * 
	 */
	private void changeVolumeName() {
		String databaseName = databaseNameText.getText();
		String level = backupLevelCombo.getText().replaceAll("Level", "");
		volumeNameText.setText(databaseName + "_backup_lv" + level);
	}

	/**
	 * 
	 * Check the data validation
	 * 
	 * @param isShowDialog
	 * @return
	 */
	public boolean valid(boolean isShowDialog) {

		String volumeName = volumeNameText.getText();
		boolean isValidVolumeNameLength = volumeName.trim().length() > 0
				&& volumeName.indexOf(" ") < 0;
		if (volumeName.trim().length() <= 0 || !isValidVolumeNameLength) {
			if (isShowDialog)
				CommonTool.openErrorBox(getShell(), Messages.errVolumeName);
			return false;
		}
		String backupDir = backupDirText.getText();
		boolean isValidBackupDir = ValidateUtil.isValidPathName(backupDir);
		if (!isValidBackupDir) {
			if (isShowDialog)
				CommonTool.openErrorBox(getShell(), Messages.errBackupDir);
			return false;
		}
		int threadNum = spnThreadNum.getSelection();
		if (threadNum < 0) {
			if (isShowDialog)
				CommonTool.openErrorBox(getShell(), Messages.errParallerBackup);
			return false;
		}
		return true;

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

	/**
	 * 
	 * Set database backup information
	 * 
	 * @param dbBackupInfo
	 */
	public void setDbBackupInfo(DbBackupInfo dbBackupInfo) {
		this.dbBackupInfo = dbBackupInfo;
	}

	/**
	 * 
	 * Return replication status in cubrid.conf parameter file
	 * 
	 * @return
	 */
	public boolean isReplication() {
		return isReplication;
	}

	/**
	 * 
	 * Set replication status in cubrid.conf parameter file
	 * 
	 * @return
	 */
	public void setReplication(boolean isReplication) {
		this.isReplication = isReplication;
	}

}
