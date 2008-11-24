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

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.*;

import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.action.ManagerLogAction;
import cubridmanager.cubrid.LogFileInfo;
import org.eclipse.swt.widgets.TabFolder;
import java.util.*;
import org.eclipse.swt.layout.FillLayout;

public class DBMT_ACCESS_ERRORDialog extends Dialog {
	private Shell dlgShell = null; // @jve:decl-index=0:visual-constraint="10,61"
	private Composite sShell = null;
	private Composite comparent = null;
	private Table table = null;
	private String dlgtype = null;
	private LogComparator comparator = new LogComparator();

	public DBMT_ACCESS_ERRORDialog(Shell parent, String style) {
		super(parent);
		dlgtype = style;
	}

	public Composite SetTabPart(TabFolder parent) {
		comparent = parent;
		dlgShell = parent.getShell(); // comment out to use VE
		createComposite();
		sShell.setParent(parent);
		return sShell;
	}

	public int doModal() {
		createSShell();
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
		dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		// dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL |
		// SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.DBMT_ACCESS_ERRORDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		sShell = new Composite(comparent, SWT.NONE);
		sShell.setLayout(new FillLayout());
		createTable();
	}

	private void createTable() {
		table = new Table(sShell, SWT.BORDER | SWT.FULL_SELECTION);
		table.setHeaderVisible(true);
		table.setLinesVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(100, 100, false));
		tlayout.addColumnData(new ColumnWeightData(100, 100, false));
		tlayout.addColumnData(new ColumnWeightData(100, 100, false));
		if (dlgtype.equals("error"))
			tlayout.addColumnData(new ColumnWeightData(100, 100, false));
		table.setLayout(tlayout);

		TableColumn col = new TableColumn(table, SWT.LEFT);
		col.setText(Messages.getString("TABLE.USER"));
		col.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent event) {
				comparator.setColumn(0);
				comparator.reverseDirection();
				updateTable();
			}
		});

		col = new TableColumn(table, SWT.LEFT);
		col.setText(Messages.getString("TABLE.TASKNAME")); //$NON-NLS-1$
		col.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent event) {
				comparator.setColumn(1);
				comparator.reverseDirection();
				updateTable();
			}
		});

		col = new TableColumn(table, SWT.LEFT);
		col.setText(Messages.getString("TABLE.TIME")); //$NON-NLS-1$
		col.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent event) {
				comparator.setColumn(2);
				comparator.reverseDirection();
				updateTable();
			}
		});
		if (dlgtype.equals("error")) {
			col = new TableColumn(table, SWT.LEFT);
			col.setText(Messages.getString("TABLE.DESCRIPTION"));
			col.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent event) {
					comparator.setColumn(3);
					comparator.reverseDirection();
					updateTable();
				}
			});
		}
		updateTable();
	}

	public void updateTable() {
		table.setRedraw(false);

		table.removeAll();
		if (dlgtype.equals("error")) {
			if (ManagerLogAction.Errorlog.size() > 1)
				Collections.sort(ManagerLogAction.Errorlog, comparator);
			for (Iterator itr = ManagerLogAction.Errorlog.iterator(); itr
					.hasNext();) {
				LogFileInfo lfi = (LogFileInfo) itr.next();
				TableItem item = new TableItem(table, SWT.NONE);
				item.setText(0, lfi.filename);
				item.setText(1, lfi.fileowner);
				item.setText(2, lfi.size);
				item.setText(3, lfi.date);
			}
		} else {
			if (ManagerLogAction.Accesslog.size() > 1)
				Collections.sort(ManagerLogAction.Accesslog, comparator);
			for (Iterator itr = ManagerLogAction.Accesslog.iterator(); itr
					.hasNext();) {
				LogFileInfo lfi = (LogFileInfo) itr.next();
				TableItem item = new TableItem(table, SWT.NONE);
				item.setText(0, lfi.filename);
				item.setText(1, lfi.fileowner);
				item.setText(2, lfi.size);
			}
		}
		table.setRedraw(true);

		for (int i = 0, n = table.getColumnCount(); i < n; i++) {
			table.getColumn(i).pack();
		}

	}

}

class LogComparator implements Comparator {
	private int column = 2; // time

	private int direction = 0; // asc

	public int compare(Object obj1, Object obj2) {
		int rc = 0;
		LogFileInfo p1 = (LogFileInfo) obj1;
		LogFileInfo p2 = (LogFileInfo) obj2;

		switch (column) {
		case 0:
			rc = p1.filename.compareTo(p2.filename);
			break;
		case 1:
			rc = p1.fileowner.compareTo(p2.fileowner);
			break;
		case 2:
			rc = p1.size.compareTo(p2.size);
			break;
		case 3:
			rc = p1.date.compareTo(p2.date);
			break;
		}

		if (direction == 1) {
			rc = -rc;
		}
		return rc;
	}

	public void setColumn(int column) {
		this.column = column;
	}

	public void setDirection(int direction) {
		this.direction = direction;
	}

	public void reverseDirection() {
		direction = 1 - direction;
	}
}
