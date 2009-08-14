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
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.dialogs.ProgressMonitorDialog;
import org.eclipse.jface.operation.IRunnableWithProgress;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.database.model.transaction.DbTransactionList;
import com.cubrid.cubridmanager.core.cubrid.database.model.transaction.KillTransactionList;
import com.cubrid.cubridmanager.core.cubrid.database.model.transaction.Transaction;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.KillTranType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.TableViewerSorter;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * Show the transaction info
 * 
 * @author robin 2009-3-11
 */
public class TransactionInfoDialog extends CMTitleAreaDialog {
	private static final Logger logger = LogUtil.getLogger(TransactionInfoDialog.class);

	private Table transactionTable;
	private List<Map<String, String>> transactionListData = new ArrayList<Map<String, String>>();
	private TableViewer transactionTableViewer;
	private Composite parentComp = null;
	private CubridDatabase database = null;
	private Label objectIdLabel;
	public static int KILL_TRANSACTION_ID = 100;
	public static int REFRESH_ID = 101;
	private boolean isRunning = false;

	private DbTransactionList dbTransactionList;

	/**
	 * 
	 * @param parentShell
	 */
	public TransactionInfoDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp, CubridManagerHelpContextIDs.databaseTransaction);
		Composite composite = new Composite(parentComp, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));

		createTitleInfo(composite);
		createTransactionGroup(composite);

		setTitle(Messages.titleTransactionDialog);
		setMessage(Messages.msgTransactionDialog);
		initial();
		return parentComp;
	}

	/**
	 * 
	 * Create Title Info
	 * 
	 * @param composite
	 */
	private void createTitleInfo(Composite composite) {
		objectIdLabel = new Label(composite, SWT.NONE);
	}

	private void createTransactionGroup(Composite composite) {
		final Group transactionGroup = new Group(composite, SWT.NONE);
		transactionGroup.setText(Messages.grpTransaction);
		transactionGroup.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout(1, true);
		transactionGroup.setLayout(layout);
		final String[] columnNameArr = new String[]
			{
			        Messages.tblColTranInfoTranIndex,
			        Messages.tblColTranInfoUserName,
			        Messages.tblColTranInfoHost,
			        Messages.tblColTranInfoProcessId,
			        Messages.tblColTranInfoProgramName

			};
		transactionTableViewer = CommonTool.createCommonTableViewer(transactionGroup, new TableViewerSorter(),
		        columnNameArr, CommonTool.createGridData(GridData.FILL_BOTH, 3, 1, -1, 200));

		transactionTableViewer.setInput(transactionListData);
		transactionTable = transactionTableViewer.getTable();

		Menu menu = new Menu(getShell(), SWT.POP_UP);
		final MenuItem itemCopy = new MenuItem(menu, SWT.PUSH);
		itemCopy.setText(Messages.menuKillTransaction);
		itemCopy.addSelectionListener(new SelectionListener() {

			public void widgetDefaultSelected(SelectionEvent e) {
			}

			public void widgetSelected(SelectionEvent e) {
				int index = transactionTable.getSelectionIndex();
				transactionTable.getItem(index).getText(3);
				if (CommonTool.openConfirmBox(parentComp.getShell(), Messages.bind(Messages.msgKillOnlyConfirm,
				        transactionTable.getItem(index).getText(3)))) {
					killTransaction(KillTranType.T, transactionTable.getItem(index).getText(0));
					initial();
				} else
					return;
			}
		});
		transactionTable.setMenu(menu);

	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		// getShell().setSize(550, 530);
		getShell().setText(Messages.titleTransactionDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, REFRESH_ID, com.cubrid.cubridmanager.ui.common.Messages.btnRefresh, true);

		createButton(parent, KILL_TRANSACTION_ID, Messages.killTransactionName, false);
		createButton(parent, IDialogConstants.CANCEL_ID, com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == KILL_TRANSACTION_ID) {
			int i = transactionTable.getSelectionIndex();
			String pid = transactionTable.getItem(i).getText(3);
			if (i >= 0 && dbTransactionList != null && dbTransactionList.getTransationInfo() != null
			        && dbTransactionList.getTransationInfo().getTransactionList() != null
			        && dbTransactionList.getTransationInfo().getTransactionList().size() > i) {
				KillTransactionDialog dlg = new KillTransactionDialog(parentComp.getShell());
				Transaction bean = null;
				for (Transaction t : dbTransactionList.getTransationInfo().getTransactionList()) {
					if (pid.equals(t.getPid())) {
						bean = t;
					}
				}
				dlg.setTransationInfo(bean);
				dlg.setDatabase(database);
				if (dlg.open() == IDialogConstants.CANCEL_ID) {
					return;
				}
				// if (dlg.open() != IDialogConstants.CANCEL_ID) {
				this.dbTransactionList.getTransationInfo().setTransactionList(
				        dlg.getKillTransactionList().getTransationInfo().getTransactionList());
				initial();
				// }
			}

		} else if (buttonId == REFRESH_ID) {
			loadData(getShell());
			initial();
			return;
		}
		super.buttonPressed(buttonId);
	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {

		while (transactionListData.size() > 0) {
			transactionListData.remove(0);
		}
		if (this.dbTransactionList != null && dbTransactionList.getTransationInfo() != null
		        && dbTransactionList.getTransationInfo().getTransactionList() != null) {
			for (Transaction bean : dbTransactionList.getTransationInfo().getTransactionList()) {
				Map<String, String> map = new HashMap<String, String>();
				map.put("0", bean.getTranindex());
				map.put("1", bean.getUser());
				map.put("2", bean.getHost());
				map.put("3", bean.getPid());
				map.put("4", bean.getProgram());
				transactionListData.add(map);
			}

		}
		transactionTableViewer.refresh();
		for (int i = 0; i < transactionTable.getColumnCount(); i++) {
			transactionTable.getColumn(i).pack();
		}
		if (database != null)
			objectIdLabel.setText(Messages.lblActiveTransaction + database.getName());

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

	/**
	 * load the init data from the server
	 * 
	 * @return
	 */
	public boolean loadData(Shell shell) {
		CommonQueryTask<DbTransactionList> task = new CommonQueryTask<DbTransactionList>(database.getServer()
		        .getServerInfo(), CommonSendMsg.commonDatabaseSendMsg, new DbTransactionList());
		task.setDbName(database.getName());
		execTask(-1, new SocketTask[]
			{
				task
			}, true, shell);
		if (task.getErrorMsg() != null)
			return false;
		setDbTransactionList(task.getResultModel());
		return true;

	}

	private void killTransaction(KillTranType type, String Parameter) {

		CommonQueryTask<KillTransactionList> task = new CommonQueryTask<KillTransactionList>(database.getServer()
		        .getServerInfo(), CommonSendMsg.killTransactionMSGItems, new KillTransactionList());
		task.setDbName(database.getName());
		task.setKillTranType(type);
		task.setKillTranParameter(Parameter);
		execTask(-1, new SocketTask[]
			{
				task
			}, true, getShell());
		if (task.getErrorMsg() != null)
			return;
		CommonTool.openInformationBox(parentComp.getShell(), Messages.titleSuccess, Messages.msgKillSuccess);
		KillTransactionList killTransactionList = task.getResultModel();
		DbTransactionList dbDbTransactionList = new DbTransactionList();
		dbDbTransactionList.setTransationInfo(killTransactionList.getTransationInfo());
		setDbTransactionList(dbDbTransactionList);

	}

	public void execTask(final int buttonId, final SocketTask[] tasks, boolean cancelable, Shell shell) {
		final Display display = shell.getDisplay();
		isRunning = false;
		try {
			new ProgressMonitorDialog(getShell()).run(true, cancelable, new IRunnableWithProgress() {
				public void run(final IProgressMonitor monitor) throws InvocationTargetException, InterruptedException {
					monitor.beginTask(com.cubrid.cubridmanager.ui.spi.Messages.msgRunning, IProgressMonitor.UNKNOWN);

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
							if (msg != null && msg.length() > 0 && !monitor.isCanceled()) {
								display.syncExec(new Runnable() {
									public void run() {
										CommonTool.openErrorBox(getShell(), msg);
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

	public DbTransactionList getDbTransactionList() {
		return dbTransactionList;
	}

	public void setDbTransactionList(DbTransactionList dbTransactionList) {
		this.dbTransactionList = dbTransactionList;
	}

}
