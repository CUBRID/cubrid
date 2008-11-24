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

package cubridmanager.cas.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cas.view.CASView;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class BROKER_SETDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Button CHECK_BS_LOG = null;
	private Combo COMBO_BROKER_ONSET_LOG = null;
	private Button CHECK_BS_TIME = null;
	private Text EDIT_BROKER_ONSET_TIME = null;
	private Button CHECK_BS_MAX = null;
	private Text EDIT_BROKER_ONSET_MAX = null;
	private Button CHECK_BS_COMPRESS = null;
	private Text EDIT_BROKER_ONSET_COMPRESS = null;
	private Button CHECK_BS_BACKUP = null;
	private Combo COMBO_BROKER_ONSET_BACKUP = null;
	private Button CHECK_BS_GAP = null;
	private Text EDIT_BROKER_ONSET_GAP = null;
	private Button CHECK_BS_TTK = null;
	private Text EDIT_BROKER_ONSET_TTK = null;
	private Group group2 = null;
	private Label label1 = null;
	private Button BUTTON_BROKER_APPLY = null;
	private Button IDOK = null;
	private Label label = null;
	private int dlgEndCode = SWT.CANCEL;
	public BROKER_SETDialog(Shell parent) {

		super(parent);
	}

	public BROKER_SETDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.open();

		setinfo();
		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return dlgEndCode;
	}

	private void createSShell() {
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages
				.getString("TOOL.SETPARAMETERUSINGCHANGERACTION"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData48 = new org.eclipse.swt.layout.GridData();
		gridData48.widthHint = 80;
		gridData48.grabExcessVerticalSpace = true;
		GridData gridData47 = new org.eclipse.swt.layout.GridData();
		gridData47.widthHint = 80;
		gridData47.grabExcessVerticalSpace = true;
		GridData gridData46 = new org.eclipse.swt.layout.GridData();
		gridData46.widthHint = 80;
		gridData46.grabExcessVerticalSpace = true;
		GridData gridData45 = new org.eclipse.swt.layout.GridData();
		gridData45.widthHint = 80;
		gridData45.grabExcessVerticalSpace = true;
		GridData gridData44 = new org.eclipse.swt.layout.GridData();
		gridData44.widthHint = 80;
		gridData44.grabExcessVerticalSpace = true;
		GridLayout gridLayout43 = new GridLayout();
		gridLayout43.numColumns = 2;
		GridData gridData42 = new org.eclipse.swt.layout.GridData();
		gridData42.widthHint = 320;
		gridData42.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData42.grabExcessVerticalSpace = true;
		gridData42.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData41 = new org.eclipse.swt.layout.GridData();
		gridData41.widthHint = 75;
		GridData gridData40 = new org.eclipse.swt.layout.GridData();
		gridData40.widthHint = 75;
		gridData40.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData40.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData39 = new org.eclipse.swt.layout.GridData();
		gridData39.horizontalSpan = 2;
		gridData39.grabExcessHorizontalSpace = true;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.PARAMETERS"));
		group1.setLayout(gridLayout43);
		group1.setLayoutData(gridData);
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.PARAMETERDESCRIPTION"));
		group2.setLayout(new GridLayout());
		group2.setLayoutData(gridData1);
		CHECK_BS_LOG = new Button(group1, SWT.CHECK);
		CHECK_BS_LOG.setText(Messages.getString("CHECK.SQLLOG"));
		CHECK_BS_LOG
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						label1.setText(Messages.getString("TOOLTIP.BRKSETLOG"));
						dlgShell.pack();
						if (CHECK_BS_LOG.getSelection())
							COMBO_BROKER_ONSET_LOG.setEnabled(true);
						else
							COMBO_BROKER_ONSET_LOG.setEnabled(false);
					}
				});
		createCombo1();
		CHECK_BS_TIME = new Button(group1, SWT.CHECK);
		CHECK_BS_TIME.setText(Messages.getString("CHECK.SQLLOGTIME"));
		CHECK_BS_TIME
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						label1
								.setText(Messages
										.getString("TOOLTIP.BRKSETTIME"));
						dlgShell.pack();
						if (CHECK_BS_TIME.getSelection())
							EDIT_BROKER_ONSET_TIME.setEnabled(true);
						else
							EDIT_BROKER_ONSET_TIME.setEnabled(false);
					}
				});
		EDIT_BROKER_ONSET_TIME = new Text(group1, SWT.BORDER | SWT.RIGHT);
		EDIT_BROKER_ONSET_TIME.setEnabled(false);
		EDIT_BROKER_ONSET_TIME.setLayoutData(gridData44);
		CHECK_BS_MAX = new Button(group1, SWT.CHECK);
		CHECK_BS_MAX.setText(Messages.getString("CHECK.APPLSERVERMAX"));
		CHECK_BS_MAX
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						label1.setText(Messages.getString("TOOLTIP.BRKSETMAX"));
						dlgShell.pack();
						if (CHECK_BS_MAX.getSelection())
							EDIT_BROKER_ONSET_MAX.setEnabled(true);
						else
							EDIT_BROKER_ONSET_MAX.setEnabled(false);
					}
				});
		EDIT_BROKER_ONSET_MAX = new Text(group1, SWT.BORDER | SWT.RIGHT);
		EDIT_BROKER_ONSET_MAX.setEnabled(false);
		EDIT_BROKER_ONSET_MAX.setLayoutData(gridData45);
		CHECK_BS_COMPRESS = new Button(group1, SWT.CHECK);
		CHECK_BS_COMPRESS.setText(Messages.getString("CHECK.COMPRESSSIZE"));
		CHECK_BS_COMPRESS
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						label1.setText(Messages
								.getString("TOOLTIP.BRKSETCOMPRESS"));
						dlgShell.pack();
						if (CHECK_BS_COMPRESS.getSelection())
							EDIT_BROKER_ONSET_COMPRESS.setEnabled(true);
						else
							EDIT_BROKER_ONSET_COMPRESS.setEnabled(false);
					}
				});
		EDIT_BROKER_ONSET_COMPRESS = new Text(group1, SWT.BORDER | SWT.RIGHT);
		EDIT_BROKER_ONSET_COMPRESS.setEnabled(false);
		EDIT_BROKER_ONSET_COMPRESS.setLayoutData(gridData46);
		CHECK_BS_BACKUP = new Button(group1, SWT.CHECK);
		CHECK_BS_BACKUP.setText(Messages.getString("CHECK.LOGBACKUP"));
		CHECK_BS_BACKUP
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						label1.setText(Messages
								.getString("TOOLTIP.BRKSETBACKUP"));
						dlgShell.pack();
						if (CHECK_BS_BACKUP.getSelection())
							COMBO_BROKER_ONSET_BACKUP.setEnabled(true);
						else
							COMBO_BROKER_ONSET_BACKUP.setEnabled(false);
					}
				});
		createCombo2();
		CHECK_BS_GAP = new Button(group1, SWT.CHECK);
		CHECK_BS_GAP.setText(Messages.getString("CHECK.PRIORITYGAP"));
		CHECK_BS_GAP
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						label1.setText(Messages.getString("TOOLTIP.BRKSETGAP"));
						dlgShell.pack();
						if (CHECK_BS_GAP.getSelection())
							EDIT_BROKER_ONSET_GAP.setEnabled(true);
						else
							EDIT_BROKER_ONSET_GAP.setEnabled(false);
					}
				});
		EDIT_BROKER_ONSET_GAP = new Text(group1, SWT.BORDER | SWT.RIGHT);
		EDIT_BROKER_ONSET_GAP.setEnabled(false);
		EDIT_BROKER_ONSET_GAP.setLayoutData(gridData47);
		CHECK_BS_TTK = new Button(group1, SWT.CHECK);
		CHECK_BS_TTK.setText(Messages.getString("CHECK.TIMETOKILL"));
		CHECK_BS_TTK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						label1.setText(Messages.getString("TOOLTIP.BRKSETTTK"));
						dlgShell.pack();
						if (CHECK_BS_TTK.getSelection())
							EDIT_BROKER_ONSET_TTK.setEnabled(true);
						else
							EDIT_BROKER_ONSET_TTK.setEnabled(false);
					}
				});
		EDIT_BROKER_ONSET_TTK = new Text(group1, SWT.BORDER | SWT.RIGHT);
		EDIT_BROKER_ONSET_TTK.setEnabled(false);
		EDIT_BROKER_ONSET_TTK.setLayoutData(gridData48);
		label1 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label1.setLayoutData(gridData42);
		label = new Label(sShell, SWT.NONE);
		label.setLayoutData(gridData39);
		BUTTON_BROKER_APPLY = new Button(sShell, SWT.NONE);
		BUTTON_BROKER_APPLY.setText(Messages.getString("BUTTON.OK"));
		BUTTON_BROKER_APPLY.setLayoutData(gridData40);
		BUTTON_BROKER_APPLY
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						gosend();
						ClientSocket cs = new ClientSocket();
						if (!cs.SendClientMessage(dlgShell, "",
								"getbrokersinfo")) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}

						dlgEndCode = SWT.OK;
						dlgShell.dispose();
					}
				});
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.CANCEL"));
		IDOK.setLayoutData(gridData41);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgEndCode = SWT.CANCEL;
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	private void createCombo1() {
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 80;
		gridData2.grabExcessVerticalSpace = true;
		COMBO_BROKER_ONSET_LOG = new Combo(group1, SWT.DROP_DOWN);
		COMBO_BROKER_ONSET_LOG.setEnabled(false);
		COMBO_BROKER_ONSET_LOG.setLayoutData(gridData2);
	}

	private void createCombo2() {
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 80;
		gridData3.grabExcessVerticalSpace = true;
		COMBO_BROKER_ONSET_BACKUP = new Combo(group1, SWT.DROP_DOWN);
		COMBO_BROKER_ONSET_BACKUP.setEnabled(false);
		COMBO_BROKER_ONSET_BACKUP.setLayoutData(gridData3);
	}

	private void setinfo() {
		COMBO_BROKER_ONSET_LOG.add("ON", 0);
		COMBO_BROKER_ONSET_LOG.add("OFF", 1);
		COMBO_BROKER_ONSET_BACKUP.add("ON", 0);
		COMBO_BROKER_ONSET_BACKUP.add("OFF", 1);
		if (((String) MainRegistry.Tmpchkrst.get(0)).equals("ON"))
			COMBO_BROKER_ONSET_LOG.select(0);
		else
			COMBO_BROKER_ONSET_LOG.select(1);
		EDIT_BROKER_ONSET_TIME.setText((String) MainRegistry.Tmpchkrst.get(1));
		EDIT_BROKER_ONSET_MAX.setText((String) MainRegistry.Tmpchkrst.get(2));
		EDIT_BROKER_ONSET_COMPRESS.setText((String) MainRegistry.Tmpchkrst
				.get(3));
		if (((String) MainRegistry.Tmpchkrst.get(4)).equals("ON"))
			COMBO_BROKER_ONSET_BACKUP.select(0);
		else
			COMBO_BROKER_ONSET_BACKUP.select(1);
		EDIT_BROKER_ONSET_GAP.setText((String) MainRegistry.Tmpchkrst.get(5));
		EDIT_BROKER_ONSET_TTK.setText((String) MainRegistry.Tmpchkrst.get(6));
	}

	boolean IsInteger(String str) {
		for (int i = 0; i < str.length(); i++) {
			if (!Character.isDigit(str.charAt(i)))
				return false;
		}
		return true;
	}

	boolean CheckValid(String name, String value) {
		String err;

		if (name.equals("SQL_LOG_TIME")) {
			if (value.equals("-1"))
				return true;
			if (IsInteger(value))
				return true;
			else
				err = "The value is not integer!";
		} else {
			if (IsInteger(value))
				return true;
			else
				err = "The value is not integer!";
		}
		CommonTool.ErrorBox(dlgShell, err);
		return false;
	}

	private void gosend() {
		String msg = "bname:" + CASView.Current_broker + "\n";

		if (CHECK_BS_LOG.getSelection()) {
			String message = msg;
			message += "conf_name:SQL_LOG\n";
			message += "conf_value:" + COMBO_BROKER_ONSET_LOG.getText() + "\n";
			ClientSocket cs = new ClientSocket();
			if (!cs.SendBackGround(dlgShell, message, "setbrokeronconf",
					Messages.getString("WAITING.SETBROKERCONF"))) {
				CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
				return;
			}
		}

		if (CHECK_BS_TIME.getSelection()) {
			String val = EDIT_BROKER_ONSET_TIME.getText().trim();
			if (!CheckValid("SQL_LOG_TIME", val)) {
				EDIT_BROKER_ONSET_TIME.setFocus();
				return;
			}
			String message = msg;
			message += "conf_name:SQL_LOG_TIME\n";
			message += "conf_value:" + val + "\n";
			ClientSocket cs = new ClientSocket();
			if (!cs.SendBackGround(dlgShell, message, "setbrokeronconf",
					Messages.getString("WAITING.SETBROKERCONF"))) {
				CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
				return;
			}
		}

		if (CHECK_BS_MAX.getSelection()) {
			String val = EDIT_BROKER_ONSET_MAX.getText().trim();
			if (!CheckValid("APPL_SERVER_MAX_SIZE", val)) {
				EDIT_BROKER_ONSET_MAX.setFocus();
				return;
			}
			String message = msg;
			message += "conf_name:APPL_SERVER_MAX_SIZE\n";
			message += "conf_value:" + val + "\n";
			ClientSocket cs = new ClientSocket();
			if (!cs.SendBackGround(dlgShell, message, "setbrokeronconf",
					Messages.getString("WAITING.SETBROKERCONF"))) {
				CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
				return;
			}
		}

		if (CHECK_BS_COMPRESS.getSelection()) {
			String val = EDIT_BROKER_ONSET_COMPRESS.getText().trim();
			if (!CheckValid("COMPRESS_SIZE", val)) {
				EDIT_BROKER_ONSET_COMPRESS.setFocus();
				return;
			}
			String message = msg;
			message += "conf_name:COMPRESS_SIZE\n";
			message += "conf_value:" + val + "\n";
			ClientSocket cs = new ClientSocket();
			if (!cs.SendBackGround(dlgShell, message, "setbrokeronconf",
					Messages.getString("WAITING.SETBROKERCONF"))) {
				CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
				return;
			}
		}

		if (CHECK_BS_BACKUP.getSelection()) {
			String val = COMBO_BROKER_ONSET_BACKUP.getText();
			String message = msg;
			message += "conf_name:LOG_BACKUP\n";
			message += "conf_value:" + val + "\n";
			ClientSocket cs = new ClientSocket();
			if (!cs.SendBackGround(dlgShell, message, "setbrokeronconf",
					Messages.getString("WAITING.SETBROKERCONF"))) {
				CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
				return;
			}
		}

		if (CHECK_BS_GAP.getSelection()) {
			String val = EDIT_BROKER_ONSET_GAP.getText().trim();
			if (!CheckValid("PRIORITY_GAP", val)) {
				EDIT_BROKER_ONSET_GAP.setFocus();
				return;
			}
			String message = msg;
			message += "conf_name:PRIORITY_GAP\n";
			message += "conf_value:" + val + "\n";
			ClientSocket cs = new ClientSocket();
			if (!cs.SendBackGround(dlgShell, message, "setbrokeronconf",
					Messages.getString("WAITING.SETBROKERCONF"))) {
				CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
				return;
			}
		}

		if (CHECK_BS_TTK.getSelection()) {
			String val = EDIT_BROKER_ONSET_TTK.getText().trim();
			if (!CheckValid("TIME_TO_KILL", val)) {
				EDIT_BROKER_ONSET_TTK.setFocus();
				return;
			}
			String message = msg;
			message += "conf_name:TIME_TO_KILL\n";
			message += "conf_value:" + val + "\n";
			ClientSocket cs = new ClientSocket();
			if (!cs.SendBackGround(dlgShell, message, "setbrokeronconf",
					Messages.getString("WAITING.SETBROKERCONF"))) {
				CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
				return;
			}
		}
	}
}
