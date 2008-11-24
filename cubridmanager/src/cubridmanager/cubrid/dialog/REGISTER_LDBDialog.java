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
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.cubrid.view.CubridView;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Spinner;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class REGISTER_LDBDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Label label1 = null;
	private Text EDIT1 = null;
	private Label label2 = null;
	private Text EDIT2 = null;
	private Group group2 = null;
	private Label label3 = null;
	private Text EDIT3 = null;
	private Label label4 = null;
	private Text EDIT4 = null;
	private Group group3 = null;
	private Button RADIO_UNIX = null;
	private Button RADIO_WINDOWS = null;
	private Label label5 = null;
	private Combo COMBO1 = null;
	private Group group4 = null;
	private Button LDB_USERNAME = null;
	private Label label6 = null;
	private Text REGLDB_USERNAME = null;
	private Label label7 = null;
	private Text REGLDB_PASSWORD = null;
	private Group group5 = null;
	private Label label8 = null;
	private Spinner REGLDB_MAXACTIVE_SPIN = null;
	private Label label9 = null;
	private Spinner REGLDB_MINACTIVE_SPIN = null;
	private Label label10 = null;
	private Spinner REGLDB_DECAYCONST_SPIN = null;
	private Label label11 = null;
	private Combo COMBO2 = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;
	private CLabel cLabel = null;

	public REGISTER_LDBDialog(Shell parent) {
		super(parent);
	}

	public REGISTER_LDBDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
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
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.REGISTER_LDBDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData15 = new org.eclipse.swt.layout.GridData();
		gridData15.widthHint = 90;
		GridData gridData14 = new org.eclipse.swt.layout.GridData();
		gridData14.widthHint = 90;
		GridData gridData13 = new org.eclipse.swt.layout.GridData();
		gridData13.widthHint = 90;
		gridData13.grabExcessHorizontalSpace = true;
		GridLayout gridLayout12 = new GridLayout();
		gridLayout12.numColumns = 3;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.widthHint = 112;
		gridData11.grabExcessHorizontalSpace = true;
		GridData gridData10 = new org.eclipse.swt.layout.GridData();
		gridData10.widthHint = 88;
		gridData10.grabExcessHorizontalSpace = true;
		GridData gridData91 = new org.eclipse.swt.layout.GridData();
		gridData91.horizontalSpan = 4;
		GridLayout gridLayout8 = new GridLayout();
		gridLayout8.numColumns = 4;
		GridData gridData71 = new org.eclipse.swt.layout.GridData();
		gridData71.grabExcessHorizontalSpace = true;
		GridData gridData61 = new org.eclipse.swt.layout.GridData();
		gridData61.widthHint = 300;
		gridData61.grabExcessHorizontalSpace = true;
		GridData gridData51 = new org.eclipse.swt.layout.GridData();
		gridData51.widthHint = 150;
		GridLayout gridLayout4 = new GridLayout();
		gridLayout4.numColumns = 2;
		GridData gridData31 = new org.eclipse.swt.layout.GridData();
		gridData31.widthHint = 100;
		gridData31.grabExcessHorizontalSpace = true;
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.widthHint = 100;
		gridData21.grabExcessHorizontalSpace = true;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 4;
		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.widthHint = 85;
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.widthHint = 85;
		gridData8.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.grabExcessHorizontalSpace = true;
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.horizontalSpan = 3;
		gridData6.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalSpan = 3;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalSpan = 2;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData4.grabExcessVerticalSpace = true;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.verticalSpan = 2;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 3;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.LOCALDATABASE"));
		group1.setLayout(gridLayout1);
		group1.setLayoutData(gridData);
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.HOSTINFORMATION"));
		group2.setLayout(gridLayout4);
		group2.setLayoutData(gridData1);
		group3 = new Group(sShell, SWT.NONE);
		group3.setText(Messages.getString("GROUP.HOSTSYSTEMTYPE"));
		group3.setLayout(new GridLayout());
		group3.setLayoutData(gridData2);
		label5 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.DATABASETYPE"));
		label5.setLayoutData(gridData4);
		createCombo1();

		group4 = new Group(sShell, SWT.NONE);
		group4.setText(Messages.getString("GROUP.AUTHORITY"));
		group4.setLayout(gridLayout8);
		group4.setLayoutData(gridData5);
		group5 = new Group(sShell, SWT.NONE);
		group5.setText(Messages.getString("GROUP.SYSTEMPARAMETER"));
		group5.setLayout(gridLayout12);
		group5.setLayoutData(gridData6);
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.LDBNAME"));
		EDIT1 = new Text(group1, SWT.BORDER);
		EDIT1.setLayoutData(gridData21);
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.NAMEINHOST"));
		EDIT2 = new Text(group1, SWT.BORDER);
		EDIT2.setLayoutData(gridData31);
		label3 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.HOSTADDRESS"));
		EDIT3 = new Text(group2, SWT.BORDER);
		EDIT3.setLayoutData(gridData51);
		label4 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.DIRECTORY"));
		label4.setLayoutData(gridData71);
		EDIT4 = new Text(group2, SWT.BORDER);
		EDIT4.setLayoutData(gridData61);
		RADIO_UNIX = new Button(group3, SWT.RADIO);
		RADIO_UNIX.setText(Messages.getString("RADIO.UNIXORLINUXSYSTEM"));
		RADIO_WINDOWS = new Button(group3, SWT.RADIO);
		RADIO_WINDOWS.setText(Messages.getString("RADIO.MICROSOFT32BIT"));
		LDB_USERNAME = new Button(group4, SWT.CHECK);
		LDB_USERNAME.setText(Messages.getString("CHECK.INSERTUSERNAME"));
		LDB_USERNAME.setLayoutData(gridData91);
		LDB_USERNAME
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (LDB_USERNAME.getSelection()) {
							REGLDB_USERNAME.setEnabled(true);
							REGLDB_PASSWORD.setEnabled(true);
						} else {
							REGLDB_USERNAME.setEnabled(false);
							REGLDB_PASSWORD.setEnabled(false);
						}
					}
				});
		label6 = new Label(group4, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.USERNAME"));
		REGLDB_USERNAME = new Text(group4, SWT.BORDER);
		REGLDB_USERNAME.setEnabled(false);
		REGLDB_USERNAME.setLayoutData(gridData10);
		label7 = new Label(group4, SWT.LEFT | SWT.WRAP);
		label7.setText(Messages.getString("LABEL.PASSWORD1"));
		REGLDB_PASSWORD = new Text(group4, SWT.BORDER | SWT.PASSWORD);
		REGLDB_PASSWORD.setEnabled(false);
		REGLDB_PASSWORD.setLayoutData(gridData11);
		label8 = new Label(group5, SWT.LEFT | SWT.WRAP);
		label8.setText(Messages.getString("LABEL.MAXACTIVE"));
		REGLDB_MAXACTIVE_SPIN = new Spinner(group5, SWT.BORDER);
		REGLDB_MAXACTIVE_SPIN.setMaximum(60000);
		REGLDB_MAXACTIVE_SPIN.setMinimum(1);
		REGLDB_MAXACTIVE_SPIN.setLayoutData(gridData13);
		REGLDB_MAXACTIVE_SPIN.setIncrement(1);
		label11 = new Label(group5, SWT.LEFT | SWT.WRAP);
		label11.setText(Messages.getString("LABEL.OBJECTID"));
		label9 = new Label(group5, SWT.LEFT | SWT.WRAP);
		label9.setText(Messages.getString("LABEL.MINACTIVE"));
		REGLDB_MINACTIVE_SPIN = new Spinner(group5, SWT.BORDER);
		REGLDB_MINACTIVE_SPIN.setMaximum(60000);
		REGLDB_MINACTIVE_SPIN.setLayoutData(gridData14);
		REGLDB_MINACTIVE_SPIN.setIncrement(1);
		createCombo2();
		label10 = new Label(group5, SWT.LEFT | SWT.WRAP);
		label10.setText(Messages.getString("LABEL.DECAYCONSTANT"));
		REGLDB_DECAYCONST_SPIN = new Spinner(group5, SWT.BORDER);
		REGLDB_DECAYCONST_SPIN.setIncrement(1);
		REGLDB_DECAYCONST_SPIN.setMaximum(60000);
		REGLDB_DECAYCONST_SPIN.setLayoutData(gridData15);
		REGLDB_DECAYCONST_SPIN.setMinimum(60);
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData7);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData8);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String ldbname = EDIT1.getText().trim();
						String nameinhost = EDIT2.getText().trim();
						String hostname = EDIT3.getText().trim();
						String direc = EDIT4.getText().trim();
						String dbtype = COMBO1.getText().trim();
						String username = REGLDB_USERNAME.getText().trim();
						String password = REGLDB_PASSWORD.getText().trim();
						int maxact = REGLDB_MAXACTIVE_SPIN.getSelection();
						int minact = REGLDB_MINACTIVE_SPIN.getSelection();
						int decay = REGLDB_DECAYCONST_SPIN.getSelection();
						String objid = COMBO2.getText().trim();
						if (ldbname.length() <= 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INPUTLDBNAME"));
							return;
						}
						if (nameinhost.length() <= 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INPUTDBNAMEINHOST"));
							return;
						}
						if (hostname.length() <= 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INPUTLDBHOSTADDR"));
							return;
						}
						if (maxact < minact) {// max_active < min_active
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INVALIDMAXMINACTIVE"));
							return;
						}

						if (LDB_USERNAME.getSelection()) {
							if (username.length() <= 0) {
								password = "";
							}
						}

						if (RADIO_WINDOWS.getSelection()) {// host is windows machine
							if (dbtype.equals("cubrid"))
								dbtype = "ordb";
							else if (dbtype.equals("oracle"))
								dbtype = "oracle";
						} else if (RADIO_UNIX.getSelection()) { // host is Unix
							if (dbtype.equals("cubrid"))
								dbtype = "cubrid";
							else if (dbtype.equals("oracle"))
								dbtype = "oracle";
						}

						String msg;
						msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "ldbname:" + ldbname + "\n";
						msg += "nameinhost:" + nameinhost + "\n";
						msg += "type:" + dbtype + "\n";
						msg += "hostname:" + hostname + "\n";
						if (LDB_USERNAME.getSelection()) {
							if (username.length() > 0)
								msg += "username:" + username + "\n";
							if (password.length() > 0)
								msg += "password:" + password + "\n";
						}

						msg += "maxactive:" + maxact + "\n";
						msg += "minactive:" + minact + "\n";
						msg += "decayvalue:" + decay + "\n";
						if (direc.length() > 0)
							msg += "directory:" + direc + "\n";
						msg += "objectid:" + objid + "\n";

						ClientSocket cs = new ClientSocket();

						if (!cs.SendBackGround(dlgShell, msg,
								"registerlocaldb", Messages
										.getString("WAITING.ADDLDB"))) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}

						CommonTool.MsgBox(dlgShell, Messages
								.getString("MSG.SUCCESS"), Messages
								.getString("MSG.ADDLDBSUCCESS"));

						dlgShell.update();
						cs = new ClientSocket();
						if (!cs.SendClientMessage(dlgShell, "dbname:"
								+ CubridView.Current_db, "getlocaldbinfo")) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}

						ret = true;
						dlgShell.dispose();
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData9);
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

	private void createCombo1() {
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 130;
		gridData3.horizontalSpan = 2;
		gridData3.grabExcessVerticalSpace = true;
		gridData3.grabExcessHorizontalSpace = true;
		COMBO1 = new Combo(sShell, SWT.DROP_DOWN | SWT.READ_ONLY);
		COMBO1.setLayoutData(gridData3);
	}

	private void createCombo2() {
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.widthHint = 130;
		COMBO2 = new Combo(group5, SWT.DROP_DOWN | SWT.READ_ONLY);
		COMBO2.setLayoutData(gridData12);
	}

	private void setinfo() {
		int default_MinActive = 1;
		int default_MaxActive = 10;
		int default_Decay = 300;

		REGLDB_MAXACTIVE_SPIN.setSelection(default_MaxActive);
		REGLDB_MINACTIVE_SPIN.setSelection(default_MinActive);
		REGLDB_DECAYCONST_SPIN.setSelection(default_Decay);

		COMBO1.add("cubrid", 0);
		COMBO1.add("oracle", 1);
		COMBO1.select(0);

		COMBO2.add("INTRINSIC", 0);
		COMBO2.add("USER DEFINED", 1);
		COMBO2.select(0);

		RADIO_UNIX.setSelection(true);

		EDIT1.setToolTipText(Messages.getString("TOOLTIP.LDBLDBNAME"));
		EDIT2.setToolTipText(Messages.getString("TOOLTIP.LDBEDITNAMEINHOST"));
		EDIT3.setToolTipText(Messages.getString("TOOLTIP.LDBEDITHOSTNAME"));
		EDIT4.setToolTipText(Messages.getString("TOOLTIP.LDBEDITDIRECTORY"));
		COMBO1.setToolTipText(Messages.getString("TOOLTIP.LDBCOMBODBTYPE"));
		REGLDB_USERNAME.setToolTipText(Messages
				.getString("TOOLTIP.LDBEDITUSERNAME"));
		REGLDB_PASSWORD.setToolTipText(Messages
				.getString("TOOLTIP.LDBEDITPASSWORD"));
		REGLDB_MAXACTIVE_SPIN.setToolTipText(Messages
				.getString("TOOLTIP.LDBSPINMAXACTIVE"));
		REGLDB_MINACTIVE_SPIN.setToolTipText(Messages
				.getString("TOOLTIP.LDBSPINMINACTIVE"));
		REGLDB_DECAYCONST_SPIN.setToolTipText(Messages
				.getString("TOOLTIP.LDBSPINDECAY"));
		COMBO2.setToolTipText(Messages.getString("TOOLTIP.LDBCOMBOOBJECTID"));
	}
}
