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
package com.cubrid.cubridmanager.ui.cubrid.jobauto.dialog;

import java.util.ArrayList;
import java.util.List;
import java.util.Observable;
import java.util.Observer;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.NullProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
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
import org.eclipse.swt.widgets.DirectoryDialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Spinner;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.AddEditType;
import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.common.model.OnOffType;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.BackupPlanInfo;
import com.cubrid.cubridmanager.core.cubrid.jobauto.task.BackupPlanTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.OsInfoType;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.YesNoType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.Messages;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.control.PeriodGroup;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.FileNameUtils;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * A dialog that show up when a user click the backup plan context menu.
 * 
 * @author lizhiqiang 2009-3-12
 */
public class EditBackupPlanDialog extends
		CMTitleAreaDialog implements
		Observer {

	private static final int MAX_THREAD = 64;
	private static final String ZERO_LEVER = Messages.zeroLever;
	private static final String ONE_LEVER = Messages.oneLever;
	private static final String TWO_LEVER = Messages.twoLever;
	private Combo leverCombo;
	private Text idText;
	private Text pathText;
	private Button storeButton;
	private Button deleteButton;
	private Button updateButton;
	private Button checkingButton;
	private Button useCompressButton;
	private Spinner numThreadspinner;

	private CubridDatabase database;
	private String opBackupInfo;
	private AddEditType operation;
	private PeriodGroup periodGroup;
	private Button onlineButton;
	private Button offlineButton;
	private String defaultPath;

	private boolean isOkenable[];
	private BackupPlanInfo backupPlanInfo;
	private List<String> childrenLabel;
	private int backplanIdMaxLen = Integer.valueOf(Messages.backplanIdMaxLen);

	/**
	 * The Constructor
	 * 
	 * @param parentShell
	 */
	public EditBackupPlanDialog(Shell parentShell) {
		super(parentShell);
		isOkenable = new boolean[6];
		for (int k = 0; k < isOkenable.length; k++) {
			isOkenable[k] = true;
		}
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp, CubridManagerHelpContextIDs.databaseJobauto);
		final Composite composite = new Composite(parentComp, SWT.RESIZE);
		final GridData gd_composite = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		gd_composite.widthHint = 500;
		composite.setLayoutData(gd_composite);
		final GridLayout gridLayout = new GridLayout();
		gridLayout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		gridLayout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		gridLayout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		gridLayout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(gridLayout);

		createBasicGroup(composite);
		periodGroup = new PeriodGroup(this);
		periodGroup.addObserver(this);
		if (operation.equals(AddEditType.EDIT)) {
			// Sets the edit title and message
			setMessage(Messages.editBackupPlanMsg);
			setTitle(Messages.editBackupPlanTitle);
			getShell().setText(Messages.editBackupPlanTitle);
			// Sets the initial value in periodGroup
			periodGroup.setTypeValue((backupPlanInfo.getPeriod_type()));
			periodGroup.setDetailValue(backupPlanInfo.getPeriod_date());
			String time = backupPlanInfo.getTime();
			String shour = time.substring(0, 2);
			String sminute = time.substring(2);
			int hour = Integer.parseInt(shour.startsWith("0") ? shour.substring(1)
					: shour);
			int minute = Integer.parseInt(sminute.startsWith("0") ? sminute.substring(1)
					: sminute);
			periodGroup.setHourValue(hour);
			periodGroup.setMinuteValue(minute);
			isOkenable[0] = true;
		} else {
			setMessage(Messages.addBackupPlanMsg);
			setTitle(Messages.addBackupPlanTitle);
			getShell().setText(Messages.addBackupPlanTitle);
			isOkenable[0] = false;
		}
		periodGroup.createPeriodGroup(composite);
		createOptionsGroup(composite);
		return parentComp;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		super.createButtonsForButtonBar(parent);
		if (operation.equals(AddEditType.ADD))
			getButton(IDialogConstants.OK_ID).setEnabled(false);
	}

	/*
	 * create the basic group in the Dialog
	 */
	private void createBasicGroup(Composite composite) {
		final Group generalInfoGroup = new Group(composite, SWT.RESIZE);
		generalInfoGroup.setText(Messages.basicGroupName);
		GridLayout groupLayout = new GridLayout();
		groupLayout.verticalSpacing = 0;
		generalInfoGroup.setLayout(groupLayout);
		final GridData gd_generalInfoGroup = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		generalInfoGroup.setLayoutData(gd_generalInfoGroup);

		Composite idComposite = new Composite(generalInfoGroup, SWT.RESIZE);
		final GridLayout idGridLayout = new GridLayout(4, false);
		idComposite.setLayout(idGridLayout);
		idComposite.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		final Label idLabel = new Label(idComposite, SWT.RESIZE);
		final GridData gd_idLabel = new GridData(SWT.CENTER, SWT.CENTER, false,
				false, 1, 1);
		gd_idLabel.widthHint = 80;
		idLabel.setLayoutData(gd_idLabel);

		idLabel.setText(Messages.msgIdLbl);
		idText = new Text(idComposite, SWT.BORDER | SWT.RESIZE);
		final GridData gd_idText = new GridData(SWT.FILL, SWT.CENTER, true,
				false, 1, 1);
		gd_idText.widthHint = 140;
		idText.setLayoutData(gd_idText);

		final Label levelLabel = new Label(idComposite, SWT.RESIZE);
		final GridData gd_levelLabel = new GridData(SWT.CENTER, SWT.CENTER,
				false, false, 1, 1);
		gd_levelLabel.widthHint = 80;
		levelLabel.setLayoutData(gd_levelLabel);
		levelLabel.setText(Messages.msgLevelLbl);

		leverCombo = new Combo(idComposite, SWT.NONE | SWT.READ_ONLY);
		leverCombo.setItems(new String[] { ZERO_LEVER, ONE_LEVER, TWO_LEVER });
		final GridData gd_leverCombo = new GridData(SWT.FILL, SWT.CENTER, true,
				false, 1, 1);
		gd_leverCombo.widthHint = 135;
		leverCombo.setLayoutData(gd_leverCombo);
		leverCombo.select(0);

		Composite pathComposite = new Composite(generalInfoGroup, SWT.RESIZE);
		pathComposite.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		final GridLayout pathGridLayout = new GridLayout(3, false);
		pathComposite.setLayout(pathGridLayout);
		pathComposite.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		final Label pathLabel = new Label(pathComposite, SWT.RESIZE);
		final GridData gdPathLabel = new GridData(SWT.CENTER, SWT.CENTER,
				false, false, 1, 1);
		gdPathLabel.widthHint = 80;
		pathLabel.setLayoutData(gdPathLabel);
		pathLabel.setText(Messages.msgPathLbl);

		pathText = new Text(pathComposite, SWT.BORDER);
		final GridData gd_pathText = new GridData(SWT.FILL, SWT.CENTER, true,
				false, 1, 1);
		gd_pathText.widthHint = 240;
		pathText.setLayoutData(gd_pathText);

		Button selectTargetDirectoryButton = new Button(pathComposite, SWT.NONE);
		selectTargetDirectoryButton.setText(Messages.btnBrowse);
		selectTargetDirectoryButton.setLayoutData(CommonTool.createGridData(1,
				1, 80, -1));
		selectTargetDirectoryButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				DirectoryDialog dlg = new DirectoryDialog(getShell());
				String backupDir = pathText.getText();
				if (backupDir != null && backupDir.trim().length() > 0)
					dlg.setFilterPath(backupDir);
				dlg.setText(Messages.msgSelectDir);
				dlg.setMessage(Messages.msgSelectDir);
				String dir = dlg.open();
				if (dir != null) {
					pathText.setText(dir);
				}
			}
		});
		ServerInfo serverInfo = database.getServer().getServerInfo();
		if (serverInfo != null && !serverInfo.isLocalServer()) {
			selectTargetDirectoryButton.setEnabled(false);
		}
		// sets the initial value
		if (operation.equals(AddEditType.EDIT)) {
			idText.setText(backupPlanInfo.getBackupid());
			int selected = Integer.parseInt(backupPlanInfo.getLevel());
			leverCombo.select(selected);
			pathText.setText(backupPlanInfo.getPath());
			idText.setEditable(false);
		} else {
			idText.setEditable(true);
			pathText.setText(defaultPath);
		}
		idText.addModifyListener(new IdTextModifyListener());
		pathText.addModifyListener(new PathTextModifyListener());
	}

	/*
	 * create the options group in the Dialog
	 */
	private void createOptionsGroup(Composite composite) {
		final Group optionsGroup = new Group(composite, SWT.NONE);
		final GridData gdOptionsGroup = new GridData(SWT.FILL, SWT.CENTER,
				true, false, 1, 1);

		optionsGroup.setLayoutData(gdOptionsGroup);
		GridLayout groupLayout = new GridLayout(1, true);
		optionsGroup.setLayout(groupLayout);

		final Group checkGroup = new Group(optionsGroup, SWT.NONE);

		checkGroup.setText(Messages.optionGroupName);
		checkGroup.setLayout(new GridLayout(2, true));
		final GridData gd_checkGroup = new GridData(SWT.FILL, SWT.TOP, true,
				false);
		checkGroup.setLayoutData(gd_checkGroup);

		storeButton = new Button(checkGroup, SWT.CHECK);
		storeButton.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		storeButton.setText(Messages.msgStroreBtn);

		deleteButton = new Button(checkGroup, SWT.CHECK);
		deleteButton.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		deleteButton.setText(Messages.msgDeleteBtn);

		updateButton = new Button(checkGroup, SWT.CHECK);
		updateButton.setText(Messages.msgUpdateBtn);

		checkingButton = new Button(checkGroup, SWT.CHECK);
		checkingButton.setText(Messages.msgCheckingBtn);

		useCompressButton = new Button(checkGroup, SWT.CHECK);
		useCompressButton.setText(Messages.msgUseCompressBtn);

		final Composite threadComposite = new Composite(checkGroup, SWT.NONE);
		final GridData gd_threadComposite = new GridData(SWT.LEFT, SWT.CENTER,
				false, false);
		gd_threadComposite.minimumHeight = 1;
		threadComposite.setLayoutData(gd_threadComposite);
		final GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		threadComposite.setLayout(gridLayout);

		final Label numThreadLabel = new Label(threadComposite, SWT.NONE);
		numThreadLabel.setText(Messages.msgNumThreadLbl);

		numThreadspinner = new Spinner(threadComposite, SWT.BORDER);
		numThreadspinner.setLayoutData(new GridData(SWT.RIGHT, SWT.TOP, false,
				true));
		numThreadspinner.setMaximum(MAX_THREAD);
		final Group radioGroup = new Group(optionsGroup, SWT.None);
		radioGroup.setText(Messages.msgComboGroup);
		final GridData gd_comboGroup = new GridData(SWT.FILL, SWT.TOP, true,
				false);
		gd_comboGroup.widthHint = 230;
		radioGroup.setLayoutData(gd_comboGroup);
		radioGroup.setLayout(new GridLayout());

		onlineButton = new Button(radioGroup, SWT.RADIO);
		onlineButton.setText(Messages.msgOnlineBtn);

		offlineButton = new Button(radioGroup, SWT.RADIO);
		offlineButton.setText(Messages.msgOfflineBtn);

		if (operation.equals(AddEditType.EDIT)) {
			storeButton.setSelection(CommonTool.str2Boolean(backupPlanInfo.getStoreold()));
			deleteButton.setSelection(CommonTool.str2Boolean(backupPlanInfo.getArchivedel()));
			updateButton.setSelection(CommonTool.str2Boolean(backupPlanInfo.getUpdatestatus()));
			checkingButton.setSelection(CommonTool.str2Boolean(backupPlanInfo.getCheck()));
			useCompressButton.setSelection(CommonTool.str2Boolean(backupPlanInfo.getZip()));
			numThreadspinner.setSelection(Integer.valueOf(backupPlanInfo.getMt()));

			boolean originalOnline = CommonTool.str2Boolean(backupPlanInfo.getOnoff());
			if (originalOnline) {
				onlineButton.setSelection(true);
				offlineButton.setSelection(false);
				if (database.getRunningType().equals(DbRunningType.STANDALONE)) {
					onlineButton.setEnabled(false);
				}
			} else {
				onlineButton.setSelection(false);
				offlineButton.setSelection(true);
			}
		} else { // sets the status of onlineButton and offlineButton if
			if (database.getRunningType().equals(DbRunningType.CS)) {
				onlineButton.setSelection(true);
				offlineButton.setSelection(false);
			} else {
				onlineButton.setSelection(false);
				offlineButton.setSelection(true);
				onlineButton.setEnabled(false);
			}

		}

	}

	/*
	 * A class that response the change of idText
	 */
	private class IdTextModifyListener implements
			ModifyListener {

		public void modifyText(ModifyEvent e) {
			String id = idText.getText().trim();
			if (id.length() <= 0) {
				isOkenable[0] = false;
			} else if (id.length() > 0 && !ValidateUtil.isValidDBName(id)) {
				isOkenable[3] = false;
			} else if (id.length() > 0 && childrenLabel.contains(id)) {
				isOkenable[4] = false;
			} else if (id.length() > backplanIdMaxLen) {
				isOkenable[5] = false;
			} else {
				isOkenable[0] = true;
				isOkenable[3] = true;
				isOkenable[4] = true;
				isOkenable[5] = true;
			}
			enableOk();
		}
	}

	/**
	 * A class that response the change of pathText
	 */
	private class PathTextModifyListener implements
			ModifyListener {

		public void modifyText(ModifyEvent e) {
			String path = pathText.getText();
			if (path.length() > 0 && !ValidateUtil.isValidPathName(path)
					&& !ValidateUtil.isValidPathNameLength(path)) {
				isOkenable[2] = false;
			} else {
				isOkenable[2] = true;
			}
			enableOk();
		}

	}

	@Override
	public void okPressed() {
		// Gets the data of dialog
		String newBackupid = idText.getText().trim();
		String newPath = pathText.getText().trim();
		String newPeriodType = periodGroup.getTextOfTypeCombo();
		String newPeriodDate = periodGroup.getTextOfDetailCombo();
		String newTime = periodGroup.getTime();
		int intLever = leverCombo.getSelectionIndex();
		String newLever = Integer.toString(intLever);
		OnOffType newArchivedel = deleteButton.getSelection() ? OnOffType.ON
				: OnOffType.OFF;
		OnOffType newUpdatestatus = updateButton.getSelection() ? OnOffType.ON
				: OnOffType.OFF;
		OnOffType newStroreold = storeButton.getSelection() ? OnOffType.ON
				: OnOffType.OFF;
		OnOffType newOnoff = onlineButton.getSelection() ? OnOffType.ON
				: OnOffType.OFF;

		YesNoType newZip = useCompressButton.getSelection() ? YesNoType.Y
				: YesNoType.N;
		YesNoType newCheck = checkingButton.getSelection() ? YesNoType.Y
				: YesNoType.N;
		String newMt = Integer.valueOf(numThreadspinner.getSelection()).toString();
		// Sets the object of backupPlanInfo
		backupPlanInfo.setBackupid(newBackupid);
		backupPlanInfo.setPath(newPath);
		backupPlanInfo.setPeriod_type(newPeriodType);
		backupPlanInfo.setPeriod_date(newPeriodDate);
		backupPlanInfo.setTime(newTime);
		backupPlanInfo.setLevel(newLever);
		backupPlanInfo.setArchivedel(newArchivedel.getText());
		backupPlanInfo.setUpdatestatus(newUpdatestatus.getText());
		backupPlanInfo.setStoreold(newStroreold.getText());
		backupPlanInfo.setOnoff(newOnoff.getText());
		backupPlanInfo.setZip(newZip.getText());
		backupPlanInfo.setCheck(newCheck.getText());
		backupPlanInfo.setMt(newMt);
		// Executes the task
		ServerInfo serverInfo = database.getServer().getServerInfo();
		BackupPlanTask backupPlanTask = new BackupPlanTask(opBackupInfo,
				serverInfo);
		backupPlanTask.setDbname(database.getName());
		backupPlanTask.setBackupid(newBackupid);
		backupPlanTask.setPath(newPath);
		backupPlanTask.setPeriodType(newPeriodType);
		backupPlanTask.setPeriodDate(newPeriodDate);
		backupPlanTask.setTime(newTime);
		backupPlanTask.setLevel(newLever);
		backupPlanTask.setArchivedel(newArchivedel);
		backupPlanTask.setUpdatestatus(newUpdatestatus);
		backupPlanTask.setStoreold(newStroreold);
		backupPlanTask.setOnoff(newOnoff);
		backupPlanTask.setZip(newZip);
		backupPlanTask.setCheck(newCheck);
		backupPlanTask.setMt(newMt);

		TaskExecutor taskExecutor = new BackupPlanTaskExec();
		taskExecutor.addTask(backupPlanTask);
		new ExecTaskWithProgress(taskExecutor).exec();
	}

	/*
	 * 
	 * A task executor that is aim to complete adding or editing a certain
	 * backup plan task
	 * 
	 * @author lizhiqiang 2009-4-8
	 */
	private class BackupPlanTaskExec extends
			TaskExecutor {

		/**
		 * Override method
		 * 
		 * @param monitor
		 * @return
		 */

		public boolean exec(final IProgressMonitor monitor) {
			boolean isSuccess = true;
			Display display = getShell().getDisplay();

			if (monitor.isCanceled()) {
				isSuccess = false;
				return isSuccess;
			}

			for (ITask task : taskList) {
				task.execute();
				final String msg = task.getErrorMsg();
				if (monitor.isCanceled()) {
					return false;
				}
				if (msg != null && msg.length() > 0 && !monitor.isCanceled()) {
					isSuccess = false;
					display.syncExec(new Runnable() {
						public void run() {
							CommonTool.openErrorBox(msg);
						}
					});
					return isSuccess;
				}
				if (monitor.isCanceled()) {
					isSuccess = false;
					return isSuccess;
				}
			}
			if (!monitor.isCanceled()) {
				display.syncExec(new Runnable() {
					public void run() {
						setReturnCode(OK);
						close();
					}
				});
			}

			return isSuccess;
		}
	}

	/**
	 * Initials the backupPlanInfo,database and childrenLabel
	 * 
	 * @param selection the selection to set
	 */
	public void initPara(DefaultSchemaNode selection) {
		childrenLabel = new ArrayList<String>();
		ICubridNode[] childrenNode = null;
		database = selection.getDatabase();
		if (operation.equals(AddEditType.EDIT)) {
			backupPlanInfo = (BackupPlanInfo) selection.getAdapter(BackupPlanInfo.class);
			childrenNode = selection.getParent().getChildren(
					new NullProgressMonitor());
		} else {
			backupPlanInfo = new BackupPlanInfo();
			String dbPath = database.getDatabaseInfo().getDbDir();
			if (database.getServer().getServerInfo().getServerOsInfo() == OsInfoType.NT) {
				dbPath = FileNameUtils.separatorsToWindows(dbPath);
			}
			defaultPath = dbPath
					+ selection.getServer().getServerInfo().getPathSeparator()
					+ "backup";
			childrenNode = selection.getChildren(new NullProgressMonitor());
		}
		for (ICubridNode childNode : childrenNode) {
			childrenLabel.add(childNode.getLabel());
		}
	}

	/**
	 * @param operation the operation to set
	 */
	public void setOperation(AddEditType operation) {
		this.operation = operation;
		if (operation.equals(AddEditType.ADD)) {
			opBackupInfo = "addbackupinfo";
		} else if (operation.equals(AddEditType.EDIT)) {
			opBackupInfo = "setbackupinfo";
		}
	}

	/**
	 * Gets the instance of BackupPlanInfo
	 * 
	 * @return the backupPlanInfo
	 */
	public BackupPlanInfo getBackupPlanInfo() {
		return backupPlanInfo;
	}

	/**
	 * Observer the change of instance of the type Period
	 */
	public void update(Observable o, Object arg) {
		boolean isAllow = (Boolean) arg;
		isOkenable[1] = isAllow;
		enableOk();
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
		if (!isOkenable[0]) {
			setErrorMessage(Messages.errBackupPlanIdEmpty);
		} else if (!isOkenable[3]) {
			setErrorMessage(Messages.errIdTextMsg);
		} else if (!isOkenable[4]) {
			setErrorMessage(Messages.errIdRepeatMsg);
		} else if (!isOkenable[5]) {
			setErrorMessage(Messages.errBackplanIdLen);
		} else if (!isOkenable[2]) {
			setErrorMessage(Messages.errPathTextMsg);
		} else if (!isOkenable[1]) {
			periodGroup.enableOk();		
		} else {
			setErrorMessage(null);
		}
		if (is) {
			getButton(IDialogConstants.OK_ID).setEnabled(true);
		} else {
			getButton(IDialogConstants.OK_ID).setEnabled(false);
		}
	}

}
