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

import java.text.NumberFormat;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.MainConstants;
import cubridmanager.Messages;
import cubridmanager.MainRegistry;
import cubridmanager.CommonTool;
import cubridmanager.cubrid.VolumeInfo;
import cubridmanager.cubrid.action.DeleteAction;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class DELETEDB_CONFIRMDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Text EDIT_DELETEDB_DBNAME = null;
	private Label label2 = null;
	private Table LIST_DELETEDB = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;
	public Button CHECK_DELETE_BACKUP = null;

	public DELETEDB_CONFIRMDialog(Shell parent) {
		super(parent);
	}

	public DELETEDB_CONFIRMDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		if (DeleteAction.ai == null)
			return false;
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
		dlgShell.setText(Messages.getString("TITLE.DELETEDB_CONFIRMDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData23 = new org.eclipse.swt.layout.GridData();
		gridData23.grabExcessHorizontalSpace = true;
		gridData23.widthHint = 100;
		GridData gridData22 = new org.eclipse.swt.layout.GridData();
		gridData22.widthHint = 100;
		gridData22.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData22.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 4;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		gridData1.widthHint = 124;
		gridData1.grabExcessHorizontalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData.horizontalSpan = 2;
		gridData.grabExcessHorizontalSpace = true;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.DATABASENAME1"));
		label1.setLayoutData(gridData);
		EDIT_DELETEDB_DBNAME = new Text(sShell, SWT.BORDER);
		EDIT_DELETEDB_DBNAME.setEditable(false);
		EDIT_DELETEDB_DBNAME.setLayoutData(gridData1);
		label2 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.VOLUMEINFORMATION"));
		label2.setLayoutData(gridData2);
		createTable1();
		GridData gdDeleteBackup = new org.eclipse.swt.layout.GridData();
		gdDeleteBackup.grabExcessHorizontalSpace = true;
		gdDeleteBackup.horizontalAlignment = GridData.FILL;
		gdDeleteBackup.horizontalSpan = 2;
		gdDeleteBackup.horizontalIndent = 20;
		gdDeleteBackup.widthHint = 150;
		CHECK_DELETE_BACKUP = new Button(sShell, SWT.CHECK);
		CHECK_DELETE_BACKUP.setText(Messages.getString("CHECK.DELETEBACKUP"));
		CHECK_DELETE_BACKUP.setToolTipText(Messages
				.getString("TOOLTIP.DELETEBACKUP"));
		CHECK_DELETE_BACKUP.setLayoutData(gdDeleteBackup);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.DELETE"));
		IDOK.setLayoutData(gridData22);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = true;
						DeleteAction.deleteBackup = CHECK_DELETE_BACKUP
								.getSelection();
						dlgShell.dispose();
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData23);
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
		EDIT_DELETEDB_DBNAME.setText(DeleteAction.ai.dbname);

		TableItem item;
		for (int i = 0, n = DeleteAction.ai.Volinfo.size(); i < n; i++) {
			VolumeInfo objrec = (VolumeInfo) DeleteAction.ai.Volinfo.get(i);
			item = new TableItem(LIST_DELETEDB, SWT.NONE);
			item.setText(0, objrec.spacename);
			item.setText(1, objrec.location);
			item.setText(2, objrec.date);
			item.setText(3, objrec.type);
			item.setText(4, objrec.tot);
			item.setText(5, objrec.free);
			double mb = CommonTool.atoi(MainRegistry.DBPARA_PAGESIZE)
					* CommonTool.atoi(objrec.tot)
					/ (double) MainConstants.MEGABYTES;
			NumberFormat nf = NumberFormat.getInstance();
			nf.setMaximumFractionDigits(2);
			nf.setGroupingUsed(false);

			String volsize = nf.format(mb) + " ";
			item.setText(6, volsize);
		}
	}

	private void createTable1() {
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.heightHint = 160;
		gridData3.widthHint = 470;
		gridData3.horizontalSpan = 4;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST_DELETEDB = new Table(sShell, SWT.FULL_SELECTION | SWT.BORDER);
		LIST_DELETEDB.setLinesVisible(true);
		LIST_DELETEDB.setLayoutData(gridData3);
		LIST_DELETEDB.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST_DELETEDB.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_DELETEDB, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.VOLUMENAME"));
		tblcol = new TableColumn(LIST_DELETEDB, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.VOLUMEPATH"));
		tblcol = new TableColumn(LIST_DELETEDB, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.CHANGEDATE"));
		tblcol = new TableColumn(LIST_DELETEDB, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.VOLUMETYPE"));
		tblcol = new TableColumn(LIST_DELETEDB, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.TOTALSIZEPAGES"));
		tblcol = new TableColumn(LIST_DELETEDB, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.REMAINSIZEPAGES"));
		tblcol = new TableColumn(LIST_DELETEDB, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.VOLUMESIZE_MB"));

		for (int i = 0, n = LIST_DELETEDB.getColumnCount(); i < n; i++) {
			LIST_DELETEDB.getColumn(i).pack();
		}
	}

}
