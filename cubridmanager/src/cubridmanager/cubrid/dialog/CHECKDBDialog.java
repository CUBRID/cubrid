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
import cubridmanager.cubrid.action.CheckAction;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class CHECKDBDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Label label1 = null;
	private Label label2 = null;
	private Text EDIT_CHECKDB_DBNAME = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;

	public CHECKDBDialog(Shell parent) {
		super(parent);
	}

	public CHECKDBDialog(Shell parent, int style) {
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
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.CHECKDBDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		gridLayout.makeColumnsEqualWidth = true;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);

		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.widthHint = 220;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData);

		GridData gridData52 = new org.eclipse.swt.layout.GridData(
				GridData.FILL_BOTH);
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.VERIFYADATABASE"));
		label1.setLayoutData(gridData52);

		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		label2 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.DATABASENAME1"));
		label2.setLayoutData(gridData1);

		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 140;
		EDIT_CHECKDB_DBNAME = new Text(sShell, SWT.BORDER);
		EDIT_CHECKDB_DBNAME.setEditable(false);
		EDIT_CHECKDB_DBNAME.setLayoutData(gridData2);

		GridData gridData49 = new org.eclipse.swt.layout.GridData();
		gridData49.widthHint = 75;
		gridData49.horizontalAlignment = SWT.END;
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData49);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ClientSocket cs = new ClientSocket();
						if (cs.SendBackGround(dlgShell, "dbname:"
								+ CheckAction.ai.dbname, "checkdb", Messages
								.getString("WAITING.CHECKDB"))) {
							ret = true;
							dlgShell.dispose();
						} else
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
					}
				});

		GridData gridData50 = new org.eclipse.swt.layout.GridData();
		gridData50.widthHint = 75;
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData50);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
		setinfo();
	}

	private void setinfo() {
		EDIT_CHECKDB_DBNAME.setText(CheckAction.ai.dbname);
	}

}
