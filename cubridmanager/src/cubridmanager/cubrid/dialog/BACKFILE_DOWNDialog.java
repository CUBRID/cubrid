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
import cubridmanager.Messages;
import cubridmanager.CommonTool;
import cubridmanager.cubrid.BackupInfo;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.action.BackupAction;
import cubridmanager.cubrid.action.DownloadFilesAction;
import cubridmanager.cubrid.action.BackupDownThread;
import org.eclipse.swt.widgets.FileDialog;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class BACKFILE_DOWNDialog extends Dialog {
	private Shell dlgShell = null; // @jve:decl-index=0:visual-constraint="10,51"
	private Composite sShell = null;
	private Label label1 = null;
	private Text EDIT_BACKUPFILEDOWN_DBNAME = null;
	private Group group1 = null;
	private Label label2 = null;
	private Label label3 = null;
	private Label label4 = null;
	private Group group2 = null;
	private Button CHECK_FILE_COMPRESS = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private Button CHECK_BACKUPFILESAVE_LV0 = null;
	private Button CHECK_BACKUPFILESAVE_LV1 = null;
	private Button CHECK_BACKUPFILESAVE_LV2 = null;
	private Table LIST_BACKUPFILESOURCE_LV0 = null;
	private Table LIST_BACKUPFILESOURCE_LV1 = null;
	private Table LIST_BACKUPFILESOURCE_LV2 = null;
	private Table LIST_BACKUPFILESAVE_LV0 = null;
	private Table LIST_BACKUPFILESAVE_LV1 = null;
	private Table LIST_BACKUPFILESAVE_LV2 = null;
	private Button BUTTON_FILEDOWN_PATHLV1 = null;
	private Button BUTTON_FILEDOWN_PATHLV0 = null;
	private Button BUTTON_FILEDOWN_PATHLV2 = null;
	private boolean ret = false;
	private CLabel cLabel = null;
	private CLabel cLabel1 = null;

	public BACKFILE_DOWNDialog(Shell parent) {
		super(parent);
	}

	public BACKFILE_DOWNDialog(Shell parent, int style) {
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
		dlgShell.setText(Messages.getString("TITLE.BACKFILE_DOWNDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData41 = new org.eclipse.swt.layout.GridData();
		gridData41.grabExcessHorizontalSpace = true;
		GridData gridData40 = new org.eclipse.swt.layout.GridData();
		gridData40.widthHint = 66;
		GridData gridData39 = new org.eclipse.swt.layout.GridData();
		gridData39.widthHint = 66;
		GridData gridData38 = new org.eclipse.swt.layout.GridData();
		gridData38.widthHint = 66;
		GridLayout gridLayout37 = new GridLayout();
		gridLayout37.numColumns = 2;
		GridData gridData36 = new org.eclipse.swt.layout.GridData();
		gridData36.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData36.verticalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		GridData gridData35 = new org.eclipse.swt.layout.GridData();
		gridData35.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData35.verticalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		GridData gridData34 = new org.eclipse.swt.layout.GridData();
		gridData34.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData34.verticalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		GridLayout gridLayout33 = new GridLayout();
		gridLayout33.numColumns = 2;
		GridData gridData32 = new org.eclipse.swt.layout.GridData();
		gridData32.widthHint = 90;
		gridData32.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData32.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData31 = new org.eclipse.swt.layout.GridData();
		gridData31.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData31.widthHint = 90;
		gridData31.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData30 = new org.eclipse.swt.layout.GridData();
		gridData30.grabExcessHorizontalSpace = true;
		GridData gridData29 = new org.eclipse.swt.layout.GridData();
		gridData29.horizontalSpan = 4;
		GridData gridData28 = new org.eclipse.swt.layout.GridData();
		gridData28.horizontalSpan = 3;
		gridData28.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData28.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		gridData1.widthHint = 150;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.DATABASENAME1"));
		label1.setLayoutData(gridData);
		EDIT_BACKUPFILEDOWN_DBNAME = new Text(sShell, SWT.BORDER);
		EDIT_BACKUPFILEDOWN_DBNAME.setEditable(false);

		EDIT_BACKUPFILEDOWN_DBNAME.setLayoutData(gridData1);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.BACKUPFILEINFORMATION"));
		group1.setLayout(gridLayout33);
		group1.setLayoutData(gridData2);
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.LEVEL0"));
		label2.setLayoutData(gridData36);
		createTable1();
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.LEVEL1"));
		label3.setLayoutData(gridData34);
		createTable2();
		label4 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.LEVEL2"));
		label4.setLayoutData(gridData35);
		createTable3();
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.FILENAMETOSAVE"));
		group2.setLayout(gridLayout37);
		group2.setLayoutData(gridData28);
		CHECK_FILE_COMPRESS = new Button(sShell, SWT.CHECK);
		CHECK_FILE_COMPRESS.setText(Messages
				.getString("CHECK.DOWNLOADCOMPRESS"));
		CHECK_FILE_COMPRESS.setLayoutData(gridData29);
		CHECK_FILE_COMPRESS
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_FILE_COMPRESS.getSelection()) { // *.gz use
							for (int i = 0, n = LIST_BACKUPFILESAVE_LV0
									.getItemCount(); i < n; i++) {
								TableItem ti = LIST_BACKUPFILESAVE_LV0
										.getItem(i);
								String fnm = ti.getText(0);
								if (!fnm.endsWith(".gz"))
									ti.setText(0, fnm + ".gz");
							}
							for (int i = 0, n = LIST_BACKUPFILESAVE_LV1
									.getItemCount(); i < n; i++) {
								TableItem ti = LIST_BACKUPFILESAVE_LV1
										.getItem(i);
								String fnm = ti.getText(0);
								if (!fnm.endsWith(".gz"))
									ti.setText(0, fnm + ".gz");
							}
							for (int i = 0, n = LIST_BACKUPFILESAVE_LV2
									.getItemCount(); i < n; i++) {
								TableItem ti = LIST_BACKUPFILESAVE_LV2
										.getItem(i);
								String fnm = ti.getText(0);
								if (!fnm.endsWith(".gz"))
									ti.setText(0, fnm + ".gz");
							}
						} else {
							for (int i = 0, n = LIST_BACKUPFILESAVE_LV0
									.getItemCount(); i < n; i++) {
								TableItem ti = LIST_BACKUPFILESAVE_LV0
										.getItem(i);
								String fnm = ti.getText(0);
								if (fnm.endsWith(".gz"))
									ti.setText(0, fnm.substring(0,
											fnm.length() - 3));
							}
							for (int i = 0, n = LIST_BACKUPFILESAVE_LV1
									.getItemCount(); i < n; i++) {
								TableItem ti = LIST_BACKUPFILESAVE_LV1
										.getItem(i);
								String fnm = ti.getText(0);
								if (fnm.endsWith(".gz"))
									ti.setText(0, fnm.substring(0,
											fnm.length() - 3));
							}
							for (int i = 0, n = LIST_BACKUPFILESAVE_LV2
									.getItemCount(); i < n; i++) {
								TableItem ti = LIST_BACKUPFILESAVE_LV2
										.getItem(i);
								String fnm = ti.getText(0);
								if (fnm.endsWith(".gz"))
									ti.setText(0, fnm.substring(0,
											fnm.length() - 3));
							}
						}
					}
				});
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData41);
		cLabel1 = new CLabel(sShell, SWT.NONE);
		cLabel1.setLayoutData(gridData30);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData31);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (!CHECK_BACKUPFILESAVE_LV0.getSelection()
								&& !CHECK_BACKUPFILESAVE_LV1.getSelection()
								&& !CHECK_BACKUPFILESAVE_LV2.getSelection()) { // not select
							DownloadFilesAction.backwork = null;
							ret = false;
							dlgShell.dispose();
						}
						DownloadFilesAction.backwork = new BackupDownThread();
						DownloadFilesAction.backwork.compress = CHECK_FILE_COMPRESS
								.getSelection();
						if (CHECK_BACKUPFILESAVE_LV0.getSelection()) {
							for (int i = 0, n = LIST_BACKUPFILESAVE_LV0
									.getItemCount(); i < n; i++) {
								TableItem ti = LIST_BACKUPFILESAVE_LV0
										.getItem(i);
								TableItem ti2 = LIST_BACKUPFILESOURCE_LV0
										.getItem(i);
								if (!FileOpenTest(ti.getText(0))) {
									CommonTool
											.ErrorBox(
													dlgShell,
													Messages
															.getString("ERROR.FILECANNOTCREATE"));
									DownloadFilesAction.backwork = null;
									return;
								}
								DownloadFilesAction.backwork.sfiles.add(ti2
										.getText(0));
								DownloadFilesAction.backwork.dfiles.add(ti
										.getText(0));
							}
						}
						if (CHECK_BACKUPFILESAVE_LV1.getSelection()) {
							for (int i = 0, n = LIST_BACKUPFILESAVE_LV1
									.getItemCount(); i < n; i++) {
								TableItem ti = LIST_BACKUPFILESAVE_LV1
										.getItem(i);
								TableItem ti2 = LIST_BACKUPFILESOURCE_LV1
										.getItem(i);
								if (!FileOpenTest(ti.getText(0))) {
									CommonTool
											.ErrorBox(
													dlgShell,
													Messages
															.getString("ERROR.FILECANNOTCREATE"));
									DownloadFilesAction.backwork = null;
									return;
								}
								DownloadFilesAction.backwork.sfiles.add(ti2
										.getText(0));
								DownloadFilesAction.backwork.dfiles.add(ti
										.getText(0));
							}
						}
						if (CHECK_BACKUPFILESAVE_LV2.getSelection()) {
							for (int i = 0, n = LIST_BACKUPFILESAVE_LV2
									.getItemCount(); i < n; i++) {
								TableItem ti = LIST_BACKUPFILESAVE_LV2
										.getItem(i);
								TableItem ti2 = LIST_BACKUPFILESOURCE_LV2
										.getItem(i);
								if (!FileOpenTest(ti.getText(0))) {
									CommonTool
											.ErrorBox(
													dlgShell,
													Messages
															.getString("ERROR.FILECANNOTCREATE"));
									DownloadFilesAction.backwork = null;
									return;
								}
								DownloadFilesAction.backwork.sfiles.add(ti2
										.getText(0));
								DownloadFilesAction.backwork.dfiles.add(ti
										.getText(0));
							}
						}
						ret = true;
						dlgShell.dispose();
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData32);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});

		CHECK_BACKUPFILESAVE_LV0 = new Button(group2, SWT.CHECK);
		CHECK_BACKUPFILESAVE_LV0.setText(Messages.getString("CHECK.LEVEL0"));
		CHECK_BACKUPFILESAVE_LV0
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_BACKUPFILESAVE_LV0.getSelection()) {
							BUTTON_FILEDOWN_PATHLV0.setEnabled(true);
							LIST_BACKUPFILESAVE_LV0.setEnabled(true);
						} else {
							BUTTON_FILEDOWN_PATHLV0.setEnabled(false);
							LIST_BACKUPFILESAVE_LV0.setEnabled(false);
						}
					}
				});
		createTable4();
		BUTTON_FILEDOWN_PATHLV0 = new Button(group2, SWT.NONE);
		BUTTON_FILEDOWN_PATHLV0.setText(Messages.getString("BUTTON.SETPATH"));
		BUTTON_FILEDOWN_PATHLV0.setLayoutData(gridData38);
		BUTTON_FILEDOWN_PATHLV0
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int selidx = LIST_BACKUPFILESAVE_LV0
								.getSelectionIndex();
						if (selidx < 0)
							selidx = 0; // no select ==> default 0
						TableItem ti = LIST_BACKUPFILESAVE_LV0.getItem(selidx);
						change_filepath(ti);
					}
				});
		CHECK_BACKUPFILESAVE_LV1 = new Button(group2, SWT.CHECK);
		CHECK_BACKUPFILESAVE_LV1.setText(Messages.getString("CHECK.LEVEL1"));
		CHECK_BACKUPFILESAVE_LV1
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_BACKUPFILESAVE_LV1.getSelection()) {
							BUTTON_FILEDOWN_PATHLV1.setEnabled(true);
							LIST_BACKUPFILESAVE_LV1.setEnabled(true);
						} else {
							BUTTON_FILEDOWN_PATHLV1.setEnabled(false);
							LIST_BACKUPFILESAVE_LV1.setEnabled(false);
						}
					}
				});
		createTable5();
		BUTTON_FILEDOWN_PATHLV1 = new Button(group2, SWT.NONE);
		BUTTON_FILEDOWN_PATHLV1.setText(Messages.getString("BUTTON.SETPATH"));
		BUTTON_FILEDOWN_PATHLV1.setLayoutData(gridData39);
		BUTTON_FILEDOWN_PATHLV1
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int selidx = LIST_BACKUPFILESAVE_LV1
								.getSelectionIndex();
						if (selidx < 0)
							selidx = 0; // no select ==> default 0
						TableItem ti = LIST_BACKUPFILESAVE_LV1.getItem(selidx);
						change_filepath(ti);
					}
				});
		CHECK_BACKUPFILESAVE_LV2 = new Button(group2, SWT.CHECK);
		CHECK_BACKUPFILESAVE_LV2.setText(Messages.getString("CHECK.LEVEL2"));
		CHECK_BACKUPFILESAVE_LV2
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_BACKUPFILESAVE_LV2.getSelection()) {
							BUTTON_FILEDOWN_PATHLV2.setEnabled(true);
							LIST_BACKUPFILESAVE_LV2.setEnabled(true);
						} else {
							BUTTON_FILEDOWN_PATHLV2.setEnabled(false);
							LIST_BACKUPFILESAVE_LV2.setEnabled(false);
						}
					}
				});
		createTable6();
		BUTTON_FILEDOWN_PATHLV2 = new Button(group2, SWT.NONE);
		BUTTON_FILEDOWN_PATHLV2.setText(Messages.getString("BUTTON.SETPATH"));
		BUTTON_FILEDOWN_PATHLV2.setLayoutData(gridData40);
		BUTTON_FILEDOWN_PATHLV2
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int selidx = LIST_BACKUPFILESAVE_LV2
								.getSelectionIndex();
						if (selidx < 0)
							selidx = 0; // no select ==> default 0
						TableItem ti = LIST_BACKUPFILESAVE_LV2.getItem(selidx);
						change_filepath(ti);
					}
				});
		dlgShell.pack();
		setinfo();
	}

	private void createTable1() {
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData3.heightHint = 70;
		gridData3.widthHint = 200;
		gridData3.grabExcessVerticalSpace = true;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		LIST_BACKUPFILESOURCE_LV0 = new Table(group1, SWT.FULL_SELECTION
				| SWT.BORDER | SWT.H_SCROLL);
		LIST_BACKUPFILESOURCE_LV0.setLinesVisible(false);
		LIST_BACKUPFILESOURCE_LV0.setLayoutData(gridData3);
		LIST_BACKUPFILESOURCE_LV0.setHeaderVisible(false);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50,
				LIST_BACKUPFILESOURCE_LV0.getBounds().width * 3, true));
		LIST_BACKUPFILESOURCE_LV0.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_BACKUPFILESOURCE_LV0,
				SWT.LEFT);
		tblcol.setText("col1");
	}

	private void createTable2() {
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.heightHint = 70;
		gridData4.grabExcessVerticalSpace = true;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData4.widthHint = 200;
		LIST_BACKUPFILESOURCE_LV1 = new Table(group1, SWT.FULL_SELECTION
				| SWT.BORDER | SWT.H_SCROLL);
		LIST_BACKUPFILESOURCE_LV1.setLinesVisible(false);
		LIST_BACKUPFILESOURCE_LV1.setLayoutData(gridData4);
		LIST_BACKUPFILESOURCE_LV1.setHeaderVisible(false);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50,
				LIST_BACKUPFILESOURCE_LV1.getBounds().width * 3, true));
		LIST_BACKUPFILESOURCE_LV1.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_BACKUPFILESOURCE_LV1,
				SWT.LEFT);
		tblcol.setText("col1");
	}

	private void createTable3() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.heightHint = 70;
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData5.grabExcessVerticalSpace = true;
		gridData5.widthHint = 200;
		LIST_BACKUPFILESOURCE_LV2 = new Table(group1, SWT.FULL_SELECTION
				| SWT.BORDER | SWT.H_SCROLL);
		LIST_BACKUPFILESOURCE_LV2.setLinesVisible(false);
		LIST_BACKUPFILESOURCE_LV2.setLayoutData(gridData5);
		LIST_BACKUPFILESOURCE_LV2.setHeaderVisible(false);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50,
				LIST_BACKUPFILESOURCE_LV2.getBounds().width * 3, true));
		LIST_BACKUPFILESOURCE_LV2.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_BACKUPFILESOURCE_LV2,
				SWT.LEFT);
		tblcol.setText("col1");
	}

	private void createTable4() {
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.verticalSpan = 2;
		gridData6.widthHint = 200;
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData6.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData6.grabExcessVerticalSpace = true;
		gridData6.heightHint = 70;
		LIST_BACKUPFILESAVE_LV0 = new Table(group2, SWT.FULL_SELECTION
				| SWT.BORDER | SWT.H_SCROLL);
		LIST_BACKUPFILESAVE_LV0.setLinesVisible(false);
		LIST_BACKUPFILESAVE_LV0.setLayoutData(gridData6);
		LIST_BACKUPFILESAVE_LV0.setHeaderVisible(false);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, LIST_BACKUPFILESAVE_LV0
				.getBounds().width * 3, true));
		LIST_BACKUPFILESAVE_LV0.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_BACKUPFILESAVE_LV0, SWT.LEFT);
		tblcol.setText("col1");
	}

	private void createTable5() {
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData7.grabExcessVerticalSpace = true;
		gridData7.heightHint = 70;
		gridData7.widthHint = 200;
		gridData7.verticalSpan = 2;
		gridData7.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		LIST_BACKUPFILESAVE_LV1 = new Table(group2, SWT.FULL_SELECTION
				| SWT.BORDER | SWT.H_SCROLL);
		LIST_BACKUPFILESAVE_LV1.setLinesVisible(false);
		LIST_BACKUPFILESAVE_LV1.setLayoutData(gridData7);
		LIST_BACKUPFILESAVE_LV1.setHeaderVisible(false);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, LIST_BACKUPFILESAVE_LV1
				.getBounds().width * 3, true));
		LIST_BACKUPFILESAVE_LV1.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_BACKUPFILESAVE_LV1, SWT.LEFT);
		tblcol.setText("col1");
	}

	private void createTable6() {
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData8.grabExcessHorizontalSpace = false;
		gridData8.grabExcessVerticalSpace = true;
		gridData8.heightHint = 70;
		gridData8.widthHint = 200;
		gridData8.verticalSpan = 2;
		gridData8.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		LIST_BACKUPFILESAVE_LV2 = new Table(group2, SWT.FULL_SELECTION
				| SWT.BORDER | SWT.H_SCROLL);
		LIST_BACKUPFILESAVE_LV2.setLinesVisible(false);
		LIST_BACKUPFILESAVE_LV2.setLayoutData(gridData8);
		LIST_BACKUPFILESAVE_LV2.setHeaderVisible(false);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, LIST_BACKUPFILESAVE_LV2
				.getBounds().width * 3, true));
		LIST_BACKUPFILESAVE_LV2.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_BACKUPFILESAVE_LV2, SWT.LEFT);
		tblcol.setText("col1");
	}

	private void setinfo() {
		EDIT_BACKUPFILEDOWN_DBNAME.setText(CubridView.Current_db);
		CHECK_FILE_COMPRESS.setSelection(true);
		LIST_BACKUPFILESOURCE_LV0.removeAll();
		LIST_BACKUPFILESOURCE_LV1.removeAll();
		LIST_BACKUPFILESOURCE_LV2.removeAll();
		TableItem item;
		for (int i = 0, n = BackupAction.backinfo.size(); i < n; i++) {
			BackupInfo bi = (BackupInfo) BackupAction.backinfo.get(i);
			if (bi.Level.equals("level0")) {
				item = new TableItem(LIST_BACKUPFILESOURCE_LV0, SWT.NONE);
				item.setText(0, bi.Path);
			} else if (bi.Level.equals("level1")) {
				item = new TableItem(LIST_BACKUPFILESOURCE_LV1, SWT.NONE);
				item.setText(0, bi.Path);
			} else if (bi.Level.equals("level2")) {
				item = new TableItem(LIST_BACKUPFILESOURCE_LV2, SWT.NONE);
				item.setText(0, bi.Path);
			}
		}

		java.io.File f = new java.io.File(".");
		String Curdir;
		try {
			Curdir = f.getCanonicalPath();
		} catch (Exception e) {
			Curdir = ".";
		}

		if (LIST_BACKUPFILESOURCE_LV0.getItemCount() <= 0) {
			CHECK_BACKUPFILESAVE_LV0.setEnabled(false);
			LIST_BACKUPFILESAVE_LV0.setEnabled(false);
			LIST_BACKUPFILESOURCE_LV0.setEnabled(false);
			BUTTON_FILEDOWN_PATHLV0.setEnabled(false);
		} else {
			CHECK_BACKUPFILESAVE_LV0.setSelection(true);
			for (int i = 0, n = LIST_BACKUPFILESOURCE_LV0.getItemCount(); i < n; i++) {
				TableItem ti = LIST_BACKUPFILESOURCE_LV0.getItem(i);
				String fnm1 = ti.getText(0), fnm2;
				int pos = fnm1.lastIndexOf("/");
				if (pos >= 0)
					fnm2 = fnm1.substring(pos + 1);
				else
					fnm2 = fnm1;
				item = new TableItem(LIST_BACKUPFILESAVE_LV0, SWT.NONE);
				item.setText(0, Curdir + "\\" + fnm2 + ".gz");
			}
		}

		if (LIST_BACKUPFILESOURCE_LV1.getItemCount() <= 0) {
			CHECK_BACKUPFILESAVE_LV1.setEnabled(false);
			LIST_BACKUPFILESAVE_LV1.setEnabled(false);
			LIST_BACKUPFILESOURCE_LV1.setEnabled(false);
			BUTTON_FILEDOWN_PATHLV1.setEnabled(false);
		} else {
			CHECK_BACKUPFILESAVE_LV1.setSelection(true);
			for (int i = 0, n = LIST_BACKUPFILESOURCE_LV1.getItemCount(); i < n; i++) {
				TableItem ti = LIST_BACKUPFILESOURCE_LV1.getItem(i);
				String fnm1 = ti.getText(0), fnm2;
				int pos = fnm1.lastIndexOf("/");
				if (pos >= 0)
					fnm2 = fnm1.substring(pos + 1);
				else
					fnm2 = fnm1;
				item = new TableItem(LIST_BACKUPFILESAVE_LV1, SWT.NONE);
				item.setText(0, Curdir + "\\" + fnm2 + ".gz");
			}
		}

		if (LIST_BACKUPFILESOURCE_LV2.getItemCount() <= 0) {
			CHECK_BACKUPFILESAVE_LV2.setEnabled(false);
			LIST_BACKUPFILESAVE_LV2.setEnabled(false);
			LIST_BACKUPFILESOURCE_LV2.setEnabled(false);
			BUTTON_FILEDOWN_PATHLV2.setEnabled(false);
		} else {
			CHECK_BACKUPFILESAVE_LV2.setSelection(true);
			for (int i = 0, n = LIST_BACKUPFILESOURCE_LV2.getItemCount(); i < n; i++) {
				TableItem ti = LIST_BACKUPFILESOURCE_LV2.getItem(i);
				String fnm1 = ti.getText(0), fnm2;
				int pos = fnm1.lastIndexOf("/");
				if (pos >= 0)
					fnm2 = fnm1.substring(pos + 1);
				else
					fnm2 = fnm1;
				item = new TableItem(LIST_BACKUPFILESAVE_LV2, SWT.NONE);
				item.setText(0, Curdir + "\\" + fnm2 + ".gz");
			}
		}
		LIST_BACKUPFILESOURCE_LV0.getColumn(0).pack();
		LIST_BACKUPFILESOURCE_LV1.getColumn(0).pack();
		LIST_BACKUPFILESOURCE_LV2.getColumn(0).pack();
		LIST_BACKUPFILESAVE_LV0.getColumn(0).pack();
		LIST_BACKUPFILESAVE_LV1.getColumn(0).pack();
		LIST_BACKUPFILESAVE_LV2.getColumn(0).pack();
	}

	private void change_filepath(TableItem ti) {
		String selstr = ti.getText(0), dlgpath, dlgfile;
		int fidx = selstr.lastIndexOf("\\");
		if (fidx < 0)
			fidx = selstr.lastIndexOf("/");
		if (fidx < 0) {
			dlgpath = "";
			dlgfile = selstr;
		} else {
			dlgpath = selstr.substring(0, fidx);
			dlgfile = selstr.substring(fidx + 1);
		}

		FileDialog dialog = new FileDialog(dlgShell, SWT.SAVE
				| SWT.APPLICATION_MODAL);
		dialog.setFilterPath(dlgpath); // Windows path
		dialog.setFileName(dlgfile);
		selstr = dialog.open();
		if (selstr != null)
			ti.setText(0, selstr);
	}

	private boolean FileOpenTest(String fnm) {
		java.io.File f = new java.io.File(fnm);
		try {
			if (f.exists()) {
				if (f.delete())
					return true;
				else
					return false;
			} else {
				if (f.createNewFile()) {
					if (f.delete())
						return true;
					else
						return false;
				} else
					return false;
			}
		} catch (Exception e) {
			return false;
		}
	}
}
