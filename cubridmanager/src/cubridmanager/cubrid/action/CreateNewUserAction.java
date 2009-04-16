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

package cubridmanager.cubrid.action;

import java.util.ArrayList;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.TableItem;

import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.cubrid.UserInfo;
import cubridmanager.MainRegistry;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.dialog.PROPPAGE_USER_GENERALDialog;
import cubridmanager.cubrid.dialog.PROPPAGE_USER_AUTHORIZATIONDialog;

public class CreateNewUserAction extends Action {
	private Shell sShell = null;
	private TabFolder tabFolder = null;
	private Composite composite = null;
	private Composite composite2 = null;
	private Label label1 = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;

	public CreateNewUserAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("CreateNewUserAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		PROPPAGE_USER_GENERALDialog.DBUser = "";
		runpage(true, getText().replaceAll("...", ""));
	}

	public void runpage(final boolean isCreateNew, String title) {
		sShell = new Shell(Application.mainwindow.getShell(),
				SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell.setLayout(gridLayout);
		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(sShell, "dbname:" + CubridView.Current_db
				+ "\ndbstatus:on", "classinfo")) {
			CommonTool.ErrorBox(sShell, cs.ErrorMsg);
			return;
		}

		tabFolder = new TabFolder(sShell, SWT.NONE);
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		tabFolder.setLayoutData(gridData1);
		final boolean dbaflag = (PROPPAGE_USER_GENERALDialog.DBUser
				.toLowerCase().equals("dba")) ? true : false;
		final PROPPAGE_USER_GENERALDialog part1 = new PROPPAGE_USER_GENERALDialog(
				sShell);
		final PROPPAGE_USER_AUTHORIZATIONDialog part2 = new PROPPAGE_USER_AUTHORIZATIONDialog(
				sShell);
		composite = part1.SetTabPart(tabFolder);

		if (!isCreateNew) {
			composite2 = part2.SetTabPart(tabFolder, dbaflag);
		}

		sShell.setText(title);

		TabItem tabItem = new TabItem(tabFolder, SWT.NONE);
		tabItem.setControl(composite);
		tabItem
				.setText(Messages
						.getString("TITLE.PROPPAGE_USER_GENERALDIALOG"));
		if (!isCreateNew) {
			TabItem tabItem2 = new TabItem(tabFolder, SWT.NONE);
			tabItem2.setControl(composite2);
			tabItem2.setText(Messages
					.getString("TITLE.PROPPAGE_USER_AUTHORIZATIONDIALOG"));
		}

		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.grabExcessHorizontalSpace = true;
		label1.setLayoutData(gridData2);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String Useredit = null;
						String Userpass = part1.EDIT_USER_PASSWORD.getText();
						if (!Userpass.equals(part1.EDIT_USER_PASSWORD_CONFIRM
								.getText())) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("WARNING.CONFIRMPASSWD"));
							return;
						}
						boolean newflag = false;
						if (PROPPAGE_USER_GENERALDialog.DBUser.length() <= 0)
							newflag = true;
						ArrayList userinfo = UserInfo
								.UserInfo_get(CubridView.Current_db);
						UserInfo uiedit = null;
						if (newflag) { // new
							Useredit = part1.EDIT_USER_LOGIN.getText();
							if (Useredit == null || Useredit.length() <= 0
									|| Useredit.indexOf(" ") >= 0) {
								CommonTool.ErrorBox(sShell, Messages
										.getString("ERROR.INVALIDUSERNAME"));
								return;
							}
							for (int i = 0, n = userinfo.size(); i < n; i++) {
								UserInfo ui = (UserInfo) userinfo.get(i);
								if (ui.userName.equals(Useredit)) {
									CommonTool
											.ErrorBox(
													sShell,
													Messages
															.getString("ERROR.USERNAMEALREADYEXIST"));
									return;
								}
							}
							if (Userpass != null && Userpass.length() > 8) {
								CommonTool.ErrorBox(sShell, Messages
										.getString("ERROR.PASSWORDLENGTH"));
								return;
							}
						} else {
							Useredit = PROPPAGE_USER_GENERALDialog.DBUser;
							uiedit = UserInfo.UserInfo_find(userinfo, Useredit);
						}
						
						/*
						 * @Wanglei: Check User Name && Pass Can't be user && test
						 * */
						if(Userpass.trim().toLowerCase().indexOf("test")>=0 && Useredit.trim().toLowerCase().indexOf("user")>=0)
						{
							CommonTool.ErrorBox(sShell,Messages
									.getString("ERROR.USERPASSSETTING"));
							return;
						}

						String message = "";

						message += "dbname:" + CubridView.Current_db + "\n";
						message += "username:" + Useredit + "\n";

						if (Userpass.equals("__NULL__")) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.INVALIDNULLPASSWORD"));
							return;
						} else if (newflag || !uiedit.password.equals(Userpass)) {
							if (Userpass == null || Userpass.length() <= 0)
								message += "userpass:__NULL__\n";
							else
								message += "userpass:" + Userpass + "\n";
						}

						message += "open:groups\n";
						for (int i = 0; i < part1.LIST_USER_GROUPS
								.getItemCount(); i++) {
							message += "group:"
									+ part1.LIST_USER_GROUPS.getItem(i)
											.getText(0) + "\n";
						}
						message += "close:groups\n";

						message += "open:addmembers\n";
						for (int i = 0; i < part1.LIST_USER_MEMBERS
								.getItemCount(); i++) {
							String memberName = part1.LIST_USER_MEMBERS
									.getItem(i).getText(0);
							if (!newflag) {
								boolean isNew = true;
								for (int i2 = 0, n2 = uiedit.members.size(); i2 < n2; i2++) {
									if (((UserInfo) uiedit.members.get(i2)).userName
											.equals(memberName)) {
										isNew = false;
										break;
									}
								}
								if (!isNew)
									continue;
							}
							message += "member:" + memberName + "\n";
						}
						message += "close:addmembers\n";

						message += "open:removemembers\n";
						if (!newflag) {
							for (int i2 = 0, n2 = uiedit.members.size(); i2 < n2; i2++) {
								String memberName = ((UserInfo) uiedit.members
										.get(i2)).userName;
								boolean isRemoved = true;
								for (int i = 0; i < part1.LIST_USER_MEMBERS
										.getItemCount(); i++) {
									if (part1.LIST_USER_MEMBERS.getItem(i)
											.getText(0).equals(memberName)) {
										isRemoved = false;
										break;
									}
								}
								if (!isRemoved)
									continue;
								message += "member:" + memberName + "\n";
							}
						}
						message += "close:removemembers\n";

						message += "open:authorization\n";
						if (!isCreateNew && !dbaflag) {
							for (int i = 0; i < part2.LIST_AUTHORIZATIONS
									.getItemCount(); i++) {
								TableItem ti = part2.LIST_AUTHORIZATIONS
										.getItem(i);
								String className = ti.getText(0);

								int authNum = 0;
								for (int j = 1; j < 15; j++) {
									if (ti.getText(j).equals("Y")) {
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
								message += className + ":" + authNum + "\n";
							} // end for list
						} // end if !dbaflag
						message += "close:authorization\n";
						ClientSocket cs = new ClientSocket();
						if (newflag) {
							if (!cs.SendBackGround(sShell, message,
									"createuser", Messages
											.getString("WAITING.NEWUSER"))) {
								CommonTool.ErrorBox(sShell, cs.ErrorMsg);
								return;
							}
						} else {
							if (!cs.SendBackGround(sShell, message,
									"updateuser", Messages
											.getString("WAITING.UPDATEUSER"))) {
								CommonTool.ErrorBox(sShell, cs.ErrorMsg);
								return;
							}
						}
						cs = new ClientSocket();
						if (!cs.SendClientMessage(sShell, "dbname:"
								+ CubridView.Current_db, "userinfo")) {
							CommonTool.ErrorBox(sShell, cs.ErrorMsg);
						}
						// apply change user password 
						MainRegistry.DBUserInfo_update(CubridView.Current_db,
								Useredit, Userpass);
						ArrayList userinfo2 = UserInfo
								.UserInfo_get(CubridView.Current_db);
						for (int i = 0, n = userinfo2.size(); i < n; i++) {
							UserInfo ui = (UserInfo) userinfo2.get(i);
							if (ui.userName.equals(Useredit)) {
								ui.password = Userpass;
							}
						}

						CubridView.myNavi.createModel();
						CubridView.viewer.refresh();

						sShell.dispose();
					}
				});

		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData3.widthHint = 100;
		IDOK.setLayoutData(gridData3);
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 100;
		IDCANCEL.setLayoutData(gridData4);
		if(isCreateNew)
			sShell.setText(Messages.getString("TITLE.ADDNEWUSER"));
		else
			sShell.setText(Messages.getString("TITLE.EDITUSER"));
		sShell.pack();
		CommonTool.centerShell(sShell);
		sShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
	}
}
