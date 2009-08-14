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
import org.eclipse.jface.operation.IRunnableWithProgress;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.common.task.CommonTaskName;
import com.cubrid.cubridmanager.core.common.task.CommonUpdateTask;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * The Dialog of Compacting the Database
 * 
 * @author robin 2009-3-11
 */
public class CompactDatabaseDialog extends
		CMTitleAreaDialog {
	private static final Logger logger = LogUtil.getLogger(CompactDatabaseDialog.class);
	private Text dbName;
	private Composite parentComp;
	private CubridDatabase database = null;
	private boolean isRunning = false;

	/**
	 * constructor
	 * 
	 * @param parentShell
	 */
	public CompactDatabaseDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp, CubridManagerHelpContextIDs.databaseCompact);
		final Composite composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);
		createdbNameGroup(composite);
		createDescriptionGroup(composite);

		setTitle(Messages.titleCompactDbDialog);
		setMessage(Messages.msgCompactDbDialog);

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
		layout.numColumns = 2;
		final Group descGroup = new Group(composite, SWT.NONE);
		descGroup.setLayoutData(new GridData(GridData.FILL_BOTH));
		descGroup.setLayout(layout);
		descGroup.setText(Messages.grpCompactDescInfo);
		final Label descriptionLabel = new Label(descGroup, SWT.LEFT | SWT.WRAP);
		GridData gridData1 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.FILL, false, false);
		descriptionLabel.setText(Messages.lblCompactDescInfo);
		descriptionLabel.setLayoutData(gridData1);
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
		dbnameGroup.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false, 2, 2));
		dbnameGroup.setLayout(layout);

		final Label databaseName = new Label(dbnameGroup, SWT.LEFT | SWT.WRAP);
		databaseName.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		databaseName.setText(Messages.lblCompactDbName);

		dbName = new Text(dbnameGroup, SWT.BORDER);
		dbName.setEnabled(false);
		final GridData gd_dbName = new GridData(SWT.FILL, SWT.FILL, true, false);
		dbName.setLayoutData(gd_dbName);
	}

	/**
	 * 
	 * Init the dialog values
	 * 
	 */
	private void initial() {
		dbName.setText(database.getName());
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		getShell().setSize(400, 350);
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleCompactDbDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);
		getButton(IDialogConstants.OK_ID).setEnabled(true);
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			if (!verify()) {
				return;
			} else {
				if (CommonTool.openConfirmBox(parentComp.getShell(),
						Messages.msgCompactConfirm)) {
					CommonUpdateTask task = new CommonUpdateTask(
							CommonTaskName.COMPACT_DATABASE_TASK_NANE,
							database.getServer().getServerInfo(),
							CommonSendMsg.commonDatabaseSendMsg);
					task.setDbName(database.getName());
					executeTask(IDialogConstants.OK_ID, task, false, getShell());
					if (task.getErrorMsg() == null)
						CommonTool.openInformationBox(parentComp.getShell(),
								Messages.titleSuccess,
								Messages.msgCompactSuccess);
					else
						CommonTool.openErrorBox(parentComp.getShell(),
								Messages.errCompactInfo + task.getErrorMsg());
				} else {
					return;
				}
			}
		}
		super.buttonPressed(buttonId);
	}

	/**
	 * Validate the dialog
	 * 
	 * @return
	 */
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

	/**
	 * Execute the tasks
	 * 
	 * @param buttonId
	 * @param task
	 * @param cancelable
	 * @param shell
	 */
	public void executeTask(final int buttonId, final SocketTask task,
			boolean cancelable, Shell shell) {
		final Display display = shell.getDisplay();
		try {
			new ProgressMonitorDialog(getShell()).run(true, cancelable,
					new IRunnableWithProgress() {
						public void run(final IProgressMonitor monitor) throws InvocationTargetException,
								InterruptedException {
							monitor.beginTask(
									com.cubrid.cubridmanager.ui.spi.Messages.msgRunning,
									IProgressMonitor.UNKNOWN);

							if (monitor.isCanceled()) {
								return;
							}
							isRunning = true;
							Thread thread = new Thread() {
								public void run() {
									if (monitor.isCanceled()) {
										return;
									}
									task.execute();
									if (monitor.isCanceled()) {
										return;
									}
									final String msg = task.getErrorMsg();
									if (msg != null && msg.length() > 0
											&& !monitor.isCanceled()) {
										display.syncExec(new Runnable() {
											public void run() {
												CommonTool.openErrorBox(
														getShell(), msg);
											}
										});
										isRunning = false;
										return;
									}

									isRunning = false;
									//							display.syncExec(new Runnable() {
									//								public void run() {
									//									setReturnCode(buttonId);
									//									close();
									//								}
									//							});
								}
							};
							thread.start();
							while (!monitor.isCanceled() && isRunning) {
								Thread.sleep(1000);
							}
							if (monitor.isCanceled())
								task.cancel();
							monitor.done();
						}
					});
		} catch (InvocationTargetException e) {
			logger.error(e.getMessage(), e);
		} catch (InterruptedException e) {
			logger.error(e.getMessage(), e);
		}
	}
}
