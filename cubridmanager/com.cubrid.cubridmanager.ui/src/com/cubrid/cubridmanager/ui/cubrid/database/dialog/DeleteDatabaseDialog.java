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
import java.text.NumberFormat;
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
import org.eclipse.jface.viewers.Viewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.common.task.CommonTaskName;
import com.cubrid.cubridmanager.core.common.task.CommonUpdateTask;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfoList;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.YesNoType;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.TableViewerSorter;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * Delete database
 * 
 * @author robin 2009-3-16
 */
public class DeleteDatabaseDialog extends
		CMTitleAreaDialog {
	private static final Logger logger = LogUtil.getLogger(DeleteDatabaseDialog.class);
	private Text dbNameText;
	private Table volumeTable;
	private Composite parentComp;
	private CubridDatabase database = null;
	private DbSpaceInfoList dbSpaceInfo = null;
	public static int DELETE_ID = 103;
	private boolean isRunning = false;
	private Button deleteBackupVolumesButton;
	public static int CONNECT_ID = 0;
	private List<Map<String, Object>> volumeTableListData;
	private TableViewer tableViewer = null;

	public DeleteDatabaseDialog(Shell parentShell) {
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
		Composite composite = new Composite(parentComp, SWT.NONE);

		GridLayout compLayout = new GridLayout();
		compLayout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		compLayout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		compLayout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		compLayout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(compLayout);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		final Group databaseGroup = new Group(composite, SWT.NONE);
		databaseGroup.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		GridLayout layout = new GridLayout();
		layout.numColumns = 3;
		databaseGroup.setLayout(layout);
		final Label databaseNameLabel = new Label(databaseGroup, SWT.NONE);
		databaseNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		databaseNameLabel.setText(Messages.lblDeleteDbName);

		dbNameText = new Text(databaseGroup, SWT.LEFT | SWT.BORDER);
		dbNameText.setEnabled(false);
		final GridData gd_dbNameText = new GridData(GridData.FILL_HORIZONTAL);
		gd_dbNameText.horizontalSpan = 2;
		dbNameText.setLayoutData(gd_dbNameText);

		createDirectoryList(composite);

		deleteBackupVolumesButton = new Button(composite, SWT.CHECK);
		deleteBackupVolumesButton.setText(Messages.btnDelBakup);

		setTitle(Messages.titleDeleteDbDialog);
		setMessage(Messages.msgDeleteDbDialog);

		initial();
		return parentComp;
	}

	/**
	 * create the directory list
	 * 
	 * @param composite
	 */
	private void createDirectoryList(Composite composite) {

		final CLabel volumeInformationOfLabel = new CLabel(composite, SWT.NONE);
		volumeInformationOfLabel.setText(Messages.lblVolumeInfomation);

		final String[] columnNameArr = new String[] {
				Messages.tblColDelDbVolName, Messages.tblColDelDbVolPath,
				Messages.tblColDelDbChangeDate, Messages.tblColDelDbVolType,
				Messages.tblColDelDbTotalSize, Messages.tblColDelDbRemainSize,
				Messages.tblColDelDbVolSize };
		tableViewer = CommonTool.createCommonTableViewer(composite,
				new TableViewerSorter() {
					@SuppressWarnings("unchecked")
					public int compare(Viewer viewer, Object e1, Object e2) {
						if (!(e1 instanceof Map) || !(e2 instanceof Map)) {
							return 0;
						}
						int rc = 0;
						Map<String, String> map1 = (Map<String, String>) e1;
						Map<String, String> map2 = (Map<String, String>) e2;
						if (column == 5 || column == 4) {
							rc = CommonTool.str2Int((String) map1.get(""
									+ column))
									- CommonTool.str2Int((String) map2.get(""
											+ column));
						} else if (column == 6) {
							double r = CommonTool.str2Double((String) map1.get(""
									+ column))
									- CommonTool.str2Double((String) map2.get(""
											+ column));
							if (r == 0)
								rc = 0;
							else
								rc = r > 0 ? 1 : -1;

						} else {
							String str1 = (String) map1.get("" + column);
							String str2 = (String) map2.get("" + column);
							rc = str1.compareTo(str2);
						}
						// If descending order, flip the direction
						if (direction == DESCENDING)
							rc = -rc;
						return rc;
					}
				}, columnNameArr, CommonTool.createGridData(GridData.FILL_BOTH,
						1, 1, -1, 200));
		volumeTable = tableViewer.getTable();

	}

	/**
	 * 
	 * Init the value of dialog field
	 * 
	 */
	private void initial() {

		dbNameText.setText(database.getName());

		volumeTableListData = new ArrayList<Map<String, Object>>();

		for (DbSpaceInfo bean : dbSpaceInfo.getSpaceinfo()) {
			if (!bean.getType().equals("GENERIC")
					&& !bean.getType().equals("DATA")
					&& !bean.getType().equals("TEMP")
					&& !bean.getType().equals("INDEX")
					&& !bean.getType().equals("Active_log")) {
				continue;
			}
			Map<String, Object> map = new HashMap<String, Object>();
			map.put("0", bean.getSpacename());
			map.put("1", bean.getLocation());
			map.put("2", bean.getDate());
			map.put("3", bean.getType());
			map.put("4", bean.getTotalpage() == 0 ? ""
					: (bean.getTotalpage() + ""));
			map.put("5", bean.getFreepage() == 0 ? ""
					: (bean.getFreepage() + ""));
			// TODO
			double mb = dbSpaceInfo.getPagesize() * bean.getTotalpage()
					/ 1048576.0;
			NumberFormat nf = NumberFormat.getInstance();
			nf.setMaximumFractionDigits(2);
			nf.setGroupingUsed(false);

			String volsize = nf.format(mb) + "";
			map.put("6", volsize);
			volumeTableListData.add(map);
		}

		tableViewer.setInput(volumeTableListData);
		for (int i = 0; i < volumeTable.getColumnCount(); i++) {
			volumeTable.getColumn(i).pack();
		}
	}

	/**
	 * 
	 * Delete the database
	 * 
	 * @return
	 */
	private boolean deleteDatabase() {
		// DeleteDbTask task = new
		// DeleteDbTask(database.getServer().getServerInfo());
		// task.setDbname(database.getName());
		CommonUpdateTask task = new CommonUpdateTask(
				CommonTaskName.DELETE_DATABASE_TASK_NAME,
				database.getServer().getServerInfo(),
				CommonSendMsg.deletedbSendMsg);
		task.setDbName(database.getName());

		if (deleteBackupVolumesButton.getSelection())
			task.setDelbackup(YesNoType.Y);
		else
			task.setDelbackup(YesNoType.N);
		execTask(DELETE_ID, task, false, getShell());
		if (task.getErrorMsg() != null && !task.getErrorMsg().equals("")) {
			return false;
		} else {
			return true;
		}
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		getShell().setSize(550, 450);
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleDeleteDbDialog);

	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, DELETE_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == DELETE_ID) {
			if (!verify()) {
				return;
			} else {
				DeleteDatabaseConfirmDialog dialog = new DeleteDatabaseConfirmDialog(
						getShell());
				dialog.setDatabase(database);
				if (dialog.open() == IDialogConstants.OK_ID)
					deleteDatabase();
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

	public DbSpaceInfoList getDbSpaceInfo() {
		return dbSpaceInfo;
	}

	public void setDbSpaceInfo(DbSpaceInfoList dbSpaceInfo) {
		this.dbSpaceInfo = dbSpaceInfo;
	}

	/**
	 * Execute task
	 * 
	 * @param buttonId
	 * @param task
	 * @param cancelable
	 * @param shell
	 */
	public void execTask(final int buttonId, final SocketTask task,
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

										if (task != null)
											task.cancel();

									}
								}
							};
							thread.start();
							if (monitor.isCanceled()) {
								isRunning = false;
								return;
							}
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
											CommonTool.openErrorBox(getShell(),
													msg);
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

}
