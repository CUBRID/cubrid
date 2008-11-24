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

import cubridmanager.ClientSocket;
import cubridmanager.Messages;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.action.NewClassAction;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class CREATE_CLASSDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Group group1 = null;
	private Label label2 = null;
	private Button RADIO_CREATE_CLASS_GENERAL = null;
	private Button RADIO_CREATE_CLASS_VIRTUAL = null;
	private Label label3 = null;
	private Text EDIT_CREATECLASS_USERNAME = null;
	private Label label4 = null;
	private Text EDIT_CLASS_NEW_NAME = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;
	private AuthItem aurec = null;
	private Label label = null;

	public CREATE_CLASSDialog(Shell parent) {
		super(parent);
	}

	public CREATE_CLASSDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		if (CubridView.Current_db.length() <= 0)
			return false;
		aurec = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (aurec == null)
			return false;
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		setinfo();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void createSShell() {
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.CREATE_CLASSDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.horizontalSpan = 2;
		gridData6.widthHint = 178;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalSpan = 2;
		gridData5.widthHint = 178;
		GridLayout gridLayout4 = new GridLayout();
		gridLayout4.numColumns = 3;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 80;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 80;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.grabExcessHorizontalSpace = true;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayoutData(gridData1);
		group1.setLayout(gridLayout4);
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.CLASSTYPE"));
		RADIO_CREATE_CLASS_GENERAL = new Button(group1, SWT.RADIO);
		RADIO_CREATE_CLASS_GENERAL.setText(Messages.getString("RADIO.NORMAL"));
		RADIO_CREATE_CLASS_VIRTUAL = new Button(group1, SWT.RADIO);
		RADIO_CREATE_CLASS_VIRTUAL.setText(Messages.getString("RADIO.VIEW"));
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.OWNER"));
		EDIT_CREATECLASS_USERNAME = new Text(group1, SWT.BORDER);
		EDIT_CREATECLASS_USERNAME.setLayoutData(gridData5);
		label4 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.CLASSNAME1"));
		EDIT_CLASS_NEW_NAME = new Text(group1, SWT.BORDER);
		EDIT_CLASS_NEW_NAME.setLayoutData(gridData6);

		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 3;
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.CREATEANEWCLASS"));
		label1.setLayoutData(gridData);

		label = new Label(sShell, SWT.NONE);
		label.setLayoutData(gridData11);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData2);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String classname = EDIT_CLASS_NEW_NAME.getText()
								.toLowerCase().trim();
						String username = EDIT_CREATECLASS_USERNAME.getText()
								.trim();
						String retstr = CommonTool
								.ValidateCheckInIdentifier(classname);
						if (retstr.length() > 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INVALIDCLASSNAME"));
							return;
						}
						retstr = CommonTool.ValidateCheckInIdentifier(username);
						if (retstr.length() > 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INVALIDUSERNAME"));
							return;
						}
						String strMsg = "";
						strMsg += "dbname:" + CubridView.Current_db + "\n";
						strMsg += "classname:" + classname;

						if (aurec.dbuser.equals("dba")
								&& !username.equals("dba")) {
							// if dba create class with another user name
							strMsg += "\nusername:" + username + "\n";
						}
						String cmds = "", waitmsg = "";
						if (RADIO_CREATE_CLASS_GENERAL.getSelection()) {
							cmds = "createclass";
							waitmsg = Messages.getString("WAITING.CREATECLASS");
						} else {
							cmds = "createvclass";
							waitmsg = Messages.getString("WAITING.CREATEVIEW");
						}
						ClientSocket cs = new ClientSocket();

						if (!cs.SendBackGround(dlgShell, strMsg, cmds, waitmsg)) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}

						cs = new ClientSocket();
						if (!cs.SendClientMessage(dlgShell, "dbname:"
								+ CubridView.Current_db + "\ndbstatus:on",
								"classinfo")) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}
						NewClassAction.newclass = classname;
						ret = true;
						dlgShell.dispose();
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData3);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	private void setinfo() {
		EDIT_CREATECLASS_USERNAME.setText(aurec.dbuser);
		if (!aurec.dbuser.equals("dba"))
			EDIT_CREATECLASS_USERNAME.setEnabled(false);
		if (CubridView.Current_select.equals("USER_VIEW")) {
			RADIO_CREATE_CLASS_GENERAL.setSelection(false);
			RADIO_CREATE_CLASS_VIRTUAL.setSelection(true);
		} else {
			RADIO_CREATE_CLASS_GENERAL.setSelection(true);
			RADIO_CREATE_CLASS_VIRTUAL.setSelection(false);
		}
	}
}
