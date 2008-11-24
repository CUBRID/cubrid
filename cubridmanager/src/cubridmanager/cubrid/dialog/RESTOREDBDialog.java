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
import java.util.Calendar;

import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Spinner;
import org.eclipse.swt.widgets.Text;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.ProtegoReadCert;
import cubridmanager.cubrid.action.RestoreAction;

public class RESTOREDBDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Text EDIT_RESTOREDB_DBNAME = null;
	private Group group1 = null;
	private Button CHECK_RESTOREDB_DATE = null;
	private Label label2 = null;
	private Label label3 = null;
	private Group group2 = null;
	private Button RADIO_RESTOREDB_LEVEL0 = null;
	private Text EDIT_RESTOREDB_LEVEL0 = null;
	private Button RADIO_RESTOREDB_LEVEL1 = null;
	private Text EDIT_RESTOREDB_LEVEL1 = null;
	private Button RADIO_RESTOREDB_LEVEL2 = null;
	private Text EDIT_RESTOREDB_LEVEL2 = null;
	private Button BUTTON_RESTOREDB_BACKUPINFO = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;
	private Spinner SPINYEAR = null;
	private Spinner SPINMON = null;
	private Spinner SPINDAY = null;
	private Spinner SPINHOUR = null;
	private Spinner SPINMIN = null;
	private Spinner SPINSEC = null;
	private CLabel cLabel = null;
	private Group group3 = null;
	private Button CHECK_PARTIAL_RECOVER = null;
	private boolean hasChange = false;

	public RESTOREDBDialog(Shell parent) {
		super(parent);
	}

	public RESTOREDBDialog(Shell parent, int style) {
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
		dlgShell.setText(Messages.getString("TITLE.RESTOREDBDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData25 = new org.eclipse.swt.layout.GridData();
		gridData25.horizontalSpan = 2;
		gridData25.widthHint = 250;
		GridData gridData24 = new org.eclipse.swt.layout.GridData();
		gridData24.widthHint = 250;
		GridData gridData23 = new org.eclipse.swt.layout.GridData();
		gridData23.widthHint = 250;
		GridData gridData22 = new org.eclipse.swt.layout.GridData();
		gridData22.grabExcessHorizontalSpace = false;
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.widthHint = 250;
		gridData21.grabExcessHorizontalSpace = false;
		GridLayout gridLayout20 = new GridLayout();
		gridLayout20.numColumns = 2;
		GridData gridData19 = new org.eclipse.swt.layout.GridData();
		gridData19.grabExcessHorizontalSpace = true;
		GridData gridData18 = new org.eclipse.swt.layout.GridData();
		gridData18.grabExcessHorizontalSpace = true;
		GridData gridData17 = new org.eclipse.swt.layout.GridData();
		gridData17.horizontalSpan = 8;
		GridLayout gridLayout16 = new GridLayout();
		gridLayout16.numColumns = 8;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.widthHint = 100;
		gridData5.grabExcessHorizontalSpace = true;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 100;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData4.grabExcessHorizontalSpace = false;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.grabExcessHorizontalSpace = true;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 3;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalSpan = 3;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.widthHint = 142;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.DATABASENAME1"));
		EDIT_RESTOREDB_DBNAME = new Text(sShell, SWT.BORDER);
		EDIT_RESTOREDB_DBNAME.setEditable(false);

		EDIT_RESTOREDB_DBNAME.setLayoutData(gridData);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.RESTOREDATEAND"));
		group1.setLayout(gridLayout16);
		group1.setLayoutData(gridData1);
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.AVAILABLEBACKUP"));
		group2.setLayout(gridLayout20);
		group2.setLayoutData(gridData2);

		GridData gridDataGroup3 = new org.eclipse.swt.layout.GridData();
		gridDataGroup3.horizontalSpan = 3;
		gridDataGroup3.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridDataGroup3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		group3 = new Group(sShell, SWT.NONE);
		group3.setLayout(new GridLayout());
		group3.setLayoutData(gridDataGroup3);

		// group1
		CHECK_RESTOREDB_DATE = new Button(group1, SWT.CHECK);
		CHECK_RESTOREDB_DATE.setText(Messages
				.getString("CHECK.SPECIFYARESTORE"));
		CHECK_RESTOREDB_DATE.setLayoutData(gridData17);
		CHECK_RESTOREDB_DATE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_RESTOREDB_DATE.getSelection()) {
							SPINYEAR.setEnabled(true);
							SPINMON.setEnabled(true);
							SPINDAY.setEnabled(true);
							SPINHOUR.setEnabled(true);
							SPINMIN.setEnabled(true);
							SPINSEC.setEnabled(true);
						} else {
							SPINYEAR.setEnabled(false);
							SPINMON.setEnabled(false);
							SPINDAY.setEnabled(false);
							SPINHOUR.setEnabled(false);
							SPINMIN.setEnabled(false);
							SPINSEC.setEnabled(false);
						}
					}
				});
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.DATE"));
		SPINYEAR = new Spinner(group1, SWT.BORDER);
		SPINYEAR.setMinimum(1970);
		SPINYEAR.setMaximum(9999);
		SPINMON = new Spinner(group1, SWT.BORDER);
		SPINMON.setMaximum(12);
		SPINMON.setMinimum(1);
		SPINMON.setPageIncrement(3);
		SPINDAY = new Spinner(group1, SWT.BORDER);
		SPINDAY.setMinimum(1);
		SPINDAY.setMaximum(31);
		SPINDAY.setLayoutData(gridData18);
		SPINDAY.setPageIncrement(5);
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.TIME"));
		SPINHOUR = new Spinner(group1, SWT.BORDER);
		SPINHOUR.setMaximum(23);
		SPINMIN = new Spinner(group1, SWT.BORDER);
		SPINMIN.setMaximum(59);
		SPINSEC = new Spinner(group1, SWT.BORDER);
		SPINSEC.setMaximum(59);
		SPINSEC.setLayoutData(gridData19);

		// group2
		RADIO_RESTOREDB_LEVEL2 = new Button(group2, SWT.RADIO);
		RADIO_RESTOREDB_LEVEL2.setText(Messages.getString("RADIO.LEVEL2FILE"));
		RADIO_RESTOREDB_LEVEL2
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						EDIT_RESTOREDB_LEVEL0.setEnabled(false);
						EDIT_RESTOREDB_LEVEL1.setEnabled(false);
						EDIT_RESTOREDB_LEVEL2.setEnabled(true);
					}
				});
		EDIT_RESTOREDB_LEVEL2 = new Text(group2, SWT.BORDER);
		EDIT_RESTOREDB_LEVEL2.setLayoutData(gridData24);
		EDIT_RESTOREDB_LEVEL2
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						hasChange = true;
					}
				});
		RADIO_RESTOREDB_LEVEL1 = new Button(group2, SWT.RADIO);
		RADIO_RESTOREDB_LEVEL1.setText(Messages.getString("RADIO.LEVEL1FILE"));
		RADIO_RESTOREDB_LEVEL1
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						EDIT_RESTOREDB_LEVEL0.setEnabled(false);
						EDIT_RESTOREDB_LEVEL1.setEnabled(true);
						EDIT_RESTOREDB_LEVEL2.setEnabled(false);
					}
				});
		EDIT_RESTOREDB_LEVEL1 = new Text(group2, SWT.BORDER);
		EDIT_RESTOREDB_LEVEL1.setLayoutData(gridData23);
		EDIT_RESTOREDB_LEVEL1
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						hasChange = true;
					}
				});
		RADIO_RESTOREDB_LEVEL0 = new Button(group2, SWT.RADIO);
		RADIO_RESTOREDB_LEVEL0.setText(Messages.getString("RADIO.LEVEL0FILE"));
		RADIO_RESTOREDB_LEVEL0.setLayoutData(gridData22);
		RADIO_RESTOREDB_LEVEL0
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						EDIT_RESTOREDB_LEVEL0.setEnabled(true);
						EDIT_RESTOREDB_LEVEL1.setEnabled(false);
						EDIT_RESTOREDB_LEVEL2.setEnabled(false);
					}
				});
		EDIT_RESTOREDB_LEVEL0 = new Text(group2, SWT.BORDER);
		EDIT_RESTOREDB_LEVEL0.setLayoutData(gridData21);
		EDIT_RESTOREDB_LEVEL0
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						hasChange = true;
					}
				});
		BUTTON_RESTOREDB_BACKUPINFO = new Button(group2, SWT.NONE);
		BUTTON_RESTOREDB_BACKUPINFO.setText(Messages
				.getString("BUTTON.SHOWBACKUPVOLUME"));
		BUTTON_RESTOREDB_BACKUPINFO.setLayoutData(gridData25);
		BUTTON_RESTOREDB_BACKUPINFO
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String pathname = "";
						if (RADIO_RESTOREDB_LEVEL0.getSelection()) {
							pathname = EDIT_RESTOREDB_LEVEL0.getText();
							if (pathname == null || pathname.length() == 0
									|| pathname.indexOf(" ") >= 0) {
								CommonTool.ErrorBox(dlgShell, Messages
										.getString("ERROR.INVALIDEVALUE"));
								EDIT_RESTOREDB_LEVEL0.setFocus();
								return;
							}
						} else if (RADIO_RESTOREDB_LEVEL1.getSelection()) {
							pathname = EDIT_RESTOREDB_LEVEL1.getText();
							if (pathname == null || pathname.length() == 0
									|| pathname.indexOf(" ") >= 0) {
								CommonTool.ErrorBox(dlgShell, Messages
										.getString("ERROR.INVALIDEVALUE"));
								EDIT_RESTOREDB_LEVEL1.setFocus();
								return;
							}
						} else if (RADIO_RESTOREDB_LEVEL2.getSelection()) {
							pathname = EDIT_RESTOREDB_LEVEL2.getText();
							if (pathname == null || pathname.length() == 0
									|| pathname.indexOf(" ") >= 0) {
								CommonTool.ErrorBox(dlgShell, Messages
										.getString("ERROR.INVALIDEVALUE"));
								EDIT_RESTOREDB_LEVEL2.setFocus();
								return;
							}
						}

						String requestMsg = "dbname:"
								+ EDIT_RESTOREDB_DBNAME.getText() + "\n";

						if (RADIO_RESTOREDB_LEVEL0.getSelection()) {
							requestMsg += "level:0\n";
						} else if (RADIO_RESTOREDB_LEVEL1.getSelection()) {
							requestMsg += "level:1\n";
						} else if (RADIO_RESTOREDB_LEVEL2.getSelection()) {
							requestMsg += "level:2\n";
						}
						if (hasChange)
							requestMsg += "pathname:" + pathname + "\n";
						else
							requestMsg += "pathname:none\n";

						ClientSocket cs = new ClientSocket();
						if (cs
								.SendBackGround(
										dlgShell,
										requestMsg,
										"backupvolinfo",
										Messages
												.getString("WAITING.GETTINGBACKUPINFO"))) {
							BACKUPVOLINFODialog bdlg = new BACKUPVOLINFODialog(
									dlgShell);
							bdlg.doModal();
						} else
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
					}
				});

		// group 3
		GridData gdPartialRecover = new GridData();
		gdPartialRecover.horizontalAlignment = GridData.FILL;
		CHECK_PARTIAL_RECOVER = new Button(group3, SWT.CHECK);
		CHECK_PARTIAL_RECOVER.setText(Messages
				.getString("CHECK.PARTIALRECOVER"));
		CHECK_PARTIAL_RECOVER.setLayoutData(gdPartialRecover);

		// bottom area buttons
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData3);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData4);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String pathname = "";
						if (RADIO_RESTOREDB_LEVEL0.getSelection()) {
							pathname = EDIT_RESTOREDB_LEVEL0.getText();
							if (pathname == null || pathname.length() == 0
									|| pathname.indexOf(" ") >= 0) {
								CommonTool.ErrorBox(dlgShell, Messages
										.getString("ERROR.INVALIDEVALUE"));
								EDIT_RESTOREDB_LEVEL0.setFocus();
								return;
							}
						} else if (RADIO_RESTOREDB_LEVEL1.getSelection()) {
							pathname = EDIT_RESTOREDB_LEVEL1.getText();
							if (pathname == null || pathname.length() == 0
									|| pathname.indexOf(" ") >= 0) {
								CommonTool.ErrorBox(dlgShell, Messages
										.getString("ERROR.INVALIDEVALUE"));
								EDIT_RESTOREDB_LEVEL1.setFocus();
								return;
							}
						} else if (RADIO_RESTOREDB_LEVEL2.getSelection()) {
							pathname = EDIT_RESTOREDB_LEVEL2.getText();
							if (pathname == null || pathname.length() == 0
									|| pathname.indexOf(" ") >= 0) {
								CommonTool.ErrorBox(dlgShell, Messages
										.getString("ERROR.INVALIDEVALUE"));
								EDIT_RESTOREDB_LEVEL2.setFocus();
								return;
							}
						}

						DBA_CONFIRMDialog condlg = new DBA_CONFIRMDialog(
								dlgShell);
						if (MainRegistry.isCertLogin()) {
							String[] ret = null;
							ProtegoReadCert reader = new ProtegoReadCert();
							ret = reader.protegoSelectCert();
							if (ret == null) {
								return;
							}
							if (!(ret[0].equals(MainRegistry.UserID))) {
								CommonTool.ErrorBox(Messages
										.getString("ERROR.USERDNERROR"));
								return;
							}
						} else if (!condlg.doModal()) {
							return;
						}

						String requestMsg = "";

						requestMsg += "dbname:"
								+ EDIT_RESTOREDB_DBNAME.getText() + "\n";

						if (CHECK_RESTOREDB_DATE.getSelection()) {
							NumberFormat nf = NumberFormat.getInstance();
							nf.setMinimumIntegerDigits(2);
							String timeStr = nf.format(SPINHOUR.getSelection())
									+ ":" + nf.format(SPINMIN.getSelection())
									+ ":" + nf.format(SPINSEC.getSelection());
							String dateStr = nf.format(SPINDAY.getSelection())
									+ "-" + nf.format(SPINMON.getSelection())
									+ "-" + SPINYEAR.getSelection();
							requestMsg += "date:" + dateStr + ":" + timeStr
									+ "\n";
						} else {
							requestMsg += "date:none\n";
						}

						if (RADIO_RESTOREDB_LEVEL0.getSelection()) {
							requestMsg += "level:0\n";
						} else if (RADIO_RESTOREDB_LEVEL1.getSelection()) {
							requestMsg += "level:1\n";
						} else if (RADIO_RESTOREDB_LEVEL2.getSelection()) {
							requestMsg += "level:2\n";
						}

						if (CHECK_PARTIAL_RECOVER.getSelection())
							requestMsg += "partial:y\n";
						else
							requestMsg += "partial:n\n";

						if (hasChange)
							requestMsg += "pathname:" + pathname + "\n";
						else
							requestMsg += "pathname:none\n";

						ClientSocket cs = new ClientSocket();
						if (cs.SendBackGround(dlgShell, requestMsg,
								"restoredb", Messages
										.getString("WAITING.RESTOREDB"))) {
							ret = true;
							dlgShell.dispose();
						} else
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);

					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData5);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});
		setinfo();
		dlgShell.pack();
	}

	private void setinfo() {
		EDIT_RESTOREDB_DBNAME.setText(RestoreAction.ai.dbname);
		CHECK_RESTOREDB_DATE.setSelection(false);

		Calendar calendar = Calendar.getInstance();
		SPINYEAR.setSelection(calendar.get(Calendar.YEAR));
		SPINMON.setSelection(calendar.get(Calendar.MONTH) + 1);
		SPINDAY.setSelection(calendar.get(Calendar.DATE));
		SPINHOUR.setSelection(calendar.get(Calendar.HOUR_OF_DAY));
		SPINMIN.setSelection(calendar.get(Calendar.MINUTE));
		SPINSEC.setSelection(calendar.get(Calendar.SECOND));
		SPINYEAR.setEnabled(false);
		SPINMON.setEnabled(false);
		SPINDAY.setEnabled(false);
		SPINHOUR.setEnabled(false);
		SPINMIN.setEnabled(false);
		SPINSEC.setEnabled(false);
		int maxlevel = -1;
		RADIO_RESTOREDB_LEVEL0.setEnabled(false);
		RADIO_RESTOREDB_LEVEL1.setEnabled(false);
		RADIO_RESTOREDB_LEVEL2.setEnabled(false);
		EDIT_RESTOREDB_LEVEL0.setEnabled(false);
		EDIT_RESTOREDB_LEVEL1.setEnabled(false);
		EDIT_RESTOREDB_LEVEL2.setEnabled(false);
		for (int i = 0, n = MainRegistry.Tmpchkrst.size(); i < n; i += 2) {
			String lvl = (String) MainRegistry.Tmpchkrst.get(i);
			String fil = (String) MainRegistry.Tmpchkrst.get(i + 1);
			if (lvl.equals("level0") && !fil.equals("none")) {
				if (maxlevel < 0)
					maxlevel = 0;
				EDIT_RESTOREDB_LEVEL0.setText(fil);
				RADIO_RESTOREDB_LEVEL0.setEnabled(true);
			} else if (lvl.equals("level1") && !fil.equals("none")) {
				if (maxlevel < 1)
					maxlevel = 1;
				EDIT_RESTOREDB_LEVEL1.setText(fil);
				RADIO_RESTOREDB_LEVEL1.setEnabled(true);
			} else if (lvl.equals("level2") && !fil.equals("none")) {
				if (maxlevel < 2)
					maxlevel = 2;
				EDIT_RESTOREDB_LEVEL2.setText(fil);
				RADIO_RESTOREDB_LEVEL2.setEnabled(true);
			}
		}
		if (maxlevel == -1) {
			BUTTON_RESTOREDB_BACKUPINFO.setEnabled(false);
			IDOK.setEnabled(false);
		} else if (maxlevel == 0) {
			EDIT_RESTOREDB_LEVEL0.setEnabled(true);
			RADIO_RESTOREDB_LEVEL0.setSelection(true);
		} else if (maxlevel == 1) {
			EDIT_RESTOREDB_LEVEL1.setEnabled(true);
			RADIO_RESTOREDB_LEVEL1.setSelection(true);
		} else if (maxlevel == 2) {
			EDIT_RESTOREDB_LEVEL2.setEnabled(true);
			RADIO_RESTOREDB_LEVEL2.setSelection(true);
		}
		CHECK_RESTOREDB_DATE.setToolTipText(Messages
				.getString("TOOLTIP.RESTORECHECKDATE"));
		CHECK_PARTIAL_RECOVER.setToolTipText(Messages
				.getString("TOOLTIP.PARTIALRECOVER"));
		hasChange = false;
	}
}
