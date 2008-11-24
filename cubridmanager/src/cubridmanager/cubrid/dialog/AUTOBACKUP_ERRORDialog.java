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
import cubridmanager.MainRegistry;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class AUTOBACKUP_ERRORDialog extends Dialog {
	private Shell dlgShell = null; // @jve:decl-index=0:visual-constraint="10,51"
	private Composite sShell = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private Table LIST_AUTO_BACKUP_ERROR_LOG = null;
	private Group group1 = null;
	private Button BUTTON_AUTO_BACKUP_ERROR_LOG_REFRESH = null;
	private CLabel cLabel = null;

	public AUTOBACKUP_ERRORDialog(Shell parent) {
		super(parent);
	}

	public AUTOBACKUP_ERRORDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
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
		return 0;
	}

	private void createSShell() {
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.AUTOBACKUP_ERRORDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData4.widthHint = 100;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData3.widthHint = 100;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData2.widthHint = 100;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessHorizontalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 4;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.heightHint = -1;
		gridData.widthHint = -1;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.LOGS"));
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData);
		createTable1();
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData1);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData2);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
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
						dlgShell.dispose();
					}
				});
		BUTTON_AUTO_BACKUP_ERROR_LOG_REFRESH = new Button(sShell, SWT.NONE);
		BUTTON_AUTO_BACKUP_ERROR_LOG_REFRESH.setText(Messages
				.getString("BUTTON.REFRESH"));
		BUTTON_AUTO_BACKUP_ERROR_LOG_REFRESH.setLayoutData(gridData4);
		BUTTON_AUTO_BACKUP_ERROR_LOG_REFRESH
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setinfo();
					}
				});
		dlgShell.pack();
	}

	private void createTable1() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.heightHint = 240;
		gridData5.widthHint = 490;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST_AUTO_BACKUP_ERROR_LOG = new Table(group1, SWT.FULL_SELECTION
				| SWT.BORDER);
		LIST_AUTO_BACKUP_ERROR_LOG.setLinesVisible(true);
		LIST_AUTO_BACKUP_ERROR_LOG.setLayoutData(gridData5);
		LIST_AUTO_BACKUP_ERROR_LOG.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		LIST_AUTO_BACKUP_ERROR_LOG.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_AUTO_BACKUP_ERROR_LOG,
				SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.DATABASE"));
		tblcol = new TableColumn(LIST_AUTO_BACKUP_ERROR_LOG, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.BACKUPID"));
		tblcol = new TableColumn(LIST_AUTO_BACKUP_ERROR_LOG, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.ERRORTIME"));
		tblcol = new TableColumn(LIST_AUTO_BACKUP_ERROR_LOG, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.ERRORDESC"));
	}

	private void setinfo() {
		LIST_AUTO_BACKUP_ERROR_LOG.removeAll();
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(dlgShell, "", "getautobackupdberrlog", Messages
				.getString("WAITING.GETTINGLOGINFORM"))) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			return;
		}
		for (int i = 0, n = MainRegistry.Tmpchkrst.size(); i < n; i += 4) {
			TableItem item = new TableItem(LIST_AUTO_BACKUP_ERROR_LOG, SWT.NONE);
			item.setText(0, (String) MainRegistry.Tmpchkrst.get(i));
			item.setText(1, (String) MainRegistry.Tmpchkrst.get(i + 1));
			item.setText(2, (String) MainRegistry.Tmpchkrst.get(i + 2));
			item.setText(3, (String) MainRegistry.Tmpchkrst.get(i + 3));
		}

		for (int i = 0, n = LIST_AUTO_BACKUP_ERROR_LOG.getColumnCount(); i < n; i++) {
			LIST_AUTO_BACKUP_ERROR_LOG.getColumn(i).pack();
		}
	}
}
