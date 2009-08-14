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
package com.cubrid.cubridmanager.ui.common.dialog;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.viewers.StructuredSelection;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.jface.viewers.Viewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.RowLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.ServerType;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.common.task.DeleteCMUserTask;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.common.control.UserManagementWizard;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.TableViewerSorter;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.dialog.CMWizardDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * User Management will use this dialog to view user information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class UserManagementDialog extends
		CMTitleAreaDialog {
	private Table userManageTable = null;
	private TableViewer tableViewer = null;
	private CubridServer server = null;
	private Button deleteButton;
	private Button editButton;
	private List<Map<String, Object>> serverUserInfoTableList = new ArrayList<Map<String, Object>>();
	private List<ServerUserInfo> serverUserInfoList = null;

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public UserManagementDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		getHelpSystem().setHelp(parent, CubridManagerHelpContextIDs.userSet);

		Composite parentComp = (Composite) super.createDialogArea(parent);
		Composite composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);

		Label tipLabel = new Label(composite, SWT.LEFT | SWT.WRAP);
		tipLabel.setText(Messages.msgUserManagementList);
		tipLabel.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));
		// create CUBRID Manager user information table
		String[] columnNameArr = new String[] { Messages.tblColumnUserId,
				Messages.tblColumnDbAuth, Messages.tblColumnBrokerAuth,
				Messages.tblColumnMonitorAuth };
		final ServerType serverType = server.getServerInfo().getServerType();
		if (serverType == ServerType.DATABASE) {
			columnNameArr = new String[] { Messages.tblColumnUserId,
					Messages.tblColumnDbAuth, Messages.tblColumnMonitorAuth };
		} else if (serverType == ServerType.BROKER) {
			columnNameArr = new String[] { Messages.tblColumnUserId,
					Messages.tblColumnBrokerAuth, Messages.tblColumnMonitorAuth };
		}
		tableViewer = CommonTool.createCommonTableViewer(composite,
				new UserManagementTableViewerSorter(), columnNameArr,
				CommonTool.createGridData(GridData.FILL_BOTH, 1, 1, -1, 200));
		userManageTable = tableViewer.getTable();
		initialTableModel();
		tableViewer.setInput(serverUserInfoTableList);
		for (int i = 0; i < userManageTable.getColumnCount(); i++) {
			userManageTable.getColumn(i).pack();
		}

		userManageTable.addSelectionListener(new SelectionAdapter() {
			@SuppressWarnings("unchecked")
			public void widgetSelected(SelectionEvent e) {
				if (userManageTable.getSelectionCount() > 0) {
					StructuredSelection selection = (StructuredSelection) tableViewer.getSelection();
					boolean isHasAdmin = false;
					if (selection != null && !selection.isEmpty()) {
						Iterator it = selection.iterator();
						while (it.hasNext()) {
							Map map = (Map) it.next();
							if (map.get("0").equals("admin")) {
								isHasAdmin = true;
								break;
							}
						}
					}
					deleteButton.setEnabled(!isHasAdmin);
				} else {
					deleteButton.setEnabled(false);
				}
				if (userManageTable.getSelectionCount() == 1) {
					editButton.setEnabled(true);
				} else {
					editButton.setEnabled(false);
				}
			}
		});
		// create button
		Composite buttonComp = new Composite(composite, SWT.NONE);
		RowLayout rowLayout = new RowLayout();
		rowLayout.spacing = 5;
		buttonComp.setLayout(rowLayout);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalAlignment = GridData.END;
		buttonComp.setLayoutData(gridData);

		Button addButton = new Button(buttonComp, SWT.PUSH);
		addButton.setText(Messages.btnAdd);
		addButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				UserManagementWizard userManagementWizard = new UserManagementWizard(
						server, null, serverUserInfoList);
				CMWizardDialog dialog = new CMWizardDialog(getShell(),
						userManagementWizard);
				int returnCode = dialog.open();
				if (returnCode == IDialogConstants.OK_ID) {
					ServerUserInfo userInfo = userManagementWizard.getServerUserInfo();
					Map<String, Object> map = new HashMap<String, Object>();
					map.put("0", userInfo.getUserName());
					if (serverType == ServerType.BOTH) {
						map.put("1", userInfo.getDbCreateAuthType().getText());
						map.put("2", userInfo.getCasAuth().getText());
						map.put("3", userInfo.getStatusMonitorAuth().getText());
						map.put("4", userInfo);
					}
					if (serverType == ServerType.DATABASE) {
						map.put("1", userInfo.getDbCreateAuthType().getText());
						map.put("2", userInfo.getStatusMonitorAuth().getText());
						map.put("3", userInfo);
					} else if (serverType == ServerType.BROKER) {
						map.put("1", userInfo.getCasAuth().getText());
						map.put("2", userInfo.getStatusMonitorAuth().getText());
						map.put("3", userInfo);
					}

					serverUserInfoTableList.add(map);
					serverUserInfoList.add(userInfo);
					tableViewer.refresh();
					for (int i = 0; i < userManageTable.getColumnCount(); i++) {
						userManageTable.getColumn(i).pack();
					}
				}
			}
		});

		editButton = new Button(buttonComp, SWT.PUSH);
		editButton.setText(Messages.btnEdit);
		editButton.addSelectionListener(new SelectionAdapter() {
			@SuppressWarnings("unchecked")
			public void widgetSelected(SelectionEvent e) {
				StructuredSelection selection = (StructuredSelection) tableViewer.getSelection();
				ServerUserInfo serverUserInfo = null;
				if (selection != null && !selection.isEmpty()) {
					Map map = (Map) selection.getFirstElement();
					if (serverType == ServerType.BOTH) {
						serverUserInfo = (ServerUserInfo) map.get("4");
					} else {
						serverUserInfo = (ServerUserInfo) map.get("3");
					}
					UserManagementWizard userManagementWizard = new UserManagementWizard(
							server, serverUserInfo, serverUserInfoList);
					CMWizardDialog dialog = new CMWizardDialog(getShell(),
							userManagementWizard);
					int returnCode = dialog.open();
					if (returnCode == IDialogConstants.OK_ID) {
						ServerUserInfo userInfo = userManagementWizard.getServerUserInfo();
						map.put("0", userInfo.getUserName());
						if (serverType == ServerType.BOTH) {
							map.put("1",
									userInfo.getDbCreateAuthType().getText());
							map.put("2", userInfo.getCasAuth().getText());
							map.put("3",
									userInfo.getStatusMonitorAuth().getText());
							map.put("4", userInfo);
						}
						if (serverType == ServerType.DATABASE) {
							map.put("1",
									userInfo.getDbCreateAuthType().getText());
							map.put("2",
									userInfo.getStatusMonitorAuth().getText());
							map.put("3", userInfo);
						} else if (serverType == ServerType.BROKER) {
							map.put("1", userInfo.getCasAuth().getText());
							map.put("2",
									userInfo.getStatusMonitorAuth().getText());
							map.put("3", userInfo);
						}
						tableViewer.refresh();
						for (int i = 0; i < userManageTable.getColumnCount(); i++) {
							userManageTable.getColumn(i).pack();
						}
					}
				}
			}
		});
		editButton.setEnabled(userManageTable.getSelectionCount() == 1);

		deleteButton = new Button(buttonComp, SWT.PUSH);
		deleteButton.setText(Messages.btnDelete);
		deleteButton.addSelectionListener(new SelectionAdapter() {
			@SuppressWarnings("unchecked")
			public void widgetSelected(SelectionEvent e) {
				boolean isDelete = CommonTool.openConfirmBox(getShell(),
						Messages.msgDeleteUserConfirm);
				if (!isDelete) {
					return;
				}
				StructuredSelection selection = (StructuredSelection) tableViewer.getSelection();

				if (selection != null && !selection.isEmpty()) {
					Map[] userInfoMapArr = new Map[selection.size()];
					Iterator it = selection.iterator();
					int i = 0;
					while (it.hasNext()) {
						Map map = (Map) it.next();
						userInfoMapArr[i] = map;
						i++;
					}
					deleteUser(userInfoMapArr);
				}
				tableViewer.refresh();
			}
		});
		deleteButton.setEnabled(userManageTable.getSelectionCount() == 1);

		setTitle(Messages.titleUserManagementDialog);
		setMessage(Messages.msgUserManagementDialog);
		return parentComp;
	}

	/**
	 * 
	 * Initial the table model of user list table
	 * 
	 */
	private void initialTableModel() {
		ServerType serverType = server.getServerInfo().getServerType();
		for (int i = 0; serverUserInfoList != null
				&& i < serverUserInfoList.size(); i++) {
			ServerUserInfo userInfo = serverUserInfoList.get(i);
			Map<String, Object> map = new HashMap<String, Object>();
			map.put("0", userInfo.getUserName());
			if (serverType == ServerType.BOTH) {
				map.put("1", userInfo.getDbCreateAuthType().getText());
				map.put("2", userInfo.getCasAuth().getText());
				map.put("3", userInfo.getStatusMonitorAuth().getText());
				map.put("4", userInfo);
			}
			if (serverType == ServerType.DATABASE) {
				map.put("1", userInfo.getDbCreateAuthType().getText());
				map.put("2", userInfo.getStatusMonitorAuth().getText());
				map.put("3", userInfo);
			} else if (serverType == ServerType.BROKER) {
				map.put("1", userInfo.getCasAuth().getText());
				map.put("2", userInfo.getStatusMonitorAuth().getText());
				map.put("3", userInfo);
			}
			serverUserInfoTableList.add(map);
		}
	}

	/**
	 * 
	 * Execute task and delete user
	 * 
	 * @param userInfoMapArr
	 */
	@SuppressWarnings("unchecked")
	private void deleteUser(final Map[] userInfoMapArr) {
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
					if (task instanceof DeleteCMUserTask) {
						DeleteCMUserTask deleteCMUserTask = (DeleteCMUserTask) task;
						String userId = deleteCMUserTask.getUserId();
						for (int i = 0; i < userInfoMapArr.length; i++) {
							String id = (String) userInfoMapArr[i].get("0");
							if (userId.equals(id)) {
								serverUserInfoTableList.remove(userInfoMapArr[i]);
								ServerType serverType = server.getServerInfo().getServerType();
								if (serverType == ServerType.BOTH) {
									serverUserInfoList.remove(userInfoMapArr[i].get("4"));
								} else {
									serverUserInfoList.remove(userInfoMapArr[i].get("3"));
								}
								break;
							}
						}
					}
				}
				return true;
			}
		};
		for (int i = 0; userInfoMapArr != null && i < userInfoMapArr.length; i++) {
			DeleteCMUserTask deleteTask = new DeleteCMUserTask(
					server.getServerInfo());
			Map map = userInfoMapArr[i];
			deleteTask.setUserId((String) map.get("0"));
			taskExcutor.addTask(deleteTask);
		}
		new ExecTaskWithProgress(taskExcutor).exec(true, true);
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleUserManagementDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, Messages.btnClose, true);
	}

	/**
	 * 
	 * Get CubridServer
	 * 
	 * @return
	 */
	public CubridServer getServer() {
		return server;
	}

	/**
	 * 
	 * Set CubridServer
	 * 
	 * @param server
	 */
	public void setServer(CubridServer server) {
		this.server = server;
	}

	public void setServerUserInfoList(List<ServerUserInfo> serverUserInfoList) {
		this.serverUserInfoList = serverUserInfoList;
	}
}

