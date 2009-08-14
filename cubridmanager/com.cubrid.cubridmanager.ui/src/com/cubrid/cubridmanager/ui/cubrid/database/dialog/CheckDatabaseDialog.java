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

import java.lang.reflect.InvocationTargetException;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.dialogs.ProgressMonitorDialog;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.common.task.CommonTaskName;
import com.cubrid.cubridmanager.core.common.task.CommonUpdateTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.YesNoType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * The Dialog of Check the Database
 * 
 * @author robin 2009-3-11
 */
public class CheckDatabaseDialog extends CMTitleAreaDialog {
	private static final Logger logger = LogUtil.getLogger(CheckDatabaseDialog.class);
	private Text dbNameText;
	private Composite parentComp;
	private CubridDatabase database = null;
	private Button repairButton = null;

	public CheckDatabaseDialog(Shell parentShell) {
		super(parentShell);
		
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see org.eclipse.jface.dialogs.TitleAreaDialog#createDialogArea(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	protected Control createDialogArea(Composite parent) {
		parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp, CubridManagerHelpContextIDs.databaseCheck);
		final Composite composite = new Composite(parentComp, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		createdbNameGroup(composite);
		createDescriptionGroup(composite);
		repairButton = new Button(composite, SWT.CHECK);
		repairButton.setLayoutData(new GridData(SWT.LEFT, SWT.CENTER, false, false, 2, 1));
		repairButton.setText(Messages.btnRepair);
		setTitle(Messages.titleCheckDbDialog);
		setMessage(Messages.msgCheckDbDialog);
		initial();
		return parentComp;
	}

	/**
	 * Create Description Group
	 * 
	 * @param composite
	 */
	private void createDescriptionGroup(Composite composite) {
		GridLayout layout = new GridLayout();
		final Group descGroup = new Group(composite, SWT.NONE);
		descGroup.setLayoutData(new GridData(GridData.FILL_BOTH));
		descGroup.setLayout(layout);
		descGroup.setText(Messages.grpCheckDescInfo);

		final Label text = new Label(descGroup, SWT.WRAP);
		text.setText(Messages.lblCheckDescInfo);
		text.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
	}

	/**
	 * Create Database Name Group
	 * 
	 * @param composite
	 */
	private void createdbNameGroup(Composite composite) {

		final Group dbnameGroup = new Group(composite, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.numColumns = 2;
		final GridData gd_dbnameGroup = new GridData(GridData.FILL_HORIZONTAL);
		dbnameGroup.setLayoutData(gd_dbnameGroup);
		dbnameGroup.setLayout(layout);

		final Label databaseName = new Label(dbnameGroup, SWT.LEFT | SWT.WRAP);

		databaseName.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		databaseName.setText(Messages.lblCheckDbName);

		dbNameText = new Text(dbnameGroup, SWT.BORDER);
		dbNameText.setEnabled(false);
		final GridData gd_dbNameText = new GridData(SWT.FILL, SWT.CENTER, true, false);
		dbNameText.setLayoutData(gd_dbNameText);
	}

	/**
	 * Init the dialog
	 * 
	 */
	private void initial() {
		dbNameText.setText(database.getName());
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog#constrainShellSize()
	 */
	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		getShell().setSize(400, 400);
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleCheckDbDialog);
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see org.eclipse.jface.dialogs.Dialog#createButtonsForButtonBar(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);
		getButton(IDialogConstants.OK_ID).setEnabled(true);
		createButton(parent, IDialogConstants.CANCEL_ID, com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see org.eclipse.jface.dialogs.Dialog#buttonPressed(int)
	 */
	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			if (!verify()) {
				return;
			} else {

				CommonUpdateTask task = new CommonUpdateTask(CommonTaskName.CHECK_DATABASE_TASK_NAME, database
				        .getServer().getServerInfo(), CommonSendMsg.commonDatabaseSendMsg);
				task.setDbName(database.getName());
				task.setRepairDb(repairButton.getSelection() ? YesNoType.Y : YesNoType.N);

				CheckDbTaskExec taskExecutor = new CheckDbTaskExec();

				taskExecutor.addTask(task);
				ExecTaskWithProgress exec = new ExecTaskWithProgress(taskExecutor);
				try {
					new ProgressMonitorDialog(null).run(true, true, exec);
				} catch (InvocationTargetException e) {
					logger.error(e.getMessage(), e);
				} catch (InterruptedException e) {
					logger.error(e.getMessage(), e);
				}


				if (task.getErrorMsg() == null)
					CommonTool.openInformationBox(parentComp.getShell(),
							Messages.titleSuccess, Messages.msgCheckSuccess);
				else
					return;
			}
		}
		super.buttonPressed(buttonId);
	}

	private boolean verify() {
		setErrorMessage(null);
		return true;
	}

	public CubridDatabase getDatabase() {
		return database;
	}

	public void setDatabase(CubridDatabase database) {
		this.database = database;
	}

	private class CheckDbTaskExec extends TaskExecutor {

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
					display.syncExec(new Runnable() {
						public void run() {
							CommonTool.openErrorBox(msg);
						}
					});
					isSuccess = false;
					return isSuccess;
				}
				if (monitor.isCanceled()) {
					isSuccess = false;
					return isSuccess;
				}
			}
			return true;
		}
	}

}
