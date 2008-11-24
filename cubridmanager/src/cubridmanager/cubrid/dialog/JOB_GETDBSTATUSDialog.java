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

import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.MainRegistry;
import cubridmanager.cubrid.DBError;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class JOB_GETDBSTATUSDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Button IDOK = null;
	private Table LIST_GETDBSTATUS = null;

	public JOB_GETDBSTATUSDialog(Shell parent) {
		super(parent);
	}

	public JOB_GETDBSTATUSDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		// dlgShell = new Shell(SWT.MODELESS | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.MODELESS | SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.JOB_GETDBSTATUSDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData1.widthHint = 80;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(new GridLayout());
		createTable1();
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.EXIT"));
		IDOK.setLayoutData(gridData1);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	private void createTable1() {
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.heightHint = 290;
		gridData.widthHint = 470;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST_GETDBSTATUS = new Table(sShell, SWT.FULL_SELECTION | SWT.BORDER);
		LIST_GETDBSTATUS.setLinesVisible(true);
		LIST_GETDBSTATUS.setLayoutData(gridData);
		LIST_GETDBSTATUS.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		LIST_GETDBSTATUS.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_GETDBSTATUS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.DATABASE"));
		tblcol = new TableColumn(LIST_GETDBSTATUS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.TIME"));
		tblcol = new TableColumn(LIST_GETDBSTATUS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.ERRORCODE"));
		tblcol = new TableColumn(LIST_GETDBSTATUS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.DESCRIPTION"));

		TableItem item = null;
		for (int i = 0, n = MainRegistry.Tmpary.size(); i < n; i++) {
			DBError dbe = (DBError) MainRegistry.Tmpary.get(i);
			item = new TableItem(LIST_GETDBSTATUS, SWT.NONE);
			item.setText(0, dbe.DbName);
			item.setText(1, dbe.Time);
			item.setText(2, dbe.ErrorCode);
			item.setText(3, dbe.Description);
		}
		for (int i = 0, n = LIST_GETDBSTATUS.getColumnCount(); i < n; i++) {
			LIST_GETDBSTATUS.getColumn(i).pack();
		}
	}

}
