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

import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.MainConstants;
import cubridmanager.Messages;
import cubridmanager.CommonTool;
import cubridmanager.VerifyDigitListener;
import cubridmanager.cubrid.ParameterItem;
import cubridmanager.cubrid.view.CubridView;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class EDIT_SQLXINITDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Group group1 = null;
	private Label label2 = null;
	private Text EDIT_SQLXINIT_EDIT_NAME = null;
	private Label label3 = null;
	private Combo COMBO_SQLXINIT_VALUE = null;
	private Text EDIT_SQLXINIT_VALUE = null;
	private Label label4 = null;
	private Text EDIT_SQLXINIT_DESC = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private String Name = null;
	private String Value = null;
	private String NewValue;
	private String errmsg;
	private CLabel cLabel = null;
	private GridData gridData39 = new org.eclipse.swt.layout.GridData();
	private GridData gridDatacom = new org.eclipse.swt.layout.GridData();
	private ParameterItem currParamItem;

	public EDIT_SQLXINITDialog(Shell parent) {
		super(parent);
	}

	public EDIT_SQLXINITDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		if (SqlxinitEditor.Current_select.length() <= 0)
			return 0;
		createSShell();
		dlgShell.setDefaultButton(IDOK);

		Name = SqlxinitEditor.Current_row.getText(0);
		Value = SqlxinitEditor.Current_row.getText(1);
		currParamItem = (ParameterItem) SqlxinitEditor.Current_row.getData();

		setinfo();
		CommonTool.centerShell(dlgShell);
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.EDIT_SQLXINITDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData41 = new org.eclipse.swt.layout.GridData();
		gridData41.horizontalSpan = 3;
		GridData gridData40 = new org.eclipse.swt.layout.GridData();
		gridData40.horizontalSpan = 3;
		gridData40.widthHint = 380;
		gridData40.heightHint = 200;
		gridData39.widthHint = 300;
		GridData gridData38 = new org.eclipse.swt.layout.GridData();
		gridData38.horizontalSpan = 2;
		gridData38.widthHint = 300;
		GridLayout gridLayout37 = new GridLayout();
		gridLayout37.numColumns = 3;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 75;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData3.widthHint = 75;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.grabExcessHorizontalSpace = true;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 3;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.EDITSYSTEMPARAMETER"));
		label1.setLayoutData(gridData);
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayoutData(gridData1);
		group1.setLayout(gridLayout37);
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.NAME"));
		EDIT_SQLXINIT_EDIT_NAME = new Text(group1, SWT.NONE);
		EDIT_SQLXINIT_EDIT_NAME.setEditable(false);
		EDIT_SQLXINIT_EDIT_NAME.setLayoutData(gridData38);
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.VALUE1"));
		EDIT_SQLXINIT_VALUE = new Text(group1, SWT.BORDER);
		EDIT_SQLXINIT_VALUE.setVisible(true);
		EDIT_SQLXINIT_VALUE.setLayoutData(gridData39);
		EDIT_SQLXINIT_VALUE.setEditable(true);
		createCombo1();
		label4 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.DESCRIPTION"));
		label4.setLayoutData(gridData41);
		EDIT_SQLXINIT_DESC = new Text(group1, SWT.BORDER | SWT.MULTI | SWT.WRAP
				| SWT.V_SCROLL);
		EDIT_SQLXINIT_DESC.setEditable(false);
		EDIT_SQLXINIT_DESC.setLayoutData(gridData40);
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData2);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData3);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (EDIT_SQLXINIT_VALUE.isVisible())
							NewValue = EDIT_SQLXINIT_VALUE.getText().trim();
						else
							NewValue = COMBO_SQLXINIT_VALUE.getText();
						if (NewValue.equals(""))
							NewValue = currParamItem.defaultValue;
						if (!NewValue.equals(Value)) {
							if (checkval()) {
								String msg = "dbname:" + CubridView.Current_db
										+ "\n";
								msg += "param:" + Name + "\n";
								msg += "paramval:" + NewValue + "\n";
								ClientSocket cs = new ClientSocket();
								if (!cs
										.SendBackGround(
												dlgShell,
												msg,
												"setsysparam",
												Messages
														.getString("WAITING.CHANGINGSERVERPROPERTY"))) {
									CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
								} else {
									if (ApplicationActionBarAdvisor.Current_auth.status == MainConstants.STATUS_START)
										CommonTool
												.MsgBox(
														dlgShell,
														Messages
																.getString("MSG.SUCCESS"),
														Messages
																.getString("MSG.SETSERVERSUCCESS"));
									SqlxinitEditor.Current_row.setText(1,
											NewValue);
									dlgShell.dispose();
								}
							} else
								CommonTool.ErrorBox(dlgShell, errmsg);
						} else
							dlgShell.dispose();
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData4);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
	}

	private void createCombo1() {
		gridDatacom.widthHint = 0;
		COMBO_SQLXINIT_VALUE = new Combo(group1, SWT.DROP_DOWN | SWT.READ_ONLY);
		COMBO_SQLXINIT_VALUE.setVisible(true);
		COMBO_SQLXINIT_VALUE.setLayoutData(gridDatacom);
	}

	private void setinfo() {
		String tmpmsg = currParamItem.desc;
		if (tmpmsg.startsWith("!"))
			tmpmsg = "";
		EDIT_SQLXINIT_EDIT_NAME.setText(Name);

		EDIT_SQLXINIT_DESC.setText(tmpmsg);
		if (Name.equals("isolation_level")) {
			gridData39.widthHint = 0;
			gridDatacom.widthHint = 300;
			COMBO_SQLXINIT_VALUE.setEnabled(true);
			COMBO_SQLXINIT_VALUE.setVisible(true);
			COMBO_SQLXINIT_VALUE.add("\"TRAN_REP_CLASS_REP_INSTANCE\"");
			COMBO_SQLXINIT_VALUE.add("\"TRAN_REP_READ\"");
			COMBO_SQLXINIT_VALUE.add("\"TRAN_SERIALIZABLE\"");
			COMBO_SQLXINIT_VALUE.add("\"TRAN_REP_CLASS_COMMIT_INSTANCE\"");
			COMBO_SQLXINIT_VALUE.add("\"TRAN_READ_COMMITTED\"");
			COMBO_SQLXINIT_VALUE.add("\"TRAN_CURSOR_STABILITY\"");
			COMBO_SQLXINIT_VALUE.add("\"TRAN_READ_UNCOMMITTED\"");
			COMBO_SQLXINIT_VALUE.add("\"TRAN_REP_CLASS_UNCOMMIT_INSTANCE\"");
			COMBO_SQLXINIT_VALUE.add("\"TRAN_COMMIT_CLASS_COMMIT_INSTANCE\"");
			COMBO_SQLXINIT_VALUE.add("\"TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE\"");
			COMBO_SQLXINIT_VALUE.select(0);
			for (int i = 0, n = COMBO_SQLXINIT_VALUE.getItemCount(); i < n; i++) {
				if (COMBO_SQLXINIT_VALUE.getItem(i).equals(Value)) {
					COMBO_SQLXINIT_VALUE.select(i);
					break;
				}
			}
			EDIT_SQLXINIT_VALUE.setEnabled(false);
			EDIT_SQLXINIT_VALUE.setVisible(false);
			COMBO_SQLXINIT_VALUE.setFocus();
			dlgShell.pack();
		} else {
			gridData39.widthHint = 300;
			gridDatacom.widthHint = 0;
			COMBO_SQLXINIT_VALUE.setEnabled(false);
			COMBO_SQLXINIT_VALUE.setVisible(false);
			EDIT_SQLXINIT_VALUE.setEnabled(true);
			EDIT_SQLXINIT_VALUE.setVisible(true);
			EDIT_SQLXINIT_VALUE.setText(Value);
			EDIT_SQLXINIT_VALUE.setFocus();
			EDIT_SQLXINIT_VALUE.addListener(SWT.Verify,
					new VerifyDigitListener(currParamItem.type));
			dlgShell.pack();
		}
	}

	private boolean checkval() {
		boolean ret = currParamItem.checkValue(NewValue);
		errmsg = currParamItem.getErrmsg();
		return ret;
	}
}
