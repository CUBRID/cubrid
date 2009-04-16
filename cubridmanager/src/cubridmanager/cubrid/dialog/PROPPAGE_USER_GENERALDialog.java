/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;
import cubridmanager.CubridmanagerPlugin;
import cubridmanager.Messages;
import cubridmanager.CommonTool;
import cubridmanager.cubrid.UserInfo;
import java.util.ArrayList;
import cubridmanager.cubrid.view.CubridView;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class PROPPAGE_USER_GENERALDialog extends Dialog {
	public static String DBUser = new String("");
	private Composite comparent = null;
	private boolean newflag = false;
	private Shell dlgShell = null; // @jve:decl-index=0:visual-constraint="8,0"
	private Composite sShell = null;
	private Group groupUserGeneral = null;
	private CLabel clabel1 = null;
	private Label label1 = null;
	public Text EDIT_USER_LOGIN = null;
	private Label label2 = null;
	public Text EDIT_USER_PASSWORD = null;
	private Label lblPasswordConfirm = null;
	public Text EDIT_USER_PASSWORD_CONFIRM = null;
	private Group groupMember = null;
	public Table LIST_USER_GROUPS = null;
	private Button BUTTON_ADD_GROUP = null;
	private Button BUTTON_REMOVE_GROUP = null;
	private Table LIST_USER_ALLUSERS = null;
	private Button BUTTON_ADD_MEMBER = null;
	private Button BUTTON_REMOVE_MEMBER = null;
	public Table LIST_USER_MEMBERS = null;
	private Composite cmpAllUsers = null;
	private Composite cmpRightAreaMember;
	private Composite cmpRightAreaGroup;

	public PROPPAGE_USER_GENERALDialog(Shell parent) {
		super(parent);
	}

	public PROPPAGE_USER_GENERALDialog(Shell parent, int style) {
		super(parent, style);
	}

	public Composite SetTabPart(TabFolder parent) {
		comparent = parent;
		createSShell();
		sShell.setParent(parent);
		return sShell;
	}

	public int doModal() {
		createDlgShell();
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createDlgShell() {
		dlgShell = new Shell(comparent.getShell(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell.setText(Messages
				.getString("TITLE.PROPPAGE_USER_GENERALDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createSShell();
	}

	private void createSShell() {
		GridLayout gridLayout45 = new GridLayout();
		gridLayout45.numColumns = 2;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.grabExcessVerticalSpace = true;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.grabExcessHorizontalSpace = true;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.grabExcessVerticalSpace = true;

		// sShell = new Composite(dlgShell, SWT.NONE); // comment out to use VE
		sShell = new Composite(comparent, SWT.NONE);
		sShell.setLayout(new GridLayout());

		createGroupUserGeneral();
		createGroupMember();

		setinfo();
		sShell.pack();
	}

	private void createGroupUserGeneral() {
		GridLayout gridLayout38 = new GridLayout();
		gridLayout38.numColumns = 3;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 3;
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.grabExcessHorizontalSpace = true;
		groupUserGeneral = new Group(sShell, SWT.NONE);
		groupUserGeneral.setLayoutData(gridData);
		groupUserGeneral.setLayout(gridLayout38);

		GridData gridData41 = new org.eclipse.swt.layout.GridData();
		gridData41.widthHint = 100;
		GridData gridData40 = new org.eclipse.swt.layout.GridData();
		gridData40.widthHint = 100;
		gridData40.grabExcessHorizontalSpace = true;
		GridData gridData39 = new org.eclipse.swt.layout.GridData();
		gridData39.verticalSpan = 3;
		gridData39.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData39.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData39.grabExcessHorizontalSpace = true;
		clabel1 = new CLabel(groupUserGeneral, SWT.SHADOW_NONE);
		clabel1.setImage(CubridmanagerPlugin.getImage("/image/dbuser.png"));
		clabel1.setLayoutData(gridData39);
		label1 = new Label(groupUserGeneral, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.LOGIN"));
		EDIT_USER_LOGIN = new Text(groupUserGeneral, SWT.BORDER);
		EDIT_USER_LOGIN.setLayoutData(gridData40);
		label2 = new Label(groupUserGeneral, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.PASSWORD1"));
		EDIT_USER_PASSWORD = new Text(groupUserGeneral, SWT.BORDER
				| SWT.PASSWORD);
		EDIT_USER_PASSWORD.setLayoutData(gridData41);
		lblPasswordConfirm = new Label(groupUserGeneral, SWT.LEFT);
		lblPasswordConfirm.setText(Messages.getString("LABEL.CONFIRMPASSWORD"));
		EDIT_USER_PASSWORD_CONFIRM = new Text(groupUserGeneral, SWT.BORDER
				| SWT.PASSWORD);
		EDIT_USER_PASSWORD_CONFIRM.setLayoutData(gridData41);
	}

	private void createGroupMember() {
		GridLayout gridLayout42 = new GridLayout();
		gridLayout42.numColumns = 2;
		gridLayout42.marginWidth = 0;
		gridLayout42.horizontalSpacing = 0;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessHorizontalSpace = true;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.grabExcessVerticalSpace = true;
		groupMember = new Group(sShell, SWT.NONE);
		groupMember.setText(Messages.getString("GROUP.GROUPCONF"));
		groupMember.setLayout(gridLayout42);
		groupMember.setLayoutData(gridData1);

		createCmpAllUsers();
		createCmpRightAreaGroup();
		createCmpRightAreaMember();
	}

	private void createCmpAllUsers() {
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.heightHint = 250;
		gridData4.widthHint = 250;
		gridData4.grabExcessHorizontalSpace = true;
		gridData4.grabExcessVerticalSpace = true;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData4.verticalSpan = 2;
		cmpAllUsers = new Composite(groupMember, SWT.NONE);
		cmpAllUsers.setLayout(new GridLayout());
		cmpAllUsers.setLayoutData(gridData4);
		(new Label(cmpAllUsers, SWT.NONE)).setText(Messages
				.getString("GROUP.ALLUSERS"));

		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.grabExcessHorizontalSpace = true;
		gridData5.grabExcessVerticalSpace = true;
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST_USER_ALLUSERS = new Table(cmpAllUsers, SWT.FULL_SELECTION
				| SWT.BORDER);
		LIST_USER_ALLUSERS.setLinesVisible(false);
		LIST_USER_ALLUSERS.setLayoutData(gridData5);
		LIST_USER_ALLUSERS.setHeaderVisible(false);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 200, true));
		LIST_USER_ALLUSERS.setLayout(tlayout);
		LIST_USER_ALLUSERS.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setBtnEnableDisable();
			}
		});

		TableColumn tblcol = new TableColumn(LIST_USER_ALLUSERS, SWT.LEFT);
		tblcol.setText("col1");
	}

	private void createCmpRightAreaGroup() {
		GridData gridData5 = new GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.grabExcessVerticalSpace = true;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.widthHint = 250;
		GridLayout gridLayout = new GridLayout();
		gridLayout.horizontalSpacing = 10;
		gridLayout.numColumns = 2;
		cmpRightAreaGroup = new Composite(groupMember, SWT.NONE);
		cmpRightAreaGroup.setLayout(gridLayout);
		cmpRightAreaGroup.setLayoutData(gridData5);

		new Label(cmpRightAreaGroup, SWT.NONE);
		(new Label(cmpRightAreaGroup, SWT.NONE)).setText(Messages
				.getString("GROUP.GROUPLIST"));
		GridData gridData43 = new org.eclipse.swt.layout.GridData();
		gridData43.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData43.verticalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData43.grabExcessVerticalSpace = true;
		BUTTON_ADD_GROUP = new Button(cmpRightAreaGroup, SWT.NONE);
		BUTTON_ADD_GROUP.setText(Messages.getString("BUTTON.ADDGROUP"));
		BUTTON_ADD_GROUP.setLayoutData(gridData43);
		BUTTON_ADD_GROUP
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int idx = LIST_USER_ALLUSERS.getSelectionIndex();
						if (idx < 0)
							return;
						TableItem item = new TableItem(LIST_USER_GROUPS,
								SWT.NONE);
						item.setText(0, LIST_USER_ALLUSERS.getItem(idx)
								.getText(0));
						LIST_USER_ALLUSERS.remove(idx);
						setBtnEnableDisable();
					}
				});

		GridData gridData7 = new GridData();
		gridData7.verticalSpan = 2;
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData7.grabExcessHorizontalSpace = true;
		gridData7.grabExcessVerticalSpace = true;
		gridData7.widthHint = 250;
		gridData7.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST_USER_GROUPS = new Table(cmpRightAreaGroup, SWT.FULL_SELECTION
				| SWT.BORDER);
		LIST_USER_GROUPS.setLinesVisible(false);
		LIST_USER_GROUPS.setLayoutData(gridData7);
		LIST_USER_GROUPS.setHeaderVisible(false);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 200, true));
		LIST_USER_GROUPS.setLayout(tlayout);
		LIST_USER_GROUPS.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setBtnEnableDisable();
			}
		});

		TableColumn tblcol = new TableColumn(LIST_USER_GROUPS, SWT.LEFT);
		tblcol.setText("col1");

		GridData gridData44 = new org.eclipse.swt.layout.GridData();
		gridData44.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData44.verticalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData44.grabExcessVerticalSpace = true;
		BUTTON_REMOVE_GROUP = new Button(cmpRightAreaGroup, SWT.NONE);
		BUTTON_REMOVE_GROUP.setText(Messages.getString("BUTTON.REMOVEGROUP"));
		BUTTON_REMOVE_GROUP.setLayoutData(gridData44);
		BUTTON_REMOVE_GROUP
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int idx = LIST_USER_GROUPS.getSelectionIndex();
						if (idx < 0)
							return;
						if (!(EDIT_USER_LOGIN.getText().equalsIgnoreCase("dba"))
								&& LIST_USER_GROUPS.getItem(idx).getText(0)
										.equals("public")) {
							CommonTool.ErrorBox(sShell.getShell(), Messages
									.getString("ERROR.CANNOTREMOVEPUBLIC"));
							return;
						}
						TableItem item = new TableItem(LIST_USER_ALLUSERS,
								SWT.NONE);
						item.setText(0, LIST_USER_GROUPS.getItem(idx)
								.getText(0));
						LIST_USER_GROUPS.remove(idx);
						setBtnEnableDisable();
					}
				});

	}

	private void createCmpRightAreaMember() {
		GridData gridData8 = new GridData();
		gridData8.grabExcessVerticalSpace = true;
		gridData8.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 2;
		gridLayout1.horizontalSpacing = 10;
		cmpRightAreaMember = new Composite(groupMember, SWT.NONE);
		cmpRightAreaMember.setLayout(gridLayout1);
		cmpRightAreaMember.setLayoutData(gridData8);

		new Label(cmpRightAreaMember, SWT.NONE);
		(new Label(cmpRightAreaMember, SWT.NONE)).setText(Messages
				.getString("GROUP.MEMBERLIST"));

		GridData gridData46 = new org.eclipse.swt.layout.GridData();
		gridData46.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData46.verticalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData46.grabExcessVerticalSpace = true;
		BUTTON_ADD_MEMBER = new Button(cmpRightAreaMember, SWT.NONE);
		BUTTON_ADD_MEMBER.setText(Messages.getString("BUTTON.ADDMEMBER"));
		BUTTON_ADD_MEMBER.setLayoutData(gridData46);
		BUTTON_ADD_MEMBER
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int idx = LIST_USER_ALLUSERS.getSelectionIndex();
						if (idx < 0)
							return;
						TableItem item = new TableItem(LIST_USER_MEMBERS,
								SWT.NONE);
						item.setText(0, LIST_USER_ALLUSERS.getItem(idx)
								.getText(0));
						LIST_USER_ALLUSERS.remove(idx);
						setBtnEnableDisable();
					}
				});

		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.grabExcessHorizontalSpace = true;
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData6.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData6.verticalSpan = 2;
		gridData6.widthHint = 250;
		gridData6.grabExcessVerticalSpace = true;
		LIST_USER_MEMBERS = new Table(cmpRightAreaMember, SWT.FULL_SELECTION
				| SWT.BORDER);
		LIST_USER_MEMBERS.setLinesVisible(false);
		LIST_USER_MEMBERS.setLayoutData(gridData6);
		LIST_USER_MEMBERS.setHeaderVisible(false);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 200, true));
		LIST_USER_MEMBERS.setLayout(tlayout);
		LIST_USER_MEMBERS.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setBtnEnableDisable();
			}
		});

		TableColumn tblcol = new TableColumn(LIST_USER_MEMBERS, SWT.LEFT);
		tblcol.setText("col1");

		GridData gridData47 = new org.eclipse.swt.layout.GridData();
		gridData47.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData47.verticalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData47.grabExcessVerticalSpace = true;
		BUTTON_REMOVE_MEMBER = new Button(cmpRightAreaMember, SWT.NONE);
		BUTTON_REMOVE_MEMBER.setText(Messages.getString("BUTTON.REMOVEMEMBER"));
		BUTTON_REMOVE_MEMBER.setLayoutData(gridData47);
		BUTTON_REMOVE_MEMBER
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int idx = LIST_USER_MEMBERS.getSelectionIndex();
						if (idx < 0)
							return;
						TableItem item = new TableItem(LIST_USER_ALLUSERS,
								SWT.NONE);
						item.setText(0, LIST_USER_MEMBERS.getItem(idx).getText(
								0));
						LIST_USER_MEMBERS.remove(idx);
						setBtnEnableDisable();
					}
				});
	}

	private void setinfo() {
		DBUser = DBUser.trim();
		newflag = (DBUser.length() <= 0) ? true : false;
		EDIT_USER_LOGIN.setText(DBUser);
		if (!newflag)
			EDIT_USER_LOGIN.setEnabled(false);

		ArrayList userinfo = UserInfo.UserInfo_get(CubridView.Current_db);

		if (newflag) {
			for (int i = 0, n = userinfo.size(); i < n; i++) {
				UserInfo ui = (UserInfo) userinfo.get(i);
				if (ui.userName.equals("public"))
					continue;
				TableItem item = new TableItem(LIST_USER_ALLUSERS, SWT.NONE);
				item.setText(0, ui.userName);
			}
			TableItem item = new TableItem(LIST_USER_GROUPS, SWT.NONE);
			item.setText(0, "public");
		} else {
			boolean chkflag = false;
			UserInfo uiedit = UserInfo.UserInfo_find(userinfo, DBUser);
			EDIT_USER_PASSWORD.setText(uiedit.password);
			EDIT_USER_PASSWORD_CONFIRM.setText(uiedit.password);

			for (int i = 0, n = userinfo.size(); i < n; i++) {
				UserInfo ui = (UserInfo) userinfo.get(i);
				if (ui.userName.equals(DBUser))
					continue;
				chkflag = false;
				for (int i2 = 0, n2 = uiedit.members.size(); i2 < n2; i2++) {
					if (((UserInfo) uiedit.members.get(i2)).userName
							.equals(ui.userName)) {
						chkflag = true;
						break;
					}
				}
				for (int i2 = 0, n2 = uiedit.groups.size(); i2 < n2; i2++) {
					if (((UserInfo) uiedit.groups.get(i2)).userName
							.equals(ui.userName)) {
						chkflag = true;
						break;
					}
				}
				if (chkflag)
					continue;
				TableItem item = new TableItem(LIST_USER_ALLUSERS, SWT.NONE);
				item.setText(0, ui.userName);
			}
			for (int i2 = 0, n2 = uiedit.members.size(); i2 < n2; i2++) {
				TableItem item = new TableItem(LIST_USER_MEMBERS, SWT.NONE);
				item.setText(0, ((UserInfo) uiedit.members.get(i2)).userName);
			}
			
			boolean isHasPublic = false;
			for (int i2 = 0, n2 = uiedit.groups.size(); i2 < n2; i2++) {
				TableItem item = new TableItem(LIST_USER_GROUPS, SWT.NONE);
				String userName = ((UserInfo) uiedit.groups.get(i2)).userName;
				if (userName.equals("public"))
					isHasPublic = true;
				item.setText(0, userName);
			}
			
			if (!isHasPublic && !DBUser.equals("dba") && !DBUser.equals("public")) {
				UserInfo publicUserInfo = UserInfo.UserInfo_find(userinfo, "public");
				uiedit.groups.add(publicUserInfo);
				TableItem item = new TableItem(LIST_USER_GROUPS, SWT.NONE);
				item.setText(0, "public");
				for (int j = 0, n = LIST_USER_ALLUSERS.getItemCount(); j < n; j++) {
					String userName = LIST_USER_ALLUSERS.getItem(j).getText(0);
					if (userName.equals("public")) {
						LIST_USER_ALLUSERS.remove(j);
						break;
					}
				}
			}
		}

		for (int i = 0, n = LIST_USER_GROUPS.getColumnCount(); i < n; i++) {
			LIST_USER_GROUPS.getColumn(i).pack();
		}

		for (int i = 0, n = LIST_USER_ALLUSERS.getColumnCount(); i < n; i++) {
			LIST_USER_ALLUSERS.getColumn(i).pack();
		}

		for (int i = 0, n = LIST_USER_MEMBERS.getColumnCount(); i < n; i++) {
			LIST_USER_MEMBERS.getColumn(i).pack();
		}

		setBtnEnableDisable();
	}

	private void setBtnEnableDisable() {
		BUTTON_ADD_GROUP.setEnabled(false);
		BUTTON_ADD_MEMBER.setEnabled(false);
		BUTTON_REMOVE_GROUP.setEnabled(false);
		BUTTON_REMOVE_MEMBER.setEnabled(false);

		if (LIST_USER_ALLUSERS.getSelectionCount() > 0
				&& LIST_USER_ALLUSERS.isFocusControl()) {
			BUTTON_ADD_GROUP.setEnabled(true);
			BUTTON_ADD_MEMBER.setEnabled(true);
		} else if (LIST_USER_GROUPS.getSelectionCount() > 0
				&& LIST_USER_GROUPS.isFocusControl()) {
			BUTTON_REMOVE_GROUP.setEnabled(true);
		} else if (LIST_USER_MEMBERS.getSelectionCount() > 0
				&& LIST_USER_MEMBERS.isFocusControl()) {
			BUTTON_REMOVE_MEMBER.setEnabled(true);
		}
	}
}
