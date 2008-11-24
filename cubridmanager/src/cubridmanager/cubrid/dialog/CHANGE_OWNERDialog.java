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

import java.util.ArrayList;

import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.cubrid.UserInfo;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.dialog.PROPPAGE_CLASS_PAGE1Dialog;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Combo;

public class CHANGE_OWNERDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Label label1 = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;
	private Combo cmbOwner = null;

	public CHANGE_OWNERDialog(Shell parent) {
		super(parent);
	}

	public CHANGE_OWNERDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

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
		dlgShell.setText(Messages.getString("TITLE.CHANGEOWNER"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {

		GridLayout gridLayout = new GridLayout();
		gridLayout.makeColumnsEqualWidth = true;
		gridLayout.numColumns = 2;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);

		createGroup1();

		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.widthHint = 75;
		gridData2.horizontalAlignment = GridData.END;
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData2);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (cmbOwner.getText().length() <= 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.EMPTYOWNER"));
							cmbOwner.setFocus();
							return;
						}
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "classname:"
								+ PROPPAGE_CLASS_PAGE1Dialog.si.name + "\n";
						msg += "ownername:" + cmbOwner.getText();

						ClientSocket cs = new ClientSocket();

						if (!cs.SendBackGround(dlgShell, msg, "changeowner",
								Messages.getString("TITLE.CHANGEOWNER"))) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}

						ret = true;
						dlgShell.dispose();
					}
				});

		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.grabExcessHorizontalSpace = true;
		gridData3.widthHint = 75;
		gridData3.horizontalAlignment = GridData.BEGINNING;
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
		dlgShell.pack(true);
	}

	private void createGroup1() {
		GridData gridGroup = new GridData(GridData.FILL_BOTH);
		gridGroup.widthHint = 200;
		gridGroup.horizontalSpan = 2;
		GridLayout layoutGroup = new GridLayout();
		layoutGroup.numColumns = 2;
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayout(layoutGroup);
		group1.setLayoutData(gridGroup);

		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.NEWOWNER"));
		label1.setAlignment(SWT.CENTER);

		createCmbOwner();
	}

	/**
	 * This method initializes cmbOwner
	 * 
	 */
	private void createCmbOwner() {
		cmbOwner = new Combo(group1, SWT.NONE);

		ArrayList userinfo = UserInfo.UserInfo_get(CubridView.Current_db);
		UserInfo ui;
		for (int i = 0, n = userinfo.size(); i < n; i++) {
			ui = (UserInfo) userinfo.get(i);
			if (!PROPPAGE_CLASS_PAGE1Dialog.si.schemaowner
					.equalsIgnoreCase(ui.userName))
				cmbOwner.add(ui.userName.toUpperCase());
		}
		cmbOwner.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
	}
}
