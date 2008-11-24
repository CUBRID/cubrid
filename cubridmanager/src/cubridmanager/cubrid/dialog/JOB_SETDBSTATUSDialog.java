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

import java.util.Properties;
import java.util.Timer;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.CommonTool;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.CubridViewTimer;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class JOB_SETDBSTATUSDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Combo COMBO_DBSTATUS_ONOFF = null;
	private Label label2 = null;
	private Text EDIT_DBSTATUS_PERIOD = null;
	private Button BUTTON_DBSTATUS_APPLY = null;
	private Button BUTTON_DBSTATUS_HELP = null;
	private Button IDOK = null;
	private Group group1 = null;
	private Label label3 = null;
	private Group group2 = null;
	GridData gridDatahelp = null;

	public JOB_SETDBSTATUSDialog(Shell parent) {
		super(parent);
	}

	public JOB_SETDBSTATUSDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		dlgShell.setDefaultButton(BUTTON_DBSTATUS_APPLY);
		group2.setVisible(false);
		gridDatahelp.heightHint = 0;
		BUTTON_DBSTATUS_HELP.setText(Messages.getString("BUTTON.HELP"));

		setinfo();
		dlgShell.pack();
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
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.JOB_SETDBSTATUSDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData28 = new org.eclipse.swt.layout.GridData();
		gridData28.grabExcessHorizontalSpace = true;
		GridData gridData27 = new org.eclipse.swt.layout.GridData();
		gridData27.widthHint = 96;
		gridData27.grabExcessHorizontalSpace = false;
		GridLayout gridLayout26 = new GridLayout();
		gridLayout26.numColumns = 2;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalSpan = 3;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridDatahelp = gridData4;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 100;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 100;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.widthHint = 100;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 3;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayoutData(gridData);
		group1.setLayout(gridLayout26);
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.MONITORINGON"));
		label1.setLayoutData(gridData28);
		createCombo1();
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.PERIODUNITMINUTE"));
		EDIT_DBSTATUS_PERIOD = new Text(group1, SWT.BORDER);
		EDIT_DBSTATUS_PERIOD.setLayoutData(gridData27);
		BUTTON_DBSTATUS_APPLY = new Button(sShell, SWT.NONE);
		BUTTON_DBSTATUS_APPLY.setText(Messages.getString("BUTTON.APPLY"));
		BUTTON_DBSTATUS_APPLY.setLayoutData(gridData1);
		BUTTON_DBSTATUS_APPLY
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int intv = CommonTool.atoi(EDIT_DBSTATUS_PERIOD
								.getText());
						if (intv < 5) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.MINIMUMVALUE"));
							return;
						}
						if (CubridView.viewtimer != null) {
							CubridView.viewtimer.cancel();
							CubridView.viewtimer = null;
						}
						MainRegistry.MONPARA_STATUS = COMBO_DBSTATUS_ONOFF
								.getText();
						MainRegistry.MONPARA_INTERVAL = EDIT_DBSTATUS_PERIOD
								.getText();
						Properties prop = new Properties();
						if (!CommonTool.LoadProperties(prop))
							CommonTool.SetDefaultParameter();
						prop.setProperty(MainConstants.MONPARA_STATUS,
								MainRegistry.MONPARA_STATUS);
						prop.setProperty(MainConstants.MONPARA_INTERVAL,
								MainRegistry.MONPARA_INTERVAL);
						CommonTool.SaveProperties(prop);
						if (MainRegistry.IsConnected
								&& MainRegistry.MONPARA_STATUS.equals("ON")) {
							CubridView.viewtimer = new Timer();
							CubridView.cvt = new CubridViewTimer();
							CubridView.viewtimer
									.scheduleAtFixedRate(
											CubridView.cvt,
											CommonTool
													.atol(MainRegistry.MONPARA_INTERVAL) * 60 * 1000,
											CommonTool
													.atol(MainRegistry.MONPARA_INTERVAL) * 60 * 1000);
						}
						dlgShell.dispose();
					}
				});
		BUTTON_DBSTATUS_HELP = new Button(sShell, SWT.NONE);
		BUTTON_DBSTATUS_HELP.setText(Messages.getString("BUTTON.HELP"));
		BUTTON_DBSTATUS_HELP.setLayoutData(gridData2);
		BUTTON_DBSTATUS_HELP
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (BUTTON_DBSTATUS_HELP.getText().equals(
								Messages.getString("BUTTON.HIDEHELP"))) {
							group2.setVisible(false);
							gridDatahelp.heightHint = 0;
							BUTTON_DBSTATUS_HELP.setText(Messages
									.getString("BUTTON.HELP"));
							dlgShell.pack();
						} else {
							group2.setVisible(true);
							gridDatahelp.heightHint = -1;
							dlgShell
									.setSize(new org.eclipse.swt.graphics.Point(
											334, 298));
							BUTTON_DBSTATUS_HELP.setText(Messages
									.getString("BUTTON.HIDEHELP"));
							dlgShell.pack();
						}
					}
				});
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.EXIT"));
		IDOK.setLayoutData(gridData3);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
		group2 = new Group(sShell, SWT.NONE);
		group2.setLayout(new GridLayout());
		group2.setLayoutData(gridData4);
		label3 = new Label(group2, SWT.LEFT | SWT.WRAP | SWT.SHADOW_OUT);
		label3.setText(Messages.getString("LABEL.ERRORCONTENTS"));
		dlgShell.pack();
	}

	private void createCombo1() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.widthHint = 96;
		gridData5.grabExcessHorizontalSpace = true;
		COMBO_DBSTATUS_ONOFF = new Combo(group1, SWT.DROP_DOWN);
		COMBO_DBSTATUS_ONOFF.setLayoutData(gridData5);
	}

	private void setinfo() {
		COMBO_DBSTATUS_ONOFF.add("ON", 0);
		COMBO_DBSTATUS_ONOFF.add("OFF", 1);
		int idx = COMBO_DBSTATUS_ONOFF.indexOf(MainRegistry.MONPARA_STATUS);
		if (idx < 0)
			idx = 0;
		COMBO_DBSTATUS_ONOFF.select(idx);
		EDIT_DBSTATUS_PERIOD.setText(MainRegistry.MONPARA_INTERVAL);
	}
}