/**
 * 
 * User management table viewer sorter for sorting user management table content
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
class UserManagementTableViewerSorter extends
		TableViewerSorter {
	@SuppressWarnings("unchecked")
	public int compare(Viewer viewer, Object e1, Object e2) {
		if (!(e1 instanceof Map) || !(e2 instanceof Map)) {
			return 0;
		}
		int rc = 0;
		Map map1 = (Map) e1;
		Map map2 = (Map) e2;
		String userName1 = (String) map1.get("0");
		String userName2 = (String) map2.get("0");
		if (userName1.equals("admin") || userName2.equals("admin")) {
			if (userName1.equals("admin")) {
				rc = -1;
			} else if (userName2.equals("admin")) {
				rc = 1;
			}
			return rc;
		} else {
			Object obj1 = map1.get("" + column);
			Object obj2 = map2.get("" + column);
			if (obj1 instanceof Number && obj2 instanceof Number) {
				Number num1 = (Number) obj1;
				Number num2 = (Number) obj2;
				if (num1.doubleValue() > num2.doubleValue()) {
					rc = 1;
				} else if (num1.doubleValue() < num2.doubleValue()) {
					rc = -1;
				} else {
					rc = 0;
				}
			} else if (obj1 instanceof String && obj2 instanceof String) {
				String str1 = (String) obj1;
				String str2 = (String) obj2;
				rc = str1.compareTo(str2);
			}
		}
		// If descending order, flip the direction
		if (direction == DESCENDING)
			rc = -rc;
		return rc;
	}
}
