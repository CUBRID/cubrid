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
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.custom.CLabel;
import cubridmanager.cubrid.LockTran;
import cubridmanager.cubrid.view.CubridView;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class KILLTRAN_CONFIRMDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Button BUTTON_KILL_INDEX = null;
	private Button BUTTON_KILL_USER = null;
	private Button BUTTON_KILL_HOST = null;
	private Button BUTTON_KILL_PROGRAM = null;
	private CLabel clabel1 = null;
	private Button IDCANCEL = null;
	public LockTran worktran = null;
	private boolean ret = false;

	public KILLTRAN_CONFIRMDialog(Shell parent) {
		super(parent);
	}

	public KILLTRAN_CONFIRMDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
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
		dlgShell.setText(Messages.getString("TITLE.KILLTRAN_CONFIRMDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData5.widthHint = 80;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.heightHint = 3;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 450;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 450;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.widthHint = 450;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.widthHint = 450;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(new GridLayout());
		BUTTON_KILL_INDEX = new Button(sShell, SWT.NONE);
		BUTTON_KILL_INDEX.setText(Messages.getString("BUTTON.KILLTHESELECTED"));
		BUTTON_KILL_INDEX.setLayoutData(gridData);
		BUTTON_KILL_INDEX
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "type:t\n";
						msg += "parameter:" + worktran.index + "\n";
						sendkill(Messages
								.getString("WARNYESNO.KILLTRANSACTION"), msg);
					}
				});
		BUTTON_KILL_USER = new Button(sShell, SWT.NONE);
		BUTTON_KILL_USER.setText(Messages
				.getString("BUTTON.KILLALLTRANSACTIONS"));
		BUTTON_KILL_USER.setLayoutData(gridData1);
		BUTTON_KILL_USER
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "type:u\n";
						msg += "parameter:" + worktran.uid + "\n";
						sendkill(Messages
								.getString("WARNYESNO.KILLTRANSACTIONS"), msg);
					}
				});
		BUTTON_KILL_HOST = new Button(sShell, SWT.NONE);
		BUTTON_KILL_HOST.setText(Messages
				.getString("BUTTON.KILLALLTRANSACTIONS1"));
		BUTTON_KILL_HOST.setLayoutData(gridData2);
		BUTTON_KILL_HOST
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "type:h\n";
						msg += "parameter:" + worktran.host + "\n";
						sendkill(Messages
								.getString("WARNYESNO.KILLTRANSACTIONS"), msg);
					}
				});
		BUTTON_KILL_PROGRAM = new Button(sShell, SWT.NONE);
		BUTTON_KILL_PROGRAM.setText(Messages
				.getString("BUTTON.KILLALLTRANSACTIONS2"));
		BUTTON_KILL_PROGRAM.setLayoutData(gridData3);
		BUTTON_KILL_PROGRAM
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "type:pg\n";
						msg += "parameter:" + worktran.pname + "\n";
						sendkill(Messages
								.getString("WARNYESNO.KILLTRANSACTIONS"), msg);
					}
				});
		clabel1 = new CLabel(sShell, SWT.SHADOW_IN);
		clabel1.setLayoutData(gridData4);
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CLOSE"));
		IDCANCEL.setLayoutData(gridData5);
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

	private void sendkill(String warnmsg, String msg) {
		if (CommonTool.WarnYesNo(dlgShell, warnmsg) != SWT.YES)
			return;
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(dlgShell, msg, "killtransaction", Messages
				.getString("WAITING.KILLTRANSACTION"))) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			return;
		}
		dlgShell.update();
		CommonTool.MsgBox(dlgShell, Messages.getString("MSG.SUCCESS"), Messages
				.getString("MSG.KILLTRANSUCCESS"));
		ret = true;
		dlgShell.dispose();
	}

}
