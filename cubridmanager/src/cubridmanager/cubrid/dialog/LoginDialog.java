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
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;

import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.DBUserInfo;

public class LoginDialog extends Dialog {

	private Shell sShell = null;
	private Label lblUser = null;
	private Label lblPassword = null;
	private Text txtUser = null;
	private Text txtPassword = null;
	private Composite cmpInputArea = null;
	private Composite cmpButtonsArea = null;
	private Button btnOk = null;
	private Button btnCancel = null;
	private boolean ret = false;
	private AuthItem ai;
	private DBUserInfo ui;
	private static String selectedDatabase = null;

	public LoginDialog(String dbname, Shell parent) {
		this(dbname, parent, SWT.PRIMARY_MODAL);
	}

	public LoginDialog(String dbname, Shell parent, int style) {
		super(parent, style);
		ai = MainRegistry.Authinfo_find(dbname);
		ui = MainRegistry.getDBUserInfo(dbname);
		if (ui == null) {
			ui = new DBUserInfo(dbname, "", "");
			MainRegistry.addDBUserInfo(ui);
		}
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(sShell);
		sShell.setDefaultButton(btnOk);
		sShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}

		return ret;
	}

	/**
	 * This method initializes sShell
	 */
	private void createSShell() {
		sShell = new Shell(Application.mainwindow.getShell(),
				SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setText(Messages.getString("TITLE.LOGIN"));
		sShell.setLayout(new GridLayout());

		createCmpInputArea();
		createCmpButtonsArea();

		sShell.pack();

		fillText();
	}

	/**
	 * This method initializes cmpInputArea
	 * 
	 */
	private void createCmpInputArea() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		gridLayout.marginHeight = 5;
		gridLayout.marginWidth = 5;
		gridLayout.verticalSpacing = 5;
		gridLayout.horizontalSpacing = 0;
		cmpInputArea = new Composite(sShell, SWT.BORDER);
		cmpInputArea.setLayout(gridLayout);
		cmpInputArea.setLayoutData(new GridData());

		GridData gridData = new GridData();
		gridData.widthHint = 70;
		lblUser = new Label(cmpInputArea, SWT.SHADOW_OUT);
		lblUser.setText(Messages.getString("LBL.USERID"));
		lblUser.setLayoutData(gridData);

		GridData gridData1 = new GridData();
		gridData1.widthHint = 130;
		txtUser = new Text(cmpInputArea, SWT.BORDER);
		txtUser.setLayoutData(gridData1);

		GridData gridData11 = new GridData();
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		lblPassword = new Label(cmpInputArea, SWT.NONE);
		lblPassword.setText(Messages.getString("LBL.PASSWORD"));
		lblPassword.setLayoutData(gridData11);

		GridData gridData2 = new GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		txtPassword = new Text(cmpInputArea, SWT.BORDER | SWT.PASSWORD);
		txtPassword.setLayoutData(gridData2);
	}

	/**
	 * This method initializes cmpButtonsArea
	 * 
	 */
	private void createCmpButtonsArea() {
		GridData gridData3 = new GridData();
		gridData3.grabExcessHorizontalSpace = true;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 2;
		gridLayout1.horizontalSpacing = 0;
		gridLayout1.marginHeight = 0;
		gridLayout1.marginWidth = 0;
		gridLayout1.verticalSpacing = 0;
		gridLayout1.makeColumnsEqualWidth = true;
		cmpButtonsArea = new Composite(sShell, SWT.BORDER);
		cmpButtonsArea.setLayout(gridLayout1);
		cmpButtonsArea.setLayoutData(gridData3);

		GridData gridData4 = new GridData();
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData4.grabExcessHorizontalSpace = true;
		btnOk = new Button(cmpButtonsArea, SWT.NONE);
		btnOk.setText(Messages.getString("BUTTON.OK"));
		btnOk.setLayoutData(gridData4);

		btnOk
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ClientSocket cs = new ClientSocket();
						String msg = "targetid:" + MainRegistry.UserID + "\n";
						msg += "dbname:" + ui.dbname + "\n";
						msg += "dbuser:" + txtUser.getText().toUpperCase()
								+ "\n";
						msg += "dbpasswd:" + txtPassword.getText() + "\n";

						if (!cs.SendClientMessage(sShell, msg, "dbmtuserlogin")) {
							CommonTool.ErrorBox(sShell, cs.ErrorMsg);
							return;
						}

						ai.dbuser = txtUser.getText();
						ai.isDBAGroup = ui.isDBAGroup;
						
						if (!ui.dbuser.equals(txtUser.getText())
								|| !ui.dbpassword.equals(txtPassword.getText())) {
							ui.dbuser = txtUser.getText();
							ui.dbpassword = txtPassword.getText();

							selectedDatabase = ui.dbname;
							msg = makeDBMTUserUpdateMessage();

							if (!cs.SendClientMessage(sShell, msg,
									"updatedbmtuser")) {
								CommonTool.ErrorBox(sShell, cs.ErrorMsg);
								return;
							}
						}

						ret = true;
						sShell.dispose();
					}
				});
		GridData gridData5 = new GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		btnCancel = new Button(cmpButtonsArea, SWT.NONE);
		btnCancel.setText(Messages.getString("BUTTON.CANCEL"));
		btnCancel.setLayoutData(gridData5);
		btnCancel
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ai.dbuser = "";
						ret = false;
						sShell.dispose();
					}
				});
	}

	public void fillText() {
		txtUser.setText(ui.dbuser);
		if (txtUser.getText().length() < 1) {
			if (MainRegistry.UserID.equals("admin"))
				txtUser.setText("dba");
			else
				txtUser.setText("public");
		}

		// txtPassword.setText(ui.dbpassword);
		txtPassword.setText("");
	}

	public static String makeDBMTUserUpdateMessage() {
		String msg = "targetid:" + MainRegistry.UserID + "\n";
		for (int i = 0; i < MainRegistry.listDBUserInfo.size(); i++) {
			if ((selectedDatabase != null)
					&& (((DBUserInfo) (MainRegistry.listDBUserInfo.get(i))).dbname
							.equals(selectedDatabase))) {
				msg += "open:dbauth\n";
				msg += "dbname:"
						+ ((DBUserInfo) (MainRegistry.listDBUserInfo.get(i))).dbname
						+ "\n";
				msg += "dbid:"
						+ ((DBUserInfo) (MainRegistry.listDBUserInfo.get(i))).dbuser
						+ "\n";
				msg += "dbpassword:"
						+ ((DBUserInfo) (MainRegistry.listDBUserInfo.get(i))).dbpassword
						+ "\n";
				msg += "close:dbauth\n";
			}
		}
		String userport = (String) MainRegistry.UserPort
				.get(MainRegistry.UserID);
		if (MainRegistry.CASAuth == MainConstants.AUTH_DBA) {
			if (userport.equals("") || userport == null) {
				msg += "casauth:admin\n";
			} else {
				msg += "casauth:admin," + userport + "\n";
			}
		} else if (MainRegistry.CASAuth == MainConstants.AUTH_NONDBA) {
			if (userport.equals("") || userport == null) {
				msg += "casauth:monitor\n";
			} else {
				msg += "casauth:monitor," + userport + "\n";
			}
		} else {
			if (userport.equals("") || userport == null) {
				msg += "casauth:none\n";
			} else {
				msg += "casauth:none," + userport + "\n";
			}
		}

		if (MainRegistry.IsDBAAuth)
			msg += "dbcreate:admin\n";
		else
			msg += "dbcreate:none\n";

		return msg;
	}
}
