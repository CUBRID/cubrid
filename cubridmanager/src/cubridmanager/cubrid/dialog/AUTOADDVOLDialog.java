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
import org.eclipse.swt.widgets.Spinner;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.Messages;
import cubridmanager.CommonTool;
import cubridmanager.VerifyDigitListener;
import cubridmanager.cubrid.view.CubridView;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class AUTOADDVOLDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Button CHECK_AUTOADDVOL_DATA = null;
	private Label label1 = null;
	private Spinner EDIT_AUTOADDVOL_DWRATE = null;
	private Label label2 = null;
	private Text EDIT_AUTOADDVOL_DERATE = null;
	private Group group2 = null;
	private Button CHECK_AUTOADDVOL_INDEX = null;
	private Label label3 = null;
	private Spinner EDIT_AUTOADDVOL_IWRATE = null;
	private Label label4 = null;
	private Text EDIT_AUTOADDVOL_IERATE = null;
	private Label label5 = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	public static boolean data_chk = false;
	public static boolean idx_chk = false;
	public static String data_warn = "";
	public static String idx_warn = "";
	public static String data_ext = "";
	public static String idx_ext = "";

	public AUTOADDVOLDialog(Shell parent) {
		super(parent);
	}

	public AUTOADDVOLDialog(Shell parent, int style) {
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
		dlgShell.setText(Messages.getString("TITLE.AUTOADDVOLDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData27 = new org.eclipse.swt.layout.GridData();
		gridData27.horizontalSpan = 2;
		gridData27.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData27.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData27.widthHint = 168;
		GridData gridData26 = new org.eclipse.swt.layout.GridData();
		gridData26.widthHint = 92;
		gridData26.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData26.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData25 = new org.eclipse.swt.layout.GridData();
		gridData25.horizontalSpan = 2;
		GridData gridData24 = new org.eclipse.swt.layout.GridData();
		gridData24.horizontalSpan = 3;
		GridLayout gridLayout23 = new GridLayout();
		gridLayout23.numColumns = 3;
		GridData gridData22 = new org.eclipse.swt.layout.GridData();
		gridData22.horizontalSpan = 2;
		gridData22.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData22.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData22.widthHint = 168;
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.widthHint = 92;
		gridData21.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData21.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData20 = new org.eclipse.swt.layout.GridData();
		gridData20.horizontalSpan = 2;
		GridData gridData19 = new org.eclipse.swt.layout.GridData();
		gridData19.horizontalSpan = 3;
		GridLayout gridLayout18 = new GridLayout();
		gridLayout18.numColumns = 3;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.grabExcessHorizontalSpace = true;
		gridData4.widthHint = 100;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 100;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData3.grabExcessHorizontalSpace = true;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.grabExcessHorizontalSpace = false;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData2.horizontalSpan = 2;
		gridData2.widthHint = 240;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalSpan = 2;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.VOLUMEPURPOSE"));
		group1.setLayout(gridLayout18);
		group1.setLayoutData(gridData);
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.VOLUMEPURPOSE1"));
		group2.setLayout(gridLayout23);
		group2.setLayoutData(gridData1);
		CHECK_AUTOADDVOL_DATA = new Button(group1, SWT.CHECK);
		CHECK_AUTOADDVOL_DATA.setText(Messages
				.getString("CHECK.USINGAUTOMATIC"));
		CHECK_AUTOADDVOL_DATA.setLayoutData(gridData19);
		CHECK_AUTOADDVOL_DATA
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_AUTOADDVOL_DATA.getSelection()) {
							EDIT_AUTOADDVOL_DWRATE.setEnabled(true);
							EDIT_AUTOADDVOL_DERATE.setEnabled(true);
						} else {
							EDIT_AUTOADDVOL_DWRATE.setEnabled(false);
							EDIT_AUTOADDVOL_DERATE.setEnabled(false);
						}
					}
				});
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.OUTOFSPACEWARNING"));
		label1.setLayoutData(gridData20);
		EDIT_AUTOADDVOL_DWRATE = new Spinner(group1, SWT.BORDER);
		EDIT_AUTOADDVOL_DWRATE.setMaximum(1000000000);
		EDIT_AUTOADDVOL_DWRATE.setMinimum(0);
		EDIT_AUTOADDVOL_DWRATE.setLayoutData(gridData21);
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.EXTENSIONPAGE"));
		EDIT_AUTOADDVOL_DERATE = new Text(group1, SWT.BORDER);
		EDIT_AUTOADDVOL_DERATE.setLayoutData(gridData22);
		EDIT_AUTOADDVOL_DERATE.addListener(SWT.Verify,
				new VerifyDigitListener());
		CHECK_AUTOADDVOL_INDEX = new Button(group2, SWT.CHECK);
		CHECK_AUTOADDVOL_INDEX.setText(Messages
				.getString("CHECK.USINGAUTOMATIC"));
		CHECK_AUTOADDVOL_INDEX.setLayoutData(gridData24);
		CHECK_AUTOADDVOL_INDEX
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_AUTOADDVOL_INDEX.getSelection()) {
							EDIT_AUTOADDVOL_IWRATE.setEnabled(true);
							EDIT_AUTOADDVOL_IERATE.setEnabled(true);
						} else {
							EDIT_AUTOADDVOL_IWRATE.setEnabled(false);
							EDIT_AUTOADDVOL_IERATE.setEnabled(false);
						}
					}
				});
		label3 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.OUTOFSPACEWARNING"));
		label3.setLayoutData(gridData25);
		EDIT_AUTOADDVOL_IWRATE = new Spinner(group2, SWT.BORDER);
		EDIT_AUTOADDVOL_IWRATE.setMaximum(1000000000);
		EDIT_AUTOADDVOL_IWRATE.setMinimum(0);
		EDIT_AUTOADDVOL_IWRATE.setLayoutData(gridData26);
		label4 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.EXTENSIONPAGE"));
		EDIT_AUTOADDVOL_IERATE = new Text(group2, SWT.BORDER);
		EDIT_AUTOADDVOL_IERATE.setLayoutData(gridData27);
		EDIT_AUTOADDVOL_IERATE.addListener(SWT.Verify,
				new VerifyDigitListener());
		label5 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.MEANSIFFREESPACE"));
		label5.setLayoutData(gridData2);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData3);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int datw = 0, idxw = 0;
						double datp = 0, idxp = 0;

						if (CHECK_AUTOADDVOL_DATA.getSelection()) {
							datw = EDIT_AUTOADDVOL_DWRATE.getSelection();
							datp = CommonTool.atof(EDIT_AUTOADDVOL_DERATE
									.getText());

							if (datw < 5) {
								CommonTool
										.ErrorBox(
												dlgShell,
												Messages
														.getString("ERROR.TOO_SMALLWARNRATEDATA"));
								return;
							} else if (datw > 30) {
								CommonTool
										.ErrorBox(
												dlgShell,
												Messages
														.getString("ERROR.TOO_LARGEWARNRATEDATA"));
								return;
							}

							if (datp <= 0.0) {
								CommonTool.ErrorBox(dlgShell, Messages
										.getString("ERROR.INVALIDEXTPAGEDATA"));
								return;
							}
							if (datp < 1000.0) {
								CommonTool
										.ErrorBox(
												dlgShell,
												Messages
														.getString("ERROR.TOO_SMALL_EXTPAGEDATA"));
								EDIT_AUTOADDVOL_DERATE.setText("10000");
								return;
							}
						}
						if (CHECK_AUTOADDVOL_INDEX.getSelection()) {
							idxw = EDIT_AUTOADDVOL_IWRATE.getSelection();
							idxp = CommonTool.atof(EDIT_AUTOADDVOL_IERATE
									.getText());
							if (idxw < 5) {
								CommonTool
										.ErrorBox(
												dlgShell,
												Messages
														.getString("ERROR.TOO_SMALLWARNRATEINDEX"));
								return;
							} else if (idxw > 30) {
								CommonTool
										.ErrorBox(
												dlgShell,
												Messages
														.getString("ERROR.TOO_LARGEWARNRATEINDEX"));
								return;
							}

							if (idxp <= 0.0) {
								CommonTool
										.ErrorBox(
												dlgShell,
												Messages
														.getString("ERROR.INVALIDEXTPAGEINDEX"));
								return;
							}
							if (idxp < 1000.0) {
								CommonTool
										.ErrorBox(
												dlgShell,
												Messages
														.getString("ERROR.TOO_SMALL_EXTPAGEINDEX"));
								EDIT_AUTOADDVOL_IERATE.setText("10000");
								return;
							}
						}
						String sDatw = String.valueOf((float) datw / 100.0);
						String sIdxw = String.valueOf((float) idxw / 100.0);
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "data:"
								+ ((CHECK_AUTOADDVOL_DATA.getSelection()) ? "ON"
										: "OFF") + "\n";
						msg += "data_warn_outofspace:" + sDatw + "\n";
						msg += "data_ext_page:" + datp + "\n";
						msg += "index:"
								+ ((CHECK_AUTOADDVOL_INDEX.getSelection()) ? "ON"
										: "OFF") + "\n";
						msg += "index_warn_outofspace:" + sIdxw + "\n";
						msg += "index_ext_page:" + idxp;
						ClientSocket cs = new ClientSocket();
						if (!cs
								.SendBackGround(
										dlgShell,
										msg,
										"setautoaddvol",
										Messages
												.getString("WAITING.SETTINGAUTOADDVOL"))) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}
						dlgShell.update();
						CommonTool.MsgBox(dlgShell, Messages
								.getString("MSG.SUCCESS"), Messages
								.getString("MSG.SUCCESSAUTOADDVOL"));
						dlgShell.dispose();
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData4);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	private void setinfo() {
		int datw, idxw;
		try {
			datw = (int) (Float.parseFloat(data_warn) * 100.0);
			idxw = (int) (Float.parseFloat(idx_warn) * 100.0);
			if (datw < 5)
				datw = 5;
			if (idxw < 5)
				idxw = 5;
		} catch (Exception ee) {
			datw = idxw = 5;
		}

		try {
			float f_data_ext, f_idx_ext;
			f_data_ext = Float.parseFloat(data_ext);
			f_idx_ext = Float.parseFloat(idx_ext);
			if (f_data_ext < 1000.0)
				data_ext = "1000.0";
			if (f_idx_ext < 1000.0)
				idx_ext = "1000.0";
		} catch (Exception ee) {
			data_ext = "1000.0";
			idx_ext = "1000.0";
		}
		CHECK_AUTOADDVOL_DATA.setSelection(data_chk);
		CHECK_AUTOADDVOL_INDEX.setSelection(idx_chk);
		EDIT_AUTOADDVOL_DWRATE.setEnabled(data_chk);
		EDIT_AUTOADDVOL_DERATE.setEnabled(data_chk);
		EDIT_AUTOADDVOL_DWRATE.setSelection(datw);
		EDIT_AUTOADDVOL_DERATE.setText(data_ext);
		EDIT_AUTOADDVOL_IWRATE.setEnabled(idx_chk);
		EDIT_AUTOADDVOL_IERATE.setEnabled(idx_chk);
		EDIT_AUTOADDVOL_IWRATE.setSelection(idxw);
		EDIT_AUTOADDVOL_IERATE.setText(idx_ext);
		EDIT_AUTOADDVOL_DWRATE.setToolTipText(Messages
				.getString("TOOLTIP.WARNRATE"));
		EDIT_AUTOADDVOL_DERATE.setToolTipText(Messages
				.getString("TOOLTIP.AUTOEXTPAGES"));
		EDIT_AUTOADDVOL_IWRATE.setToolTipText(Messages
				.getString("TOOLTIP.WARNRATE"));
		EDIT_AUTOADDVOL_IERATE.setToolTipText(Messages
				.getString("TOOLTIP.AUTOEXTPAGES"));
	}
}
