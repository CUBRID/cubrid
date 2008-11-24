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

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.WaitingMsgBox;
import cubridmanager.Messages;
import cubridmanager.cubrid.BackupInfo;
import cubridmanager.cubrid.action.BackupAction;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Spinner;

public class BACKUPDBDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Text EDIT_BACKUPDB_DBNAME = null;
	private Group group1 = null;
	private Table LIST_BACKUPDB_BACKUPINFO = null;
	private Label label2 = null;
	private Label label3 = null;
	private Label label4 = null;
	private Text EDIT_BACKUPDB_VOLNAME = null;
	private Label label5 = null;
	private Combo COMBO_BACKUPDB_LEVEL = null;
	private Label label6 = null;
	private Text EDIT_BACKUPDB_DIR = null;
	private Button CHECK_BACKUPDB_REMOVELOG = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;
	private CLabel cLabel = null;
	private Button CHECK_CHECKING_CONSISTENCY = null;
	private Composite cmpCompressOptionArea = null;
	private Label lblReadThreadAllowed = null;
	private Spinner spnReadThreadAllowed = null;
	private Button chkUseCompress = null;

	public BACKUPDBDialog(Shell parent) {
		super(parent);
	}

	public BACKUPDBDialog(Shell parent, int style) {
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
		dlgShell = new Shell(super.getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.BACKUPDBDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData47 = new org.eclipse.swt.layout.GridData();
		gridData47.horizontalSpan = 2;
		gridData47.widthHint = 150;
		GridLayout gridLayout46 = new GridLayout();
		gridLayout46.numColumns = 3;
		GridData gridData44 = new org.eclipse.swt.layout.GridData();
		gridData44.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData44.widthHint = 80;
		gridData44.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData43 = new org.eclipse.swt.layout.GridData();
		gridData43.widthHint = 80;
		gridData43.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData43.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData42 = new org.eclipse.swt.layout.GridData();
		gridData42.horizontalSpan = 2;
		gridData42.grabExcessHorizontalSpace = true;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalSpan = 3;
		gridData5.widthHint = 376;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 158;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 4;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		gridData1.widthHint = 170;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData.horizontalSpan = 1;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.DATABASENAME1"));
		label1.setLayoutData(gridData);
		EDIT_BACKUPDB_DBNAME = new Text(sShell, SWT.BORDER);
		EDIT_BACKUPDB_DBNAME.setEditable(false);
		EDIT_BACKUPDB_DBNAME.setLayoutData(gridData1);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.PREVIOUSBACKUP"));
		group1.setLayoutData(gridData2);
		group1.setLayout(gridLayout46);
		createTable1();
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		// label2.setText(Messages.getString("LABEL.FREEDISKSPACE"));
		label2.setText("");
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setLayoutData(gridData47);
		label4 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.VOLUMENAME"));
		EDIT_BACKUPDB_VOLNAME = new Text(sShell, SWT.BORDER);
		EDIT_BACKUPDB_VOLNAME.setLayoutData(gridData3);
		label5 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.BACKUPLEVEL"));
		createCombo1();
		label6 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.BACKUPDIRECTORY"));
		EDIT_BACKUPDB_DIR = new Text(sShell, SWT.BORDER);
		EDIT_BACKUPDB_DIR.setLayoutData(gridData5);
		createCmpCompressOptionArea();
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData42);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData43);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String Volname = EDIT_BACKUPDB_VOLNAME.getText();
						String Volpath = EDIT_BACKUPDB_DIR.getText();
						if (COMBO_BACKUPDB_LEVEL.getSelectionIndex() <= 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.SELECTBACKUPLEVEL"));
							return;
						}
						if (Volname == null || Volname.length() <= 0
								|| Volname.indexOf(" ") >= 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INVALIDVOLUMENAME"));
							return;
						}
						if (Volpath == null || Volpath.length() <= 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.NOBACKUPDIR"));
							return;
						}
						/*
						 * if
						 * ((CommonTool.atof(MainRegistry.TmpVolsize)/(1024*1024)) >
						 * CommonTool.atof(BackupAction.free_space)) {
						 * CommonTool.ErrorBox(dlgShell,Messages.getString("ERROR.NOTENOUGHSPACE"));
						 * return; } if
						 * (((CommonTool.atof(MainRegistry.TmpVolsize)/(1024*1024))*1.1) >
						 * CommonTool.atof(BackupAction.free_space)) { if
						 * (CommonTool.WarnYesNo(dlgShell,Messages.getString("WARNYESNO.BACKUPDBSPACEOVER"))
						 * !=SWT.YES) return; }
						 */
						if (CommonTool.WarnYesNo(dlgShell, Messages
								.getString("WARNYESNO.BACKUPDB")
								+ " " + EDIT_BACKUPDB_DBNAME.getText()) == SWT.YES) {
							ClientSocket cs = new ClientSocket();
							if (!CheckDirs(cs)) {
								CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
								return;
							}
							dlgShell.update();
							if (MainRegistry.Tmpchkrst.size() > 0) { 
								NEWDIRECTORYDialog newdlg = new NEWDIRECTORYDialog(
										dlgShell);
								if (newdlg.doModal() == 0)
									return; // cancel
							}
							cs = new ClientSocket();
							if (!CheckFiles(cs)) {
								CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
								return;
							}
							dlgShell.update();
							if (MainRegistry.Tmpchkrst.size() > 0) { 
								CHECKFILEDialog newdlg = new CHECKFILEDialog(
										dlgShell);
								if (newdlg.doModal() == 0)
									return;
							}
							cs = new ClientSocket();
							if (!SendBackup(cs)) {
								CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
								return;
							}
							ret = true;
							dlgShell.dispose();
						}
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData44);
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

	private void createCombo1() {
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 118;
		COMBO_BACKUPDB_LEVEL = new Combo(sShell, SWT.DROP_DOWN | SWT.READ_ONLY);
		COMBO_BACKUPDB_LEVEL.setLayoutData(gridData4);
		COMBO_BACKUPDB_LEVEL
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						if (COMBO_BACKUPDB_LEVEL.getSelectionIndex() > 0) {
							EDIT_BACKUPDB_VOLNAME
									.setText(EDIT_BACKUPDB_DBNAME.getText()
											+ "_backup_lv"
											+ (COMBO_BACKUPDB_LEVEL
													.getSelectionIndex() - 1));
							IDOK.setEnabled(true);
						} else {
							EDIT_BACKUPDB_VOLNAME.setText("");
							IDOK.setEnabled(false);
						}
					}
				});
	}

	private void createTable1() {
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData7.horizontalSpan = 3;
		gridData7.heightHint = 114;
		gridData7.widthHint = 490;
		gridData7.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST_BACKUPDB_BACKUPINFO = new Table(group1, SWT.FULL_SELECTION
				| SWT.SINGLE | SWT.BORDER);
		LIST_BACKUPDB_BACKUPINFO.setLinesVisible(true);
		LIST_BACKUPDB_BACKUPINFO.setLayoutData(gridData7);
		LIST_BACKUPDB_BACKUPINFO.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 100, true));
		tlayout.addColumnData(new ColumnWeightData(50, 100, true));
		tlayout.addColumnData(new ColumnWeightData(50, 70, true));
		tlayout.addColumnData(new ColumnWeightData(50, 400, true));
		LIST_BACKUPDB_BACKUPINFO.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_BACKUPDB_BACKUPINFO, SWT.LEFT);
		tblcol.setText(Messages.getString("LABEL.BACKUPLEVEL"));
		tblcol = new TableColumn(LIST_BACKUPDB_BACKUPINFO, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.LASTBACKUPDATE"));
		tblcol = new TableColumn(LIST_BACKUPDB_BACKUPINFO, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.SIZE_MB"));
		tblcol = new TableColumn(LIST_BACKUPDB_BACKUPINFO, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.PATH"));
	}

	private void setinfo() {
		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(dlgShell, "dbname:" + BackupAction.ai.dbname,
				"getdbsize")) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			dlgShell.dispose();
			return;
		}
		EDIT_BACKUPDB_DBNAME.setText(BackupAction.ai.dbname);
		BackupInfo bi;
		int levelcnt = 0;
		for (int i = 0, n = BackupAction.backinfo.size(); i < n; i++) {
			bi = (BackupInfo) BackupAction.backinfo.get(i);
			TableItem item = new TableItem(LIST_BACKUPDB_BACKUPINFO, SWT.NONE);
			item.setText(0, bi.Level);
			item.setText(1, bi.Date.substring(0, 10) + " "
					+ bi.Date.substring(11, 13) + ":"
					+ bi.Date.substring(14, 16));
			NumberFormat nf = NumberFormat.getInstance();
			nf.setMaximumFractionDigits(2);
			item
					.setText(2, nf.format(CommonTool.atof(bi.Size)
							/ (1024 * 1024)));
			item.setText(3, bi.Path);
			levelcnt++;
		}
		for (int i = 0, n = LIST_BACKUPDB_BACKUPINFO.getColumnCount(); i < n; i++) {
			LIST_BACKUPDB_BACKUPINFO.getColumn(i).pack();
		}
		COMBO_BACKUPDB_LEVEL.add(Messages.getString("COMBO.SELECTBACKUPLEVEL"));
		if (levelcnt < 3)
			levelcnt++;
		for (int i = 1; i <= levelcnt; i++) {
			COMBO_BACKUPDB_LEVEL.add("Level" + (i - 1), i);
		}
		COMBO_BACKUPDB_LEVEL.select(0);
		EDIT_BACKUPDB_DIR.setText(BackupAction.dbdir);
		label3.setText("");
		// label3.setText(BackupAction.free_space+"(MB)");
		CHECK_CHECKING_CONSISTENCY.setSelection(true);
	}

	private boolean CheckDirs(ClientSocket cs) {
		String requestMsg = "dir:" + EDIT_BACKUPDB_DIR.getText();

		if (cs.Connect()) {
			if (cs.Send(dlgShell, requestMsg, "checkdir")) {
				WaitingMsgBox dlg = new WaitingMsgBox(dlgShell);
				dlg.run(Messages.getString("WAITING.CHECKINGDIRECTORY"));
				if (cs.ErrorMsg != null) {
					return false;
				}
			} else {
				return false;
			}
		} else {
			return false;
		}
		return true;
	}

	private boolean CheckFiles(ClientSocket cs) {
		String requestMsg = "file:" + EDIT_BACKUPDB_DIR.getText() + "/"
				+ EDIT_BACKUPDB_VOLNAME.getText();
		requestMsg = requestMsg.replaceAll("//", "/");
		if (cs.Connect()) {
			if (cs.Send(dlgShell, requestMsg, "checkfile")) {
				WaitingMsgBox dlg = new WaitingMsgBox(dlgShell);
				dlg.run(Messages.getString("WAITING.CHECKINGFILES"));
				if (cs.ErrorMsg != null) {
					return false;
				}
			} else {
				return false;
			}
		} else {
			return false;
		}
		return true;
	}

	private boolean SendBackup(ClientSocket cs) {
		String requestMsg = "";
		requestMsg += "dbname:" + EDIT_BACKUPDB_DBNAME.getText() + "\n";
		requestMsg += "level:" + (COMBO_BACKUPDB_LEVEL.getSelectionIndex() - 1)
				+ "\n";
		requestMsg += "volname:" + EDIT_BACKUPDB_VOLNAME.getText() + "\n";
		requestMsg += "backupdir:" + EDIT_BACKUPDB_DIR.getText() + "\n";

		if (CHECK_BACKUPDB_REMOVELOG.getSelection())
			requestMsg += "removelog:y\n";
		else
			requestMsg += "removelog:n\n";

		if (CHECK_CHECKING_CONSISTENCY.getSelection())
			requestMsg += "check:y\n";
		else
			requestMsg += "check:n\n";

		requestMsg += "mt:" + spnReadThreadAllowed.getDigits() + "\n";
		if (chkUseCompress.getSelection())
			requestMsg += "zip:y\n";
		else
			requestMsg += "zip:n\n";

		if (cs.Connect()) {
			if (cs.Send(dlgShell, requestMsg, "backupdb")) {
				WaitingMsgBox dlg = new WaitingMsgBox(dlgShell);
				dlg.run(Messages.getString("WAITING.BACKUPDB"));
				if (cs.ErrorMsg != null) {
					return false;
				}
			} else {
				return false;
			}
		} else {
			return false;
		}
		return true;
	}

	private void createCmpCompressOptionArea() {
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 3;
		GridData gridData8 = new GridData();
		gridData8.horizontalSpan = 4;
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData8.grabExcessHorizontalSpace = true;
		cmpCompressOptionArea = new Composite(sShell, SWT.NONE);
		cmpCompressOptionArea.setLayoutData(gridData8);
		cmpCompressOptionArea.setLayout(gridLayout1);

		GridData gridData0 = new org.eclipse.swt.layout.GridData();
		gridData0.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData0.horizontalSpan = 2;
		gridData0.grabExcessHorizontalSpace = true;
		CHECK_CHECKING_CONSISTENCY = new Button(cmpCompressOptionArea,
				SWT.CHECK);
		CHECK_CHECKING_CONSISTENCY.setText(Messages
				.getString("CHECK.CHCKINGCONSISTENCY"));
		CHECK_CHECKING_CONSISTENCY.setLayoutData(gridData0);
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.grabExcessHorizontalSpace = true;
		CHECK_BACKUPDB_REMOVELOG = new Button(cmpCompressOptionArea, SWT.CHECK);
		CHECK_BACKUPDB_REMOVELOG.setText(Messages
				.getString("CHECK.REMOVEARCHIVE"));
		CHECK_BACKUPDB_REMOVELOG.setLayoutData(gridData1);

		lblReadThreadAllowed = new Label(cmpCompressOptionArea, SWT.NONE);
		lblReadThreadAllowed.setText(Messages
				.getString("LABEL.READTHREADALLOWED"));

		GridData gridData12 = new GridData();
		gridData12.widthHint = 35;
		gridData12.grabExcessHorizontalSpace = true;
		spnReadThreadAllowed = new Spinner(cmpCompressOptionArea, SWT.BORDER);
		spnReadThreadAllowed.setMinimum(0);
		spnReadThreadAllowed.setLayoutData(gridData12);

		chkUseCompress = new Button(cmpCompressOptionArea, SWT.CHECK);
		chkUseCompress.setText(Messages.getString("CHECK.USECOMPRESS"));
		chkUseCompress.setSelection(true);
	}
}
