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
package com.cubrid.cubridmanager.ui.cubrid.user.dialog;

import java.lang.reflect.InvocationTargetException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.dialogs.ProgressMonitorDialog;
import org.eclipse.jface.operation.IRunnableWithProgress;
import org.eclipse.jface.viewers.CellEditor;
import org.eclipse.jface.viewers.CheckboxCellEditor;
import org.eclipse.jface.viewers.ICellModifier;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.FocusEvent;
import org.eclipse.swt.events.KeyEvent;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Item;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.UserSendObj;
import com.cubrid.cubridmanager.core.cubrid.table.model.ClassAuthorizations;
import com.cubrid.cubridmanager.core.cubrid.table.model.ClassInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetAllClassListTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ClassType;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfoList;
import com.cubrid.cubridmanager.core.cubrid.user.task.UpdateAddUserTask;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.cubrid.user.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.TableContentProvider;
import com.cubrid.cubridmanager.ui.spi.TableLabelProvider;
import com.cubrid.cubridmanager.ui.spi.TableViewerSorter;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTrayDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * Show the edit user dialog
 * 
 * @author robin 2009-3-18
 */
public class EditUserDialog extends
		CMTrayDialog {
	private static final Logger logger = LogUtil.getLogger(EditUserDialog.class);
	private Table classTable;
	private List<Map<String, String>> classListData = new ArrayList<Map<String, String>>();
	private TableViewer classTableViewer;
	private List<Map<String, String>> memberListData = new ArrayList<Map<String, String>>();
	private TableViewer memberTableViewer;
	private Table allUserTable;
	private List<Map<String, String>> allUserListData = new ArrayList<Map<String, String>>();
	private TableViewer allUserTableViewer;

	private List<Map<String, String>> groupListData = new ArrayList<Map<String, String>>();
	private TableViewer groupTableViewer;

	private Table authTable;
	private List<Map<String, Object>> authListData = new ArrayList<Map<String, Object>>();
	private TableViewer authTableViewer;
	private Map<String, String> classGrantMap;
	private Table userGroupTable;

	private Button buttonRemoveGroup;
	private Button passwordButton;
	private Button revokeButton;
	private Composite cmpAllUsers;
	private Text pwdCfmText;
	private Text pwdText;
	private Text oldPwdText;
	private Text userNameText;

	private CubridDatabase database = null;
	private TabFolder tabFolder;
	private Button grantButton = null;
	private Composite parentComp;
	private Button buttonAddGroup;

	private GridLayout layout;

	private Composite cmpRightAreaGroup;
	private boolean isRunning = false;
	private List<ClassInfo> allClassInfoList;
	private DbUserInfoList userListInfo;
	private String userName;
	private DbUserInfo userInfo = null;
	// private boolean isPwdChanged = false;
	private String oldLoginPassword = "";
	private EditUserTaskExec taskExecutor = new EditUserTaskExec();
	private boolean newFlag = false;
	private boolean changePasswordFlag = false;
	public final static String DB_DEFAULT_USERNAME = "public";
	public final static String DB_DBA_USERNAME = "dba";
	boolean dbaLoginFlag = false;
	private Map<String, String> partitionClassMap;

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public EditUserDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp, CubridManagerHelpContextIDs.databaseUser);
		tabFolder = new TabFolder(parentComp, SWT.NONE);
		tabFolder.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		tabFolder.setLayout(layout);

		TabItem item = new TabItem(tabFolder, SWT.NONE);
		item.setText(Messages.tabItemGeneral);
		Composite lockComposite = createUserComposit();
		item.setControl(lockComposite);

		if (!DB_DBA_USERNAME.equalsIgnoreCase(userName)) {
			item = new TabItem(tabFolder, SWT.NONE);
			item.setText(Messages.tabItemAuthoration);
			Composite composite = createAuthComposit();
			item.setControl(composite);
		}
		// item = new TabItem(tabFolder, SWT.NONE);
		// item.setText("Member list");
		// Composite composite = createMemberComposit();
		// item.setControl(composite);
		initial();
		return parentComp;
	}

	/**
	 * Create user dialog
	 * 
	 * @return
	 */
	private Composite createUserComposit() {
		final Composite composite = new Composite(tabFolder, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		composite.setLayout(layout);
		createUserPwdGroup(composite);

		final Group userMemberGroup = new Group(composite, SWT.NONE);

		userMemberGroup.setText(Messages.grpUserMemberInfo);
		layout = new GridLayout();
		layout.numColumns = 2;
		layout.makeColumnsEqualWidth = true;
		layout.marginWidth = 10;
		layout.horizontalSpacing = 10;
		userMemberGroup.setLayout(layout);
		GridData gd_userMemberGroup = new org.eclipse.swt.layout.GridData(
				SWT.FILL, SWT.FILL, true, true);
		userMemberGroup.setLayoutData(gd_userMemberGroup);

		createAllUserComposit(userMemberGroup);
		createGroupListComposit(userMemberGroup);

		return composite;
	}

	private void createUserPwdGroup(Composite composite) {
		final Group userNameGroup = new Group(composite, SWT.NONE);
		final GridData gd_userPasswordGroup = new GridData(SWT.FILL, SWT.NONE,
				true, true);
		userNameGroup.setLayoutData(gd_userPasswordGroup);
		layout = new GridLayout();
		layout.numColumns = 2;
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		userNameGroup.setLayout(layout);

		final Label userNameLabel = new Label(userNameGroup, SWT.NONE);
		final GridData gd_userNameLabel = new GridData(SWT.FILL, SWT.FILL,
				false, false);

		userNameLabel.setLayoutData(gd_userNameLabel);
		userNameLabel.setText(Messages.lblUserName);

		userNameText = new Text(userNameGroup, SWT.BORDER);
		userNameText.setEnabled(false);
		final GridData gd_userNameText = new GridData(SWT.FILL, SWT.FILL, true,
				false);
		userNameText.setLayoutData(gd_userNameText);
		userNameText.addKeyListener(new org.eclipse.swt.events.KeyAdapter() {
			public void keyPressed(KeyEvent e) {
			}

			public void keyReleased(KeyEvent e) {
				String userName = userNameText.getText();
				if (null == userName || "".equals(userName)
						|| userName.length() <= 0) {
					getButton(IDialogConstants.OK_ID).setEnabled(false);
					return;
				}
				getButton(IDialogConstants.OK_ID).setEnabled(true);
			}
		});

		final Group userPasswordGroup = new Group(userNameGroup, SWT.NONE);
		final GridData gd_passwordGroup = new GridData(SWT.FILL, SWT.FILL,
				false, false);
		gd_passwordGroup.horizontalSpan = 2;
		userPasswordGroup.setLayoutData(gd_passwordGroup);
		layout = new GridLayout();
		layout.numColumns = 2;
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		userPasswordGroup.setLayout(layout);
		userPasswordGroup.setText(Messages.grpPasswordSetting);
		if (!newFlag) {
			passwordButton = new Button(userPasswordGroup, SWT.CHECK);
			passwordButton.setText(Messages.btnPasswordChange);
			passwordButton.setLayoutData(new GridData(SWT.LEFT, SWT.CENTER,
					false, false, 2, 1));
			passwordButton.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(final SelectionEvent e) {
					if (passwordButton.getSelection()) {
						if (!database.getDatabaseInfo().getAuthLoginedDbUserInfo().getName().equalsIgnoreCase(
								DB_DBA_USERNAME)||(database.getDatabaseInfo().getAuthLoginedDbUserInfo().getName().equalsIgnoreCase(
										DB_DBA_USERNAME)&&DB_DBA_USERNAME.equalsIgnoreCase(userName)))
							oldPwdText.setEnabled(true);
						pwdText.setEnabled(true);
						pwdCfmText.setEnabled(true);
						changePasswordFlag = true;

					} else {
						oldPwdText.setEnabled(false);
						pwdText.setEnabled(false);
						pwdCfmText.setEnabled(false);
						changePasswordFlag = false;
					}
				}

			});
			final Label oldPasswordLabel = new Label(userPasswordGroup,
					SWT.NONE);
			oldPasswordLabel.setText(Messages.lblOldPassword);

			oldPwdText = new Text(userPasswordGroup, SWT.BORDER | SWT.PASSWORD);
			final GridData gd_OldPwdText = new GridData(SWT.FILL, SWT.CENTER,
					true, false);
			oldPwdText.setLayoutData(gd_OldPwdText);
			oldPwdText.setTextLimit(ValidateUtil.MAX_NAME_LENGTH);

			final Label passwordLabel = new Label(userPasswordGroup, SWT.NONE);
			passwordLabel.setText(Messages.lblNewPassword);

			pwdText = new Text(userPasswordGroup, SWT.BORDER | SWT.PASSWORD);
			pwdText.setTextLimit(ValidateUtil.MAX_NAME_LENGTH);
			final GridData gd_pwdText = new GridData(SWT.FILL, SWT.CENTER,
					true, false);
			pwdText.setLayoutData(gd_pwdText);
			final Label newPasswordConfirmLabel = new Label(userPasswordGroup,
					SWT.NONE);
			newPasswordConfirmLabel.setText(Messages.lblNewPasswordConf);

			pwdCfmText = new Text(userPasswordGroup, SWT.BORDER | SWT.PASSWORD);
			final GridData gd_pwdCfmText = new GridData(SWT.FILL, SWT.CENTER,
					true, false);
			pwdCfmText.setLayoutData(gd_pwdCfmText);
			pwdCfmText.setTextLimit(ValidateUtil.MAX_NAME_LENGTH);
			oldPwdText.setEnabled(false);

			pwdText.setEnabled(false);

			pwdCfmText.setEnabled(false);
		} else {

			final Label passwordLabel = new Label(userPasswordGroup, SWT.NONE);
			passwordLabel.setText(Messages.lblPassword);

			pwdText = new Text(userPasswordGroup, SWT.BORDER | SWT.PASSWORD);
			final GridData gd_pwdText = new GridData(SWT.FILL, SWT.CENTER,
					true, false);
			pwdText.setLayoutData(gd_pwdText);
			final Label newPasswordConfirmLabel = new Label(userPasswordGroup,
					SWT.NONE);
			newPasswordConfirmLabel.setText(Messages.lblPasswordConf);

			pwdCfmText = new Text(userPasswordGroup, SWT.BORDER | SWT.PASSWORD);
			final GridData gd_pwdCfmText = new GridData(SWT.FILL, SWT.CENTER,
					true, false);
			pwdCfmText.setLayoutData(gd_pwdCfmText);

		}
	}

	/**
	 * Create all user composit
	 * 
	 * @param group
	 */
	private void createAllUserComposit(Group group) {
		cmpAllUsers = new Composite(group, SWT.NONE);
		final GridData gd_cmpAllUsers = new GridData(SWT.FILL, SWT.FILL, true,
				true, 1, 2);
		cmpAllUsers.setLayoutData(gd_cmpAllUsers);
		cmpAllUsers.setLayout(new GridLayout());

		final Label allUsersLabel = new Label(cmpAllUsers, SWT.NONE);
		allUsersLabel.setText(Messages.lblAllUser);

		final String[] userColumnNameArr = new String[] { "col1" };
		allUserTableViewer = CommonTool.createCommonTableViewer(cmpAllUsers,
				null, userColumnNameArr, CommonTool.createGridData(
						GridData.FILL_BOTH, 1, 4, -1, 200));
		allUserTable = allUserTableViewer.getTable();

		allUserTableViewer.setInput(allUserListData);
		allUserTable.setLinesVisible(false);
		allUserTable.setHeaderVisible(false);
		allUserTable.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setBtnEnableDisable();
			}
		});

	}

	private void createGroupListComposit(Group group) {

		cmpRightAreaGroup = new Composite(group, SWT.NONE);
		final GridData gd_cmpAllUsers = new GridData(SWT.FILL, SWT.FILL, true,
				true);
		cmpRightAreaGroup.setLayoutData(gd_cmpAllUsers);
		GridLayout gridLayout = new GridLayout();
		gridLayout.horizontalSpacing = 10;
		gridLayout.numColumns = 2;
		cmpRightAreaGroup.setLayout(gridLayout);

		new Label(cmpRightAreaGroup, SWT.NONE);

		final Label groupListLabel = new Label(cmpRightAreaGroup, SWT.NONE);
		groupListLabel.setText(Messages.grpUserGroupList);

		GridData gd_buttonAddGroup = new org.eclipse.swt.layout.GridData(
				SWT.FILL, SWT.BOTTOM, false, true);
		buttonAddGroup = new Button(cmpRightAreaGroup, SWT.NONE);
		buttonAddGroup.setEnabled(false);
		buttonAddGroup.setText(Messages.btnAddGroup);
		buttonAddGroup.setLayoutData(gd_buttonAddGroup);
		buttonAddGroup.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				int[] idx = allUserTable.getSelectionIndices();
				if (idx.length < 0)
					return;

				for (int i : idx) {
					TableItem item = new TableItem(userGroupTable, SWT.NONE);
					item.setText(0, allUserTable.getItem(i).getText(0));
				}
				allUserTable.remove(idx);
				setBtnEnableDisable();
			}
		});

		final String[] groupColumnNameArr = new String[] { "col" };
		groupTableViewer = CommonTool.createCommonTableViewer(
				cmpRightAreaGroup, null, groupColumnNameArr,
				CommonTool.createGridData(GridData.FILL_BOTH, 1, 2, -1, 70));
		groupTableViewer.setInput(groupListData);
		userGroupTable = groupTableViewer.getTable();
		userGroupTable.setLinesVisible(false);
		userGroupTable.setHeaderVisible(false);
		userGroupTable.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setBtnEnableDisable();
			}
		});
		buttonRemoveGroup = new Button(cmpRightAreaGroup, SWT.NONE);
		buttonRemoveGroup.setEnabled(false);
		final GridData gd_buttonRemoveGroup = new GridData(SWT.LEFT, SWT.TOP,
				false, true);
		buttonRemoveGroup.setLayoutData(gd_buttonRemoveGroup);
		buttonRemoveGroup.setText(Messages.btnRemoveGroup);
		buttonRemoveGroup.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				int[] idx = userGroupTable.getSelectionIndices();
				if (idx.length < 0)
					return;
				for (int i : idx)
					if (!(userNameText.getText().equalsIgnoreCase("dba"))
							&& userGroupTable.getItem(i).getText(0).equalsIgnoreCase(
									"public")) {
						CommonTool.openErrorBox(parentComp.getShell(),
								Messages.errRomoveUserGroup);
						return;
					}

				for (int i : idx) {
					TableItem item = new TableItem(allUserTable, SWT.NONE);
					item.setText(0, userGroupTable.getItem(i).getText(0));
				}

				userGroupTable.remove(idx);
				setBtnEnableDisable();
			}
		});
		new Label(cmpRightAreaGroup, SWT.NONE);
		Label tmpLabel = new Label(cmpRightAreaGroup, SWT.NONE);
		tmpLabel.setText(Messages.grpUserMemberList);
		new Label(cmpRightAreaGroup, SWT.NONE);
		memberTableViewer = CommonTool.createCommonTableViewer(
				cmpRightAreaGroup, null, groupColumnNameArr,
				CommonTool.createGridData(GridData.FILL_BOTH, 1, 2, -1, 70));
		memberTableViewer.setInput(memberListData);
		memberTableViewer.getTable().setLinesVisible(false);
		memberTableViewer.getTable().setHeaderVisible(false);

	}

	/**
	 * Create auth composit
	 * 
	 * @return
	 */
	private Composite createAuthComposit() {
		final Composite composite = new Composite(tabFolder, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		composite.setLayout(layout);
		Label classTableDescLabel=new Label(composite,SWT.NONE);
		classTableDescLabel.setText(Messages.lblUnAuthorizedTable);
		final String[] columnNameArr = new String[] { Messages.tblColClassName,
				Messages.tblColClassSchematype, Messages.tblColClassOwner,
				 Messages.tblColClassType };
		classTableViewer = CommonTool.createCommonTableViewer(composite,
				new TableViewerSorter(), columnNameArr,
				CommonTool.createGridData(GridData.FILL_BOTH, 3, 1, -1, 200));
		classTableViewer.setInput(classListData);
		classTable = classTableViewer.getTable();
		
		classTable.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setAuthBtnEnableDisable();
			}
		});

		final Composite cmpControl = new Composite(composite, SWT.NONE);
		final GridData gd_cmpControl = new GridData(SWT.CENTER, SWT.FILL,
				false, false);
		cmpControl.setLayoutData(gd_cmpControl);
		final GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		cmpControl.setLayout(gridLayout);

		grantButton = new Button(cmpControl, SWT.LEFT);
		grantButton.setEnabled(false);
		grantButton.setText(Messages.addClassButtonName);
		grantButton.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				int[] idx = classTable.getSelectionIndices();
				if (idx.length < 0)
					return;

				for (int i : idx) {
					String className = classTable.getItem(i).getText(0);
					for (Map<String, String> map : classListData) {
						if (map.get("0").equals(className)) {
							classListData.remove(map);
							break;
						}
					}
					String authNum = "1";
					if (classGrantMap.containsKey(className))
						authNum = classGrantMap.get(className);
					authListData.add(getItemAuthMap(new ClassAuthorizations(
							className, CommonTool.str2Int(authNum))));
				}
				classTableViewer.refresh();
				authTableViewer.refresh();
				if (authTableViewer.getTable().getColumn(0) != null)
					authTableViewer.getTable().getColumn(0).pack();
				setAuthBtnEnableDisable();

			}
		});

		revokeButton = new Button(cmpControl, SWT.NONE);
		revokeButton.setEnabled(false);
		revokeButton.setText(Messages.deleteClassButtonName);
		revokeButton.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				int[] idx = authTable.getSelectionIndices();
				if (idx.length < 0)
					return;
				for (int id : idx) {
					String tableName = authTable.getItem(id).getText(0);
					for (ClassInfo bean : allClassInfoList) {
						if (tableName.equals(bean.getClassName())) {
							if (bean.isSystemClass()) {
								CommonTool.openErrorBox(parentComp.getShell(),
										Messages.errRemoveSysClass);
								return;
							} else {
								Map<String, String> map = new HashMap<String, String>();
								map.put("0", bean.getClassName());
								map.put("1", bean.isSystemClass()?Messages.msgSystemSchema:Messages.msgUserSchema);
								map.put("2", bean.getOwnerName());
								map.put(
										"3",
										bean.getClassType() == ClassType.VIEW ? Messages.msgVirtualClass
												: Messages.msgClass);
								classListData.add(map);
							}
						}
					}
					for (Map<String, Object> map : authListData) {
						String className = (String) map.get("0");
						if (tableName.equals(className)) {
							authListData.remove(map);
							break;
						}
					}

				}
				authTableViewer.refresh();
				classTableViewer.refresh();
				setAuthBtnEnableDisable();
			}
		});
		Label authTableDescLabel=new Label(composite,SWT.NONE);
		authTableDescLabel.setText(Messages.lblAuthorizedTable);
		final String[] authColumnNameArr = new String[] {
				Messages.tblColAuthTable, Messages.tblColAuthSelect,
				Messages.tblColAuthInsert, Messages.tblColAuthUpdate,
				Messages.tblColAuthDelete, Messages.tblColAuthAlter,
				Messages.tblColAuthIndex, Messages.tblColAuthExecute,
				Messages.tblColAuthGrantselect, Messages.tblColAuthGrantinsert,
				Messages.tblColAuthGrantupdate, Messages.tblColAuthGrantdelete,
				Messages.tblColAuthGrantalter, Messages.tblColAuthGrantindex,
				Messages.tblColAuthGrantexecute

		};
		authTableViewer = createCommonTableViewer(composite, authColumnNameArr,
				CommonTool.createGridData(GridData.FILL_BOTH, 3, 1, -1, 200));
		authTableViewer.setLabelProvider(new MyTableLabelProvider());

		authTableViewer.setInput(authListData);
		authTable = authTableViewer.getTable();
		CellEditor[] editors = new CellEditor[15];
		editors[0] = null;
		for (int i = 1; i < 15; i++)
			editors[i] = new CheckboxCellEditor(authTable, SWT.READ_ONLY);

		authTableViewer.setColumnProperties(authColumnNameArr);
		authTableViewer.setCellEditors(editors);
		authTableViewer.setCellModifier(new ICellModifier() {
			@SuppressWarnings("unchecked")
			public boolean canModify(Object element, String property) {
				Map<String, Object> map = (Map<String, Object>) element;
				if (!database.getDatabaseInfo().getAuthLoginedDbUserInfo().isDbaAuthority())
					return false;
				String name = (String) map.get("0");
				for (ClassInfo bean : allClassInfoList) {
					if (name.equals(bean.getClassName())
							&& bean.isSystemClass()) {
						return false;
					}
				}
				return true;
			}

			@SuppressWarnings("unchecked")
			public Object getValue(Object element, String property) {
				Map<String, Object> map = (Map<String, Object>) element;
				for (int i = 1; i < 15; i++)
					if (property.equals(authColumnNameArr[i])) {
						return Boolean.valueOf((Boolean) map.get("" + i));
					}
				return null;
			}

			@SuppressWarnings("unchecked")
			public void modify(Object element, String property, Object value) {
				if (element instanceof Item) {
					element = ((Item) element).getData();
				}

				String key = "";
				Map<String, Object> map = (Map<String, Object>) element;
				for (int i = 1; i < 15; i++)
					if (property.equals(authColumnNameArr[i])) {
						key = "" + i;
						break;
					}

				if (value instanceof Boolean) {
					map.put(key, ((Boolean) value).booleanValue());
				}

				authTableViewer.refresh();
			}
		});

		authTable.addSelectionListener(new SelectionListener() {
			public void widgetDefaultSelected(SelectionEvent e) {
			}

			public void widgetSelected(SelectionEvent e) {
				setAuthBtnEnableDisable();
			}
		});

		authTable.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
			public void focusGained(FocusEvent e) {
				setAuthBtnEnableDisable();
			}
		});

		return composite;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		getShell().setSize(680, 650);
		CommonTool.centerShell(getShell());
		if (!isNewFlag())
			getShell().setText(Messages.msgEditUserDialog);
		else
			getShell().setText(Messages.msgAddUserDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);
		if (!database.getDatabaseInfo().getAuthLoginedDbUserInfo().isDbaAuthority())
			getButton(IDialogConstants.OK_ID).setEnabled(false);
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			if (valid()) {
				final boolean dbaflag = userNameText.getText().equalsIgnoreCase(
						DB_DBA_USERNAME);

				boolean changePassword = false;
				if (newFlag)
					changePassword = true;
				else if (passwordButton.getSelection())
					changePassword = true;

				String password = "";

				if (changePassword) {
					password = pwdText.getText();
					if (password == null)
						password = "";
				}

				UpdateAddUserTask task = new UpdateAddUserTask(
						database.getServer().getServerInfo(), newFlag);
				UserSendObj userSendObj = new UserSendObj();
				userSendObj.setDbname(database.getName());
				userSendObj.setUsername(userNameText.getText().toLowerCase());
				// String message = "";
				if (changePassword) {
					if (dbaflag) {
						if (pwdText.getText() == null
								|| pwdText.getText().length() <= 0)
							userSendObj.setUserpass("__NULL__");
						else
							userSendObj.setUserpass(password);
					} else if (database.getDatabaseInfo().getAuthLoginedDbUserInfo().getName().equalsIgnoreCase(
							userName)) {
						if (newFlag
								|| !oldLoginPassword.equals(pwdText.getText())) {
							if (pwdText.getText() == null
									|| pwdText.getText().length() <= 0)
								userSendObj.setUserpass("__NULL__");
							else
								userSendObj.setUserpass(password);
						}
					} else {
						if (pwdText.getText() == null
								|| pwdText.getText().length() <= 0)
							userSendObj.setUserpass("__NULL__");
						else
							userSendObj.setUserpass(password);
					}
				}

				for (int i = 0; i < userGroupTable.getItemCount(); i++) {
					userSendObj.addGroups(userGroupTable.getItem(i).getText(0));
				}

				if (!dbaflag) {
					for (int i = 0; i < authListData.size(); i++) {
						Map<String, Object> map = authListData.get(i);

						String className = (String) map.get("0");
						int authNum = 0;
						for (int j = 1; j < 15; j++) {
							if ((Boolean) map.get(j + "")) {
								switch (j) {
								case 1:
									authNum += 1;
									break;
								case 2:
									authNum += 2;
									break;
								case 3:
									authNum += 4;
									break;
								case 4:
									authNum += 8;
									break;
								case 5:
									authNum += 16;
									break;
								case 6:
									authNum += 32;
									break;
								case 7:
									authNum += 64;
									break;
								case 8:
									authNum += 256;
									break;
								case 9:
									authNum += 512;
									break;
								case 10:
									authNum += 1024;
									break;
								case 11:
									authNum += 2048;
									break;
								case 12:
									authNum += 4096;
									break;
								case 13:
									authNum += 8192;
									break;
								case 14:
									authNum += 16384;
									break;
								}
							}
						} // end for 1 - 15
						userSendObj.addAuthorization(className, authNum + "");

					}// end for list
				}// end if !dbaflag
				task.setUserSendObj(userSendObj);
				taskExecutor.setTask(new SocketTask[] { task });
				ExecTaskWithProgress exec = new ExecTaskWithProgress(
						taskExecutor);
				try {
					new ProgressMonitorDialog(null).run(true, true, exec);
				} catch (InvocationTargetException e) {
					logger.error(e.getMessage(), e);
				} catch (InterruptedException e) {
					logger.error(e.getMessage(), e);
				}

				if (task.isSuccess()
						&& changePasswordFlag
						&& database.getDatabaseInfo().getAuthLoginedDbUserInfo().getName().equalsIgnoreCase(
								userName)) {
					database.getDatabaseInfo().getAuthLoginedDbUserInfo().setNoEncryptPassword(
							password);
				}
			} else {
				return;
			}

		}
		super.buttonPressed(buttonId);
	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {
		List<String> groupList = null;
		if (newFlag) {
			userNameText.setEnabled(true);
			classGrantMap = new HashMap<String, String>();
			Map<String, String> map = new HashMap<String, String>();
			map.put("0", DB_DEFAULT_USERNAME);
			groupList = new ArrayList<String>();
			groupList.add(DB_DEFAULT_USERNAME);
		} else {
			for (DbUserInfo bean : userListInfo.getUserList()) {
				if (bean.getName().equalsIgnoreCase(userName)) {
					userInfo = bean;
				}
				List<String> groups = bean.getGroups().getGroup();
				if (groups != null)
					for (String g : groups) {
						if (userName != null && userName.equalsIgnoreCase(g)) {
							Map<String, String> map = new HashMap<String, String>();
							map.put("0", bean.getName());
							memberListData.add(map);
						}
					}

			}
			memberTableViewer.refresh();
			if (!newFlag)
				if ((database.getDatabaseInfo().getAuthLoginedDbUserInfo().getName().equalsIgnoreCase(
						userName) && database.getDatabaseInfo().getAuthLoginedDbUserInfo().isDbaAuthority())
						|| database.getDatabaseInfo().getAuthLoginedDbUserInfo().getName().equalsIgnoreCase(
								DB_DBA_USERNAME))
					passwordButton.setEnabled(true);
				else
					passwordButton.setEnabled(false);
			userNameText.setText(userInfo.getName());
			/*
			 * if (!"".equals(userInfo.getPassword()) && null !=
			 * userInfo.getPassword()) {
			 * pwdText.setText(userInfo.getPassword());
			 * pwdCfmText.setText(userInfo.getPassword()); // isPwdChanged =
			 * false; }
			 */
			groupList = userInfo.getGroups().getGroup();
			classGrantMap = userInfo.getAuthorization();
			oldLoginPassword = database.getDatabaseInfo().getAuthLoginedDbUserInfo().getNoEncryptPassword();
			if (oldLoginPassword == null)
				oldLoginPassword = "";

		}

		Map<String, String> groupMap = new HashMap<String, String>();
		Map<String, String> memberMap = new HashMap<String, String>();
		// set group map
		if (groupList == null)
			groupList = new ArrayList<String>();
		for (String group : groupList)
			groupMap.put(group.toLowerCase(), "");
		for (Map<String, String> map : memberListData) {
			memberMap.put(map.get("0").toLowerCase(), "");
		}

		for (DbUserInfo user : userListInfo.getUserList()) {
			if (!groupMap.containsKey(user.getName().toLowerCase())
					&& !memberMap.containsKey(user.getName().toLowerCase())
					&& !user.getName().equalsIgnoreCase(userName)) {
				Map<String, String> map = new HashMap<String, String>();
				map.put("0", user.getName().toLowerCase());
				allUserListData.add(map);
			}
		}

		for (String userName : groupList) {
			Map<String, String> map = new HashMap<String, String>();
			map.put("0", userName.toLowerCase());
			groupListData.add(map);
		}
		allUserTableViewer.refresh();
		groupTableViewer.refresh();
		// if (!newFlag) {
		Iterator<String> authIter = classGrantMap.keySet().iterator();
		while (authIter.hasNext()) {
			String className = authIter.next();
			if (!partitionClassMap.containsKey(className)) {
				String authNum = classGrantMap.get(className);
				authListData.add(getItemAuthMap(new ClassAuthorizations(
						className, CommonTool.str2Int(authNum))));
			}
		}
		if (!DB_DBA_USERNAME.equalsIgnoreCase(userName)) {

			authTableViewer.refresh();
			for (ClassInfo bean : allClassInfoList) {

				if (classGrantMap.containsKey(bean.getClassName())
						|| bean.isSystemClass()
						|| bean.getOwnerName().equalsIgnoreCase(
								DB_DEFAULT_USERNAME))
					continue;
				Map<String, String> map = new HashMap<String, String>();
				map.put("0", bean.getClassName());
				map.put("1", Messages.msgUserSchema);
				map.put("2", bean.getOwnerName());
				map.put(
						"3",
						bean.getClassType() == ClassType.VIEW ? Messages.msgVirtualClass
								: Messages.msgClass);

				classListData.add(map);
			}
			classTableViewer.refresh();
			for (int i = 0; i < classTable.getColumnCount(); i++)
				classTable.getColumn(i).pack();
			for (int i = 0; i < authTable.getColumnCount(); i++)
				authTable.getColumn(i).pack();
		}
		for (int i = 0; i < allUserTable.getColumnCount(); i++)
			allUserTable.getColumn(i).pack();
		for (int i = 0; i < userGroupTable.getColumnCount(); i++)
			userGroupTable.getColumn(i).pack();
		for (int i = 0; i < memberTableViewer.getTable().getColumnCount(); i++)
			memberTableViewer.getTable().getColumn(i).pack();

		dbaLoginFlag = database.getDatabaseInfo().getAuthLoginedDbUserInfo().getName().equalsIgnoreCase(
				DB_DBA_USERNAME);
	}

	/**
	 * Get item map
	 * 
	 * @param auth
	 * @return
	 */
	private Map<String, Object> getItemAuthMap(ClassAuthorizations auth) {
		Map<String, Object> map = new HashMap<String, Object>();
		map.put("0", auth.getClassName());
		map.put("1", auth.isSelectPriv());
		map.put("2", auth.isInsertPriv());
		map.put("3", auth.isUpdatePriv());
		map.put("4", auth.isDeletePriv());
		map.put("5", auth.isAlterPriv());
		map.put("6", auth.isIndexPriv());
		map.put("7", auth.isExecutePriv());
		map.put("8", auth.isGrantSelectPriv());
		map.put("9", auth.isGrantInsertPriv());
		map.put("10", auth.isGrantUpdatePriv());
		map.put("11", auth.isGrantDeletePriv());
		map.put("12", auth.isGrantAlterPriv());
		map.put("13", auth.isGrantIndexPriv());
		map.put("14", auth.isGrantExecutePriv());
		return map;
	}

	/**
	 * Set the button disable
	 * 
	 */
	private void setBtnEnableDisable() {
		buttonAddGroup.setEnabled(false);
		buttonRemoveGroup.setEnabled(false);
		if (!database.getDatabaseInfo().getAuthLoginedDbUserInfo().isDbaAuthority())
			return;
		if (userNameText.getText().equalsIgnoreCase(DB_DEFAULT_USERNAME))
			return;
		if (allUserTable.getSelectionCount() > 0
				&& allUserTable.isFocusControl()) {
			if (!userNameText.getText().equalsIgnoreCase(DB_DBA_USERNAME))
				buttonAddGroup.setEnabled(true);
		} else if (userGroupTable.getSelectionCount() > 0
				&& userGroupTable.isFocusControl()) {
			int[] idx = userGroupTable.getSelectionIndices();
			for (int id : idx) {
				String name = userGroupTable.getItem(id).getText();
				if (name.equalsIgnoreCase(DB_DEFAULT_USERNAME)) {
					buttonRemoveGroup.setEnabled(false);
					return;
				}
			}

			buttonRemoveGroup.setEnabled(true);
		}
	}

	/**
	 * Set the auth button disable
	 * 
	 */
	private void setAuthBtnEnableDisable() {
		grantButton.setEnabled(false);
		revokeButton.setEnabled(false);
		if (!database.getDatabaseInfo().getAuthLoginedDbUserInfo().isDbaAuthority())
			return;
		if (classTable.getSelectionCount() > 0 && classTable.isFocusControl()) {
			grantButton.setEnabled(true);
		} else if (authTable.getSelectionCount() > 0
				&& authTable.isFocusControl()) {
			revokeButton.setEnabled(true);
		}
	}

	/**
	 * 
	 * Check the data validation
	 * 
	 * @return
	 */
	public boolean valid() {
		String userName = userNameText.getText();
		if (userName == null)
			userName = "";
		if (newFlag) {

			String pwd = pwdText.getText();
			String pwdcfm = pwdCfmText.getText();

			if (!pwd.equals(pwdcfm)) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.errPasswordDiff);
				return false;
			}
			if (userName == null || userName.equals("")) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.errInputName);
				return false;
			}
			if (!ValidateUtil.isValidDBName(userName)) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.errInputNameValidate);
				return false;
			}

			if (userName.length() > ValidateUtil.MAX_NAME_LENGTH) {
				CommonTool.openErrorBox(parentComp.getShell(), Messages.bind(
						Messages.errInputNameLength,
						ValidateUtil.MAX_NAME_LENGTH));
				return false;
			}
			for (DbUserInfo user : userListInfo.getUserList()) {
				if (user.getName().equalsIgnoreCase(userName)) {
					CommonTool.openErrorBox(parentComp.getShell(),
							Messages.errInputNameExist);
					return false;
				}
			}

			if (!pwd.equals(pwdcfm)) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.errPasswordDiff);
				return false;
			}
			if (pwd.length() > ValidateUtil.MAX_PASSWORD_LENGTH) {
				CommonTool.openErrorBox(parentComp.getShell(), Messages.bind(
						Messages.errInputPassLength,
						ValidateUtil.MAX_PASSWORD_LENGTH));
				return false;
			}
			if (pwd == null || pwd.equals("")) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.errInputPassword);
				return false;
			}
			if (pwd.indexOf(" ") >= 0) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.errInvalidPassword);
				return false;
			}
			if (pwd.equals("__NULL__")) {
				CommonTool.openErrorBox(parentComp.getShell(), Messages.bind(
						Messages.errInputNameAccept, "__NULL__"));
				return false;
			}

		} else if (passwordButton.getSelection()) {
			String pwd = pwdText.getText();
			String pwdcfm = pwdCfmText.getText();
			String oldPwd = oldPwdText.getText();
			
			if (oldPwdText.getEnabled() && !oldLoginPassword.equals(oldPwd)) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.errOldPassword);
				return false;
			}
			if (!pwd.equals(pwdcfm)) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.errPasswordDiff);
				return false;
			}
			if (pwd.length() > ValidateUtil.MAX_NAME_LENGTH) {
				CommonTool.openErrorBox(parentComp.getShell(), Messages.bind(
						Messages.errInputPassLength,
						ValidateUtil.MAX_NAME_LENGTH));
				return false;
			}

			if (pwd == null || pwd.equals("")) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.errInputPassword);
				return false;
			}
			if (pwd.indexOf(" ") >= 0) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.errInvalidPassword);
				return false;
			}

			if (pwd.equals("__NULL__")) {
				CommonTool.openErrorBox(parentComp.getShell(), Messages.bind(
						Messages.errInputNameAccept, "__NULL__"));
				return false;
			}
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

	public void execTask(final int buttonId, final ITask[] tasks,
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
										for (ITask t : tasks) {
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
							for (ITask task : tasks) {
								if (task != null) {

									if (task instanceof GetAllClassListTask) {
										setAllClassInfoList(((GetAllClassListTask) task).getAllClassInfoList());
										continue;
									}
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

	public DbUserInfoList getUserListInfo() {
		return userListInfo;
	}

	public void setUserListInfo(DbUserInfoList userListInfo) {
		this.userListInfo = userListInfo;
	}

	public String getUserName() {
		return userName;
	}

	public void setUserName(String userName) {
		this.userName = userName;
	}

	public boolean isNewFlag() {
		return newFlag;
	}

	public void setNewFlag(boolean newFlag) {
		this.newFlag = newFlag;
	}

	private class EditUserTaskExec extends
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
			if (!monitor.isCanceled()) {
				display.syncExec(new Runnable() {
					public void run() {
						setReturnCode(OK);
						close();
					}
				});
			}
			return true;
		}
	}

	/**
	 * 
	 * The provider is get table colume image
	 * 
	 * @author robin 2009-6-4
	 */
	static class MyTableLabelProvider extends
			TableLabelProvider {

		@SuppressWarnings("unchecked")
		@Override
		public Image getColumnImage(Object element, int columnIndex) {
			Map<String, Object> item = (Map<String, Object>) element;
			if (columnIndex > 0) {
				Boolean flag = (Boolean) item.get(columnIndex + "");
				return flag ? CubridManagerUIPlugin.getImage("icons/checked.gif")
						: CubridManagerUIPlugin.getImage("icons/unchecked.gif");

			}
			return null;
		}

		@SuppressWarnings("unchecked")
		@Override
		public String getColumnText(Object element, int columnIndex) {
			if (!(element instanceof Map)) {
				return "";
			}
			if (columnIndex != 0)
				return null;
			Map<String, Object> map = (Map<String, Object>) element;
			return map.get("" + columnIndex).toString();
		}
		@Override
		public boolean isLabelProperty(Object element, String property) {
			return true;
		}
	}

	/**
	 * Create common tableViewer
	 * 
	 * @param parent
	 * @param columnNameArr
	 * @param gridData
	 * @return
	 */
	public TableViewer createCommonTableViewer(Composite parent,
			final String[] columnNameArr, GridData gridData) {
		final TableViewer tableViewer = new TableViewer(parent, SWT.V_SCROLL
				| SWT.MULTI | SWT.BORDER | SWT.H_SCROLL | SWT.FULL_SELECTION);
		tableViewer.setContentProvider(new TableContentProvider());
		tableViewer.setLabelProvider(new MyTableLabelProvider());
		tableViewer.setSorter(new TableViewerSorter());

		tableViewer.getTable().setLinesVisible(true);
		tableViewer.getTable().setHeaderVisible(true);

		tableViewer.getTable().setLayoutData(gridData);

		for (int i = 0; i < columnNameArr.length; i++) {
			final TableColumn tblColumn = new TableColumn(
					tableViewer.getTable(), SWT.CHECK);
			if (i != 0)
				tblColumn.setImage(CubridManagerUIPlugin.getImage("icons/unchecked.gif"));
			tblColumn.setData(false);
			tblColumn.setText(columnNameArr[i]);
			final int num = i;
			tblColumn.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent event) {
					TableColumn column = (TableColumn) event.widget;

					if (num == 0) {

						int j = 0;
						for (j = 0; j < columnNameArr.length; j++) {
							if (column.getText().equals(columnNameArr[j])) {
								break;
							}
						}
						TableViewerSorter sorter = ((TableViewerSorter) tableViewer.getSorter());
						if (sorter == null) {
							return;
						}
						sorter.doSort(j);
						tableViewer.getTable().setSortColumn(column);
						tableViewer.getTable().setSortDirection(
								sorter.isAsc() ? SWT.UP : SWT.DOWN);
						tableViewer.refresh();
						for (int k = 0; k < tableViewer.getTable().getColumnCount(); k++) {
							tableViewer.getTable().getColumn(k).pack();
						}
						return;
					}

					if ((Boolean) tblColumn.getData()) {
						column.setImage(CubridManagerUIPlugin.getImage("icons/unchecked.gif"));
						column.setData(false);

						for (int i = 0; i < column.getParent().getItemCount(); i++) {
							Map<String, Object> map = authListData.get(i);
							if (isSystemClass((String) map.get("0"))) {
								map.put(num + "", false);
								// TableItem item =
								// column.getParent().getItem(i);
								// item.setImage(num,
								// CubridManagerUIPlugin.getImage("icons/unchecked.gif"));
							}
						}
					} else {
						column.setImage(CubridManagerUIPlugin.getImage("icons/checked.gif"));
						column.setData(true);

						for (int i = 0; i < column.getParent().getItemCount(); i++) {
							Map<String, Object> map = authListData.get(i);
							if (isSystemClass((String) map.get("0"))) {
								map.put(num + "", true);
								// TableItem item =
								// column.getParent().getItem(i);
								// item.setImage(num,
								// CubridManagerUIPlugin.getImage("icons/checked.gif"));
							}
						}
					}
					tableViewer.refresh();
				}
			});

			tblColumn.pack();
		}
		return tableViewer;
	}

	public boolean isSystemClass(String name) {
		if (!database.getDatabaseInfo().getAuthLoginedDbUserInfo().isDbaAuthority())
			return false;
		for (ClassInfo bean : allClassInfoList) {
			if (bean.getClassName().equals(name) && bean.isSystemClass()) {
				return false;
			}
		}
		return true;
	}

	public List<ClassInfo> getAllClassInfoList() {
		return allClassInfoList;
	}

	public void setAllClassInfoList(List<ClassInfo> allClassInfoList) {
		this.allClassInfoList = allClassInfoList;
	}

	public Map<String, String> getPartitionClassMap() {
		return partitionClassMap;
	}

	public void setPartitionClassMap(Map<String, String> partitionClassMap) {
		this.partitionClassMap = partitionClassMap;
	}

}
