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

package cubridmanager.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;
import cubridmanager.Messages;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.custom.CLabel;

public class SECURITYDialog extends Dialog {
	private Shell sShell = null;
	private Table LIST_USER_INFO = null;
	private Button IDCANCEL = null;
	private CLabel clabel1 = null;

	public SECURITYDialog(Shell parent) {
		super(parent);
	}

	public SECURITYDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		sShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setText(Messages.getString("TITLE.SECURITYDIALOG"));
		sShell.setSize(new org.eclipse.swt.graphics.Point(821, 374));
		createTable1();
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setBounds(new org.eclipse.swt.graphics.Rectangle(689, 323, 54, 23));
		clabel1 = new CLabel(sShell, SWT.LEFT);
		clabel1.setText(Messages.getString("LABEL.RIGHTCLICKTO"));
		clabel1.setBounds(new org.eclipse.swt.graphics.Rectangle(12, 326, 601, 19));
	}

	private void createTable1() {
		LIST_USER_INFO = new Table(sShell, SWT.FULL_SELECTION | SWT.SINGLE
				| SWT.BORDER);
		LIST_USER_INFO.setBounds(new org.eclipse.swt.graphics.Rectangle(6, 4, 803, 315));
		LIST_USER_INFO.setLinesVisible(true);
		LIST_USER_INFO.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST_USER_INFO.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_USER_INFO, SWT.LEFT);
		tblcol.setText("col1");
		tblcol = new TableColumn(LIST_USER_INFO, SWT.LEFT);
		tblcol.setText("col2");

		TableItem item = new TableItem(LIST_USER_INFO, SWT.NONE);
		item.setText(0, "dat1");
		item.setText(1, "dat2");
		for (int i = 0, n = LIST_USER_INFO.getColumnCount(); i < n; i++) {
			LIST_USER_INFO.getColumn(i).pack();
		}
	}

}
