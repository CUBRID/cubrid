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
import org.eclipse.jface.resource.JFaceResources;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
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

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.database.model.transaction.KillTransactionList;
import com.cubrid.cubridmanager.core.cubrid.database.model.transaction.Transaction;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.KillTranType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * Lock information of detailed
 * 
 * @author robin 2009-3-11
 */
public class KillTransactionDialog extends
		CMTitleAreaDialog {

	private static final Logger logger = LogUtil.getLogger(TransactionInfoDialog.class);
	private Composite parentComp = null;
	private CubridDatabase database = null;
	private Transaction transationInfo;
	private boolean isRunning = false;
	private KillTransactionList killTransactionList;
	private Composite composite;
	public static int KILL_ONE_TRANSACTION_ID = 2001;
	public static int KILL_USER_TRANSACTION_ID = 2002;
	public static int KILL_CLIENT_TRANSACTION_ID = 2003;
	public static int KILL_PROGRAM_TRANSACTION_ID = 2004;
	private org.eclipse.swt.widgets.List killCombo;

	/**
	 * 
	 * @param parentShell
	 */
	public KillTransactionDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp, CubridManagerHelpContextIDs.databaseTransaction);
		composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		layout.numColumns = 2;
		composite.setLayout(layout);

		final Group tranGroup = new Group(composite, SWT.NONE);
		layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		layout.numColumns = 4;
		final GridData gd_dbnameGroup = new GridData(GridData.FILL_BOTH);
		gd_dbnameGroup.horizontalSpan = 2;
		tranGroup.setLayoutData(gd_dbnameGroup);
		tranGroup.setLayout(layout);
		tranGroup.setText(Messages.grpTransactionInfo);
		if (true) {
			final Label parameterNameLabel = new Label(tranGroup, SWT.LEFT
					| SWT.WRAP);
			parameterNameLabel.setLayoutData(CommonTool.createGridData(1, 1,
					-1, -1));
			parameterNameLabel.setText(Messages.lblTransactionUserName);
			final Text l2 = new Text(tranGroup, SWT.WRAP | SWT.BORDER);
			l2.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
			l2.setText(transationInfo.getUser());
			l2.setEnabled(false);
		}

		if (true) {
			final Label parameterNameLabel = new Label(tranGroup, SWT.LEFT
					| SWT.WRAP);
			parameterNameLabel.setLayoutData(CommonTool.createGridData(1, 1,
					-1, -1));
			parameterNameLabel.setText(Messages.lblTransactionHostName);
			final Text l2 = new Text(tranGroup, SWT.WRAP | SWT.BORDER);
			l2.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
			l2.setText(transationInfo.getHost());
			l2.setEnabled(false);
		}
		if (true) {
			final Label parameterNameLabel = new Label(tranGroup, SWT.LEFT
					| SWT.WRAP);
			parameterNameLabel.setLayoutData(CommonTool.createGridData(1, 1,
					-1, -1));
			parameterNameLabel.setText(Messages.lblTransactionProcessId);
			final Text l2 = new Text(tranGroup, SWT.WRAP | SWT.BORDER);
			l2.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
			l2.setText(transationInfo.getPid());
			l2.setEnabled(false);
		}
		if (true) {
			final Label parameterNameLabel = new Label(tranGroup, SWT.LEFT
					| SWT.WRAP);
			parameterNameLabel.setLayoutData(CommonTool.createGridData(1, 1,
					-1, -1));
			parameterNameLabel.setText(Messages.lblTransactionProgramName);
			final Text l2 = new Text(tranGroup, SWT.WRAP | SWT.BORDER);
			l2.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
			l2.setText(transationInfo.getProgram());
			l2.setEnabled(false);
		}
		//
		// final Composite killGroup = new Composite(composite, SWT.NONE);
		// layout = new GridLayout();
		// layout.marginWidth = 10;
		// layout.marginHeight = 10;
		// layout.numColumns = 2;
		// final GridData gd_killGroup = new GridData(GridData.FILL_BOTH);
		// killGroup.setLayoutData(gd_killGroup);
		//		
		final Label label = new Label(composite, SWT.LEFT | SWT.WRAP);
		label.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		label.setText(Messages.lblTransactionKillType);

		killCombo = new org.eclipse.swt.widgets.List(composite, SWT.READ_ONLY
				| SWT.BORDER);

		killCombo.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, false));
		//		killCombo.
		killCombo.add(Messages.itemKillOnly);
		killCombo.add(Messages.itemKillSameName);
		killCombo.add(Messages.itemKillSameHost);
		killCombo.add(Messages.itemKillSameProgram);
		killCombo.select(0);
		initial();
		setTitle(Messages.titleKillTransactionDialog);
		setMessage(Messages.msgKillTransactionDialog);

		return parentComp;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleKillTransactionDialog);
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
			int index = killCombo.getSelectionIndex();
			switch (index) {
			case 0:
				if (CommonTool.openConfirmBox(parentComp.getShell(),
						Messages.bind(Messages.msgKillOnlyConfirm,
								transationInfo.getPid()))) {
					killTransaction(KillTranType.T,
							transationInfo.getTranindex());
					setReturnCode(KILL_ONE_TRANSACTION_ID);
					close();
				} else
					return;
				break;
			case 1:
				if (CommonTool.openConfirmBox(parentComp.getShell(),
						Messages.bind(Messages.msgKillSameUserConfirm,
								transationInfo.getUser()))) {

					killTransaction(KillTranType.U, transationInfo.getUser());
					setReturnCode(KILL_USER_TRANSACTION_ID);
					close();
				} else
					return;
				break;
			case 2:
				if (CommonTool.openConfirmBox(parentComp.getShell(),
						Messages.bind(Messages.msgKillSameHostConfirm,
								transationInfo.getHost()))) {
					killTransaction(KillTranType.H, transationInfo.getHost());
					setReturnCode(KILL_CLIENT_TRANSACTION_ID);
					close();
				} else
					return;
				break;
			case 3:
				if (CommonTool.openConfirmBox(parentComp.getShell(),
						Messages.bind(Messages.msgKillSameHostConfirm,
								transationInfo.getProgram()))) {
					killTransaction(KillTranType.PG,
							transationInfo.getProgram());
					setReturnCode(KILL_PROGRAM_TRANSACTION_ID);
					close();
				} else
					return;
				break;
			default:
				break;
			}
		}

		super.buttonPressed(buttonId);
	}

	private void killTransaction(KillTranType type, String Parameter) {

		CommonQueryTask<KillTransactionList> task = new CommonQueryTask<KillTransactionList>(
				database.getServer().getServerInfo(),
				CommonSendMsg.killTransactionMSGItems,
				new KillTransactionList());
		task.setDbName(database.getName());
		task.setKillTranType(type);
		task.setKillTranParameter(Parameter);
		connect(-1, new SocketTask[] { task }, true, getShell());
		if (task.getErrorMsg() != null)
			return;
		CommonTool.openInformationBox(parentComp.getShell(),
				Messages.titleSuccess, Messages.msgKillSuccess);
		killTransactionList = task.getResultModel();

	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {

	}

	/**
	 * 
	 * Check the data validation
	 * 
	 * @return
	 */
	public boolean valid() {
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

	public Transaction getTransationInfo() {
		return transationInfo;
	}

	public void setTransationInfo(Transaction transationInfo) {
		this.transationInfo = transationInfo;
	}

	public void connect(final int buttonId, final SocketTask[] tasks,
			boolean cancelable, Shell shell) {
		final Display display = shell.getDisplay();
		isRunning = false;
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
									while (!monitor.isCanceled() && isRunning) {
										try {
											sleep(1);
										} catch (InterruptedException e) {
										}
									}
									if (monitor.isCanceled()) {
										for (SocketTask t : tasks) {
											if (t != null)
												t.cancel();
										}

									}
								}
							};
							thread.start();
							if (monitor.isCanceled()) {
								isRunning = false;
								return;
							}
							for (SocketTask task : tasks) {
								if (task != null) {
									task.execute();
									final String msg = task.getErrorMsg();
									if (monitor.isCanceled()) {
										isRunning = false;
										return;
									}
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
								}
								if (monitor.isCanceled()) {
									isRunning = false;
									return;
								}
							}
							if (monitor.isCanceled()) {
								isRunning = false;
								return;
							}
							if (!monitor.isCanceled()) {
								display.syncExec(new Runnable() {
									public void run() {
										if (buttonId > 0) {
											setReturnCode(buttonId);
											close();
										}
									}
								});
							}
							isRunning = false;
							monitor.done();
						}
					});
		} catch (InvocationTargetException e) {
			logger.error(e.getMessage(), e);
		} catch (InterruptedException e) {
			logger.error(e.getMessage(), e);
		}
	}

	public KillTransactionList getKillTransactionList() {
		return killTransactionList;
	}

	public void setKillTransactionList(KillTransactionList killTransactionList) {
		this.killTransactionList = killTransactionList;
	}

	protected Button createButtonSelf(Composite parent, int id, String label,
			boolean defaultButton) {
		// increment the number of columns in the button bar

		Button button = new Button(parent, SWT.PUSH);
		button.setText(label);
		button.setFont(JFaceResources.getDialogFont());
		button.setData(Integer.valueOf(id));
		button.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent event) {
				buttonPressed(((Integer) event.widget.getData()).intValue());
			}
		});
		if (defaultButton) {
			Shell shell = parent.getShell();
			if (shell != null) {
				shell.setDefaultButton(button);
			}
		}
		// buttons.put(new Integer(id), button);
		setButtonLayoutData(button);
		return button;
	}
}
