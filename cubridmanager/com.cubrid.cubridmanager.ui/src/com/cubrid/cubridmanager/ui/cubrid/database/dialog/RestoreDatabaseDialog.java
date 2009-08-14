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
import java.text.NumberFormat;
import java.util.Calendar;
import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.DirectoryDialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Spinner;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.database.task.GetBackupVolInfoTask;
import com.cubrid.cubridmanager.core.cubrid.database.task.RestoreDbTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.OsInfoType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.FileNameUtils;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * Restore database will use this dialog to fill in the information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class RestoreDatabaseDialog extends
		CMTitleAreaDialog implements
		SelectionListener,
		ModifyListener {

	private Text databaseNameText = null;
	private CubridDatabase database = null;
	private Button restoredDataTimeButton;
	private Spinner yearSpn;
	private Spinner monthSpn;
	private Spinner daySpn;
	private Spinner hourSpn;
	private Spinner minuteSpn;
	private Spinner secondSpn;
	private Button level2Button;
	private Text level2Text;
	private Text level1Text;
	private Button level1Button;
	private Button level0Button;
	private Text level0Text;
	private Button partialButton;
	private List<String> backupList = null;
	private Button showBackupInfoButton;
	private Button recoveryPathButton;
	private Text recoveryPathText;
	private Button selectPathButton;
	private Button backupTimeButton;
	private Button selectTimeButton;

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public RestoreDatabaseDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		getHelpSystem().setHelp(parent,
				CubridManagerHelpContextIDs.databaseRestore);

		Composite parentComp = (Composite) super.createDialogArea(parent);
		Composite composite = new Composite(parentComp, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);
		composite.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		createDatabseNameGroup(composite);
		createDataTimeGroup(composite);
		createBackupInfoGroup(composite);
		createPartialGroup(composite);
		//current server do not support to specify recovery path
		//createUserDefinedRecoveryPathGroup(composite);

		setTitle(Messages.titleRestoreDbDialog);
		setMessage(Messages.msgRestoreDbDialog);
		initial();
		return parentComp;
	}

	/**
	 * 
	 * Create database name group
	 * 
	 * @param parent
	 */
	private void createDatabseNameGroup(Composite parent) {
		Group databaseNameGroup = new Group(parent, SWT.NONE);
		databaseNameGroup.setText(Messages.grpDbName);
		databaseNameGroup.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		GridLayout layout = new GridLayout();
		layout.numColumns = 4;
		databaseNameGroup.setLayout(layout);

		Label databaseNameLabel = new Label(databaseNameGroup, SWT.LEFT
				| SWT.WRAP);
		databaseNameLabel.setText(Messages.lblDbName);
		databaseNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		databaseNameText = new Text(databaseNameGroup, SWT.BORDER);
		if (database != null)
			databaseNameText.setText(database.getLabel());
		databaseNameText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));
		databaseNameText.setEditable(false);
	}

	/**
	 * 
	 * Create restored date and time information group
	 * 
	 * @param parent
	 */
	private void createDataTimeGroup(Composite parent) {
		Group dataTimeGroup = new Group(parent, SWT.NONE);
		dataTimeGroup.setText(Messages.grpRestoredDate);
		GridLayout layout = new GridLayout();
		layout.numColumns = 9;
		layout.horizontalSpacing = 2;
		dataTimeGroup.setLayout(layout);
		dataTimeGroup.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		selectTimeButton = new Button(dataTimeGroup, SWT.LEFT | SWT.CHECK);
		selectTimeButton.setText(Messages.btnSelectDateAndTime);
		selectTimeButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 9, 1, -1, -1));
		selectTimeButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (selectTimeButton.getSelection()) {
					backupTimeButton.setEnabled(true);
					restoredDataTimeButton.setEnabled(true);
					if (restoredDataTimeButton.getSelection()) {
						yearSpn.setEnabled(true);
						monthSpn.setEnabled(true);
						daySpn.setEnabled(true);
						hourSpn.setEnabled(true);
						minuteSpn.setEnabled(true);
						secondSpn.setEnabled(true);
					} else {
						yearSpn.setEnabled(false);
						monthSpn.setEnabled(false);
						daySpn.setEnabled(false);
						hourSpn.setEnabled(false);
						minuteSpn.setEnabled(false);
						secondSpn.setEnabled(false);
					}
					if (!backupTimeButton.getSelection()
							&& !restoredDataTimeButton.getSelection()) {
						backupTimeButton.setSelection(true);
					}
				} else {
					backupTimeButton.setEnabled(false);
					restoredDataTimeButton.setEnabled(false);
					yearSpn.setEnabled(false);
					monthSpn.setEnabled(false);
					daySpn.setEnabled(false);
					hourSpn.setEnabled(false);
					minuteSpn.setEnabled(false);
					secondSpn.setEnabled(false);
				}
			}
		});

		backupTimeButton = new Button(dataTimeGroup, SWT.LEFT | SWT.RADIO);
		backupTimeButton.setText(Messages.btnBackupTime);
		backupTimeButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 9, 1, -1, -1));
		backupTimeButton.addSelectionListener(this);

		restoredDataTimeButton = new Button(dataTimeGroup, SWT.LEFT | SWT.RADIO);
		restoredDataTimeButton.setText(Messages.btnRestoreDate);
		restoredDataTimeButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 9, 1, -1, -1));
		restoredDataTimeButton.addSelectionListener(this);

		Label dateLabel = new Label(dataTimeGroup, SWT.LEFT);
		dateLabel.setText(Messages.lblDate);
		dateLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		yearSpn = new Spinner(dataTimeGroup, SWT.BORDER);
		yearSpn.setMinimum(1);
		Calendar cal = Calendar.getInstance();
		yearSpn.setMaximum(cal.get(Calendar.YEAR));
		yearSpn.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		yearSpn.addSelectionListener(this);
		yearSpn.addModifyListener(this);

		monthSpn = new Spinner(dataTimeGroup, SWT.BORDER);
		monthSpn.setMinimum(1);
		monthSpn.setMaximum(12);
		monthSpn.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		monthSpn.addSelectionListener(this);
		monthSpn.addModifyListener(this);

		daySpn = new Spinner(dataTimeGroup, SWT.BORDER);
		daySpn.setMinimum(1);
		daySpn.setMaximum(31);
		daySpn.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		daySpn.addSelectionListener(this);
		daySpn.addModifyListener(this);

		Label timeLabel = new Label(dataTimeGroup, SWT.LEFT);
		timeLabel.setText(Messages.lblTime);
		timeLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		hourSpn = new Spinner(dataTimeGroup, SWT.BORDER);
		hourSpn.setMinimum(0);
		hourSpn.setMaximum(23);
		hourSpn.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		hourSpn.addSelectionListener(this);
		hourSpn.addModifyListener(this);

		minuteSpn = new Spinner(dataTimeGroup, SWT.BORDER);
		minuteSpn.setMinimum(0);
		minuteSpn.setMaximum(59);
		minuteSpn.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		minuteSpn.addSelectionListener(this);
		minuteSpn.addModifyListener(this);

		secondSpn = new Spinner(dataTimeGroup, SWT.BORDER);
		secondSpn.setMinimum(0);
		secondSpn.setMaximum(59);
		secondSpn.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		secondSpn.addSelectionListener(this);
		secondSpn.addModifyListener(this);
		Label label = new Label(dataTimeGroup, SWT.NONE);
		label.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
	}

	/**
	 * 
	 * Create available backup information group
	 * 
	 * @param parent
	 */
	private void createBackupInfoGroup(Composite parent) {
		Group backupInfoGroup = new Group(parent, SWT.NONE);
		backupInfoGroup.setText(Messages.grpBackupInfo);
		GridLayout layout = new GridLayout();
		layout.numColumns = 4;
		backupInfoGroup.setLayout(layout);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		backupInfoGroup.setLayoutData(gridData);

		level2Button = new Button(backupInfoGroup, SWT.LEFT | SWT.RADIO);
		level2Button.setText(Messages.btnLevel2File);
		level2Button.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		level2Button.setEnabled(false);

		level2Text = new Text(backupInfoGroup, SWT.BORDER);
		level2Text.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));
		level2Text.setEnabled(false);
		level2Text.addModifyListener(this);

		level1Button = new Button(backupInfoGroup, SWT.LEFT | SWT.RADIO);
		level1Button.setText(Messages.btnLevel1File);
		level1Button.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		level1Button.setEnabled(false);

		level1Text = new Text(backupInfoGroup, SWT.BORDER);
		level1Text.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));
		level1Text.setEnabled(false);
		level1Text.addModifyListener(this);

		level0Button = new Button(backupInfoGroup, SWT.LEFT | SWT.RADIO);
		level0Button.setText(Messages.btnLevel0File);
		level0Button.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		level0Button.setEnabled(false);

		level0Text = new Text(backupInfoGroup, SWT.BORDER);
		level0Text.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));
		level0Text.setEnabled(false);
		level0Text.addModifyListener(this);

		showBackupInfoButton = new Button(backupInfoGroup, SWT.CENTER
				| SWT.PUSH);
		showBackupInfoButton.setText(Messages.btnShowBackupInfo);
		showBackupInfoButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL | GridData.HORIZONTAL_ALIGN_END, 4, 1,
				-1, -1));
		showBackupInfoButton.setEnabled(false);
		showBackupInfoButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				String level = "";
				String path = "none";
				int size = 0;
				if (backupList == null) {
					return;
				}
				if (backupList != null)
					size = backupList.size();
				if (level0Button.getSelection()) {
					level = "0";
					if (size > 0
							&& level0Text.getText().equals(backupList.get(0))) {
						path = level0Text.getText();
					}
				} else if (level1Button.getSelection()) {
					level = "1";
					if (size > 1
							&& level1Text.getText().equals(backupList.get(1))) {
						path = level1Text.getText();
					}
				} else if (level2Button.getSelection()) {
					level = "2";
					if (size > 2
							&& level2Text.getText().equals(backupList.get(2))) {
						path = level2Text.getText();
					}
				}
				showBackupVolumeInfo(level, path);
			}
		});

	}

	/**
	 * 
	 * Create partial recovery information group
	 * 
	 * @param parent
	 */
	private void createPartialGroup(Composite parent) {
		Group partialGroup = new Group(parent, SWT.NONE);
		partialGroup.setText(Messages.grpPartialRecovery);
		GridLayout layout = new GridLayout();
		layout.numColumns = 3;
		partialGroup.setLayout(layout);
		partialGroup.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		partialButton = new Button(partialGroup, SWT.LEFT | SWT.CHECK);
		partialButton.setText(Messages.btnPerformPartialRecovery);
		partialButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));
	}

	/**
	 * 
	 * Create user defined recovery path group
	 * 
	 * @param parent
	 */
	@SuppressWarnings("unused")
	private void createUserDefinedRecoveryPathGroup(Composite parent) {
		Group recoveryPathGroup = new Group(parent, SWT.NONE);
		recoveryPathGroup.setText(Messages.grpRecoveryPath);
		GridLayout layout = new GridLayout();
		layout.numColumns = 3;
		recoveryPathGroup.setLayout(layout);
		recoveryPathGroup.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		recoveryPathButton = new Button(recoveryPathGroup, SWT.LEFT | SWT.CHECK);
		recoveryPathButton.setText(Messages.btnUserDefinedRecoveryPath);
		recoveryPathButton.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		recoveryPathButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (recoveryPathButton.getSelection()) {
					recoveryPathText.setEnabled(true);
					ServerInfo serverInfo = database.getServer().getServerInfo();
					if (serverInfo != null && !serverInfo.isLocalServer()) {
						selectPathButton.setEnabled(false);
					} else {
						selectPathButton.setEnabled(true);
					}

				} else {
					recoveryPathText.setEnabled(false);
					selectPathButton.setEnabled(false);
				}
				valid();
			}
		});
		recoveryPathText = new Text(recoveryPathGroup, SWT.BORDER);
		recoveryPathText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		recoveryPathText.setEnabled(false);
		recoveryPathText.addModifyListener(this);

		selectPathButton = new Button(recoveryPathGroup, SWT.NONE);
		selectPathButton.setText(Messages.btnBrowse);
		selectPathButton.setLayoutData(CommonTool.createGridData(1, 1, 80, -1));
		selectPathButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				DirectoryDialog dlg = new DirectoryDialog(getShell());
				String dbDir = database.getDatabaseInfo().getDbDir();
				if (dbDir != null && dbDir.trim().length() > 0)
					dlg.setFilterPath(dbDir);
				dlg.setText(Messages.msgSelectDir);
				dlg.setMessage(Messages.msgSelectDir);
				String dir = dlg.open();
				if (dir != null) {
					recoveryPathText.setText(dir);
				}
			}
		});
		selectPathButton.setEnabled(false);
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleRestoreDbDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);
		if (this.backupList == null || this.backupList.size() <= 0) {
			getButton(IDialogConstants.OK_ID).setEnabled(false);
		}
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			restoreDb(buttonId);
		} else {
			super.buttonPressed(buttonId);
		}
	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {
		selectTimeButton.setSelection(false);
		backupTimeButton.setEnabled(false);
		restoredDataTimeButton.setEnabled(false);
		backupTimeButton.setSelection(false);
		restoredDataTimeButton.setSelection(false);
		yearSpn.setEnabled(false);
		monthSpn.setEnabled(false);
		daySpn.setEnabled(false);
		hourSpn.setEnabled(false);
		minuteSpn.setEnabled(false);
		secondSpn.setEnabled(false);
		Calendar cal = Calendar.getInstance();
		yearSpn.setSelection(cal.get(Calendar.YEAR));
		monthSpn.setSelection(cal.get(Calendar.MONTH) + 1);
		daySpn.setSelection(cal.get(Calendar.DATE));
		hourSpn.setSelection(cal.get(Calendar.HOUR_OF_DAY));
		minuteSpn.setSelection(cal.get(Calendar.MINUTE));
		secondSpn.setSelection(cal.get(Calendar.SECOND));
		if (this.backupList != null && this.backupList.size() > 0) {
			if (backupList.size() > 0) {
				level0Button.setEnabled(true);
				level0Button.setSelection(true);
				String path = backupList.get(0);
				if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
					path = FileNameUtils.separatorsToWindows(path);
				}
				level0Text.setText(path);
			}
			if (backupList.size() > 1) {
				level1Button.setEnabled(true);
				level1Button.setSelection(false);
				String path = backupList.get(1);
				if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
					path = FileNameUtils.separatorsToWindows(path);
				}
				level1Text.setText(path);
			}
			if (backupList.size() > 2) {
				level2Button.setEnabled(true);
				level2Button.setSelection(false);
				String path = backupList.get(2);
				if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
					path = FileNameUtils.separatorsToWindows(path);
				}
				level2Text.setText(path);
			}
			showBackupInfoButton.setEnabled(true);
		} else {
			level2Button.setEnabled(false);
			level1Button.setEnabled(false);
			level0Button.setEnabled(false);
			level2Text.setEnabled(false);
			level1Text.setEnabled(false);
			level0Text.setEnabled(false);
			showBackupInfoButton.setEnabled(false);
		}
	}

	public void widgetSelected(SelectionEvent e) {
		if (e.widget == backupTimeButton) {
			if (backupTimeButton.getSelection()) {
				yearSpn.setEnabled(false);
				monthSpn.setEnabled(false);
				daySpn.setEnabled(false);
				hourSpn.setEnabled(false);
				minuteSpn.setEnabled(false);
				secondSpn.setEnabled(false);
			}
		}
		if (e.widget == restoredDataTimeButton) {
			if (restoredDataTimeButton.getSelection()) {
				yearSpn.setEnabled(true);
				monthSpn.setEnabled(true);
				daySpn.setEnabled(true);
				hourSpn.setEnabled(true);
				minuteSpn.setEnabled(true);
				secondSpn.setEnabled(true);
			} else {
				yearSpn.setEnabled(false);
				monthSpn.setEnabled(false);
				daySpn.setEnabled(false);
				hourSpn.setEnabled(false);
				minuteSpn.setEnabled(false);
				secondSpn.setEnabled(false);
			}
		}
		valid();
	}

	/**
	 * 
	 * Check the validation
	 * 
	 * @return
	 */
	private boolean valid() {
		boolean isValidYear = true;
		boolean isValidMonth = true;
		boolean isValidDay = true;
		boolean isValidHour = true;
		boolean inValidMinute = true;
		boolean isValidSecond = true;
		boolean isValidLevel0Text = true;
		boolean isValidLevel1Text = true;
		boolean isValidLevel2Text = true;
		boolean isValidRecoveryPath = true;
		if (restoredDataTimeButton.getSelection()) {
			Calendar cal = Calendar.getInstance();
			int year = yearSpn.getSelection();
			isValidYear = year > 0 && year <= cal.get(Calendar.YEAR);
			int month = monthSpn.getSelection();
			isValidMonth = month > 0 && month <= 12;
			int day = daySpn.getSelection();
			isValidDay = day > 0 && day <= 31;
			int hour = hourSpn.getSelection();
			isValidHour = hour >= 0 && hour <= 23;
			int minute = minuteSpn.getSelection();
			inValidMinute = minute >= 0 && minute < 60;
			int second = secondSpn.getSelection();
			isValidSecond = second >= 0 && second < 60;
		}
		if (level2Button.getSelection()) {
			String path = level2Text.getText();
			isValidLevel2Text = ValidateUtil.isValidPathName(path);
			if (isValidLevel2Text
					&& database.getServer().getServerInfo().isLocalServer()) {
				File file = new File(path);
				if (!file.exists()) {
					isValidLevel2Text = false;
				}
			}
		} else if (level1Button.getSelection()) {
			String path = level1Text.getText();
			isValidLevel1Text = ValidateUtil.isValidPathName(path);
			if (isValidLevel1Text
					&& database.getServer().getServerInfo().isLocalServer()) {
				File file = new File(path);
				if (!file.exists()) {
					isValidLevel1Text = false;
				}
			}
		} else if (level0Button.getSelection()) {
			String path = level0Text.getText();
			isValidLevel0Text = ValidateUtil.isValidPathName(path);
			if (isValidLevel0Text
					&& database.getServer().getServerInfo().isLocalServer()) {
				File file = new File(path);
				if (!file.exists()) {
					isValidLevel0Text = false;
				}
			}
		}
		/*if (recoveryPathButton.getSelection()) {
					String path = recoveryPathText.getText();
					isValidRecoveryPath = ValidateUtil.isValidPathName(path);
				}*/
		if (!isValidYear) {
			setErrorMessage(Messages.errYear);
		} else if (!isValidMonth) {
			setErrorMessage(Messages.errMonth);
		} else if (!isValidDay) {
			setErrorMessage(Messages.errDay);
		} else if (!isValidHour) {
			setErrorMessage(Messages.errHour);
		} else if (!inValidMinute) {
			setErrorMessage(Messages.errMinute);
		} else if (!isValidSecond) {
			setErrorMessage(Messages.errSecond);
		} else if (!isValidLevel2Text) {
			setErrorMessage(Messages.errLevel2File);
		} else if (!isValidLevel1Text) {
			setErrorMessage(Messages.errLevel1File);
		} else if (!isValidLevel0Text) {
			setErrorMessage(Messages.errLevel0File);
		} else if (!isValidRecoveryPath) {
			setErrorMessage(Messages.errRecoveryPath);
		}
		boolean isValid = isValidYear && isValidMonth && isValidDay
				&& isValidHour && inValidMinute && isValidSecond
				&& isValidLevel2Text && isValidLevel1Text && isValidLevel0Text
				&& isValidRecoveryPath;
		if (isValid) {
			setErrorMessage(null);
		}
		changeOkButtonStatus(isValid);
		return isValid;
	}

	public void widgetDefaultSelected(SelectionEvent e) {

	}

	public void modifyText(ModifyEvent e) {
		valid();
	}

	/**
	 * 
	 * Change button stauts(enabled or disabled)
	 * 
	 * @param isEnabled
	 */
	private void changeOkButtonStatus(boolean isEnabled) {
		if (getButton(IDialogConstants.OK_ID) != null) {
			getButton(IDialogConstants.OK_ID).setEnabled(isEnabled);
			if (isEnabled
					&& (this.backupList == null || this.backupList.size() <= 0)) {
				getButton(IDialogConstants.OK_ID).setEnabled(false);
			}
		}
	}

	/**
	 * 
	 * Show backup volume information
	 * 
	 * @param level
	 * @param path
	 */
	private void showBackupVolumeInfo(String level, String path) {
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
					if (task instanceof GetBackupVolInfoTask) {
						GetBackupVolInfoTask getBackupVolInfoTask = (GetBackupVolInfoTask) task;
						final String backupVolInfo = getBackupVolInfoTask.getDbBackupVolInfo();
						if (backupVolInfo != null && backupVolInfo.length() > 0) {
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
				return true;
			}
		};
		String databaseName = databaseNameText.getText();
		GetBackupVolInfoTask getBackupVolInfoTask = new GetBackupVolInfoTask(
				database.getServer().getServerInfo());
		getBackupVolInfoTask.setDbName(databaseName);
		getBackupVolInfoTask.setLevel(level);
		getBackupVolInfoTask.setPath(path);
		taskExcutor.addTask(getBackupVolInfoTask);
		new ExecTaskWithProgress(taskExcutor).exec(true, true);
	}

	/**
	 * 
	 * Restore database
	 * 
	 */
	private void restoreDb(final int buttonId) {
		if (!valid()) {
			return;
		}
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
				}
				if (!monitor.isCanceled()) {
					display.syncExec(new Runnable() {
						public void run() {
							CommonTool.openInformationBox(getShell(),
									Messages.titleSuccess,
									Messages.msgRestoreSuccess);
							setReturnCode(buttonId);
							close();
						}
					});
				}
				return true;
			}
		};
		String databaseName = databaseNameText.getText();
		RestoreDbTask restoreDbTask = new RestoreDbTask(
				database.getServer().getServerInfo());
		restoreDbTask.setDbName(databaseName);
		String level = "";
		String path = "none";
		int size = 0;
		if (backupList != null)
			size = backupList.size();
		if (level0Button.getSelection()) {
			level = "0";
			if (size > 0 && level0Text.getText().equals(backupList.get(0))) {
				path = level0Text.getText();
			}
		} else if (level1Button.getSelection()) {
			level = "1";
			if (size > 1 && level1Text.getText().equals(backupList.get(1))) {
				path = level1Text.getText();
			}
		} else if (level2Button.getSelection()) {
			level = "2";
			if (size > 2 && level2Text.getText().equals(backupList.get(2))) {
				path = level2Text.getText();
			}
		}
		path = "none";//Now,server don't support to specify the path
		restoreDbTask.setLevel(level);
		restoreDbTask.setPathName(path);
		if (selectTimeButton.getSelection()) {
			if (backupTimeButton.getSelection()) {
				restoreDbTask.setDate("backuptime");
			} else if (restoredDataTimeButton.getSelection()) {
				NumberFormat nf = NumberFormat.getInstance();
				nf.setMinimumIntegerDigits(2);
				String timeStr = nf.format(hourSpn.getSelection()) + ":"
						+ nf.format(minuteSpn.getSelection()) + ":"
						+ nf.format(secondSpn.getSelection());
				String dateStr = nf.format(daySpn.getSelection()) + "-"
						+ nf.format(monthSpn.getSelection()) + "-"
						+ yearSpn.getSelection();
				dateStr += ":" + timeStr;
				restoreDbTask.setDate(dateStr);
			} else {
				restoreDbTask.setDate("none");
			}
		} else {
			restoreDbTask.setDate("none");
		}

		if (partialButton.getSelection()) {
			restoreDbTask.setPartial(true);
		} else {
			restoreDbTask.setPartial(false);
		}
		//current server do not support to specify the recovery path
		/*if (recoveryPathButton.getSelection()) {
			restoreDbTask.setRecoveryPath(recoveryPathText.getText());
		} else {
			restoreDbTask.setRecoveryPath("none");
		}*/
		restoreDbTask.setRecoveryPath("none");
		taskExcutor.addTask(restoreDbTask);
		new ExecTaskWithProgress(taskExcutor).exec(true, true);
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
	 * Set backup list
	 * 
	 * @param backupList
	 */
	public void setBackupList(List<String> backupList) {
		this.backupList = backupList;
	}
}
