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
import cubridmanager.Messages;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Spinner;

import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBTriggers;
import cubridmanager.cubrid.Trigger;
import cubridmanager.CommonTool;
import java.util.ArrayList;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class ALTER_TRIGGERDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Group group1 = null;
	private Button CHECK_ALTER_TRIGGER = null;
	private Label label2 = null;
	private Spinner SPIN_ALTER_TRIGGER = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private ArrayList tinfo = null;
	private Trigger trigger = null;
	private boolean ret = false;
	private CLabel cLabel = null;

	public ALTER_TRIGGERDialog(Shell parent) {
		super(parent);
	}

	public ALTER_TRIGGERDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
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
		return ret;
	}

	private void createSShell() {
		dlgShell = new Shell(super.getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.ALTER_TRIGGERDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData13 = new org.eclipse.swt.layout.GridData();
		gridData13.widthHint = 97;
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.horizontalSpan = 2;
		GridLayout gridLayout11 = new GridLayout();
		gridLayout11.numColumns = 2;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData4.widthHint = 70;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData3.widthHint = 70;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.grabExcessHorizontalSpace = true;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 3;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.widthHint = 264;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.THESTATUSANDPRIORITY"));
		label1.setLayoutData(gridData);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.STATUSPRIORITY"));
		group1.setLayout(gridLayout11);
		group1.setLayoutData(gridData1);
		CHECK_ALTER_TRIGGER = new Button(group1, SWT.CHECK);
		CHECK_ALTER_TRIGGER
				.setText(Messages.getString("CHECK.ACTIVATETRIGGER"));
		CHECK_ALTER_TRIGGER.setLayoutData(gridData12);
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.PRIORITY"));
		SPIN_ALTER_TRIGGER = new Spinner(group1, SWT.BORDER);
		SPIN_ALTER_TRIGGER.setDigits(2);
		SPIN_ALTER_TRIGGER.setLayoutData(gridData13);
		SPIN_ALTER_TRIGGER.setMaximum(999999);
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData2);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData3);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String actstr = (CHECK_ALTER_TRIGGER.getSelection()) ? "ACTIVE"
								: "INACTIVE";
						double pri = SPIN_ALTER_TRIGGER.getSelection() / 100.0;
						double oldpri = CommonTool.atof(trigger.Priority);
						if (pri != oldpri || !actstr.equals(trigger.Status)) {
							String msg = "dbname:" + CubridView.Current_db
									+ "\n";
							msg += "triggername:" + trigger.Name + "\n";
							if (!actstr.equals(trigger.Status))
								msg += "status:" + actstr + "\n";
							if (pri != oldpri)
								msg += "priority:" + pri + "\n";

							ClientSocket cs = new ClientSocket();
							if (!cs.SendBackGround(dlgShell, msg,
									"altertrigger", Messages
											.getString("WAITING.ALTERTRIGGER"))) {
								CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
								return;
							}

							CommonTool.MsgBox(dlgShell, Messages
									.getString("MSG.SUCCESS"), Messages
									.getString("MSG.ALTERTRIGGERSUCCESS"));

							cs = new ClientSocket();
							if (!cs.SendClientMessage(dlgShell, "dbname:"
									+ CubridView.Current_db, "gettriggerinfo")) {
								CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
								return;
							}
						}
						ret = true;
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
						ret = false;
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	private void setinfo() {
		tinfo = DBTriggers.triggerinfo;
		for (int i = 0, n = tinfo.size(); i < n; i++) {
			trigger = (Trigger) tinfo.get(i);
			if (trigger.Name.equals(DBTriggers.Current_select))
				break;
		}
		SPIN_ALTER_TRIGGER.setSelection((int) (CommonTool
				.atof(trigger.Priority) * 100));
		if (trigger.Status.equals("ACTIVE"))
			CHECK_ALTER_TRIGGER.setSelection(true);
		else
			CHECK_ALTER_TRIGGER.setSelection(false);
		SPIN_ALTER_TRIGGER.setToolTipText(Messages
				.getString("TOOLTIP.ALTERTRIGPRIORITY"));
		CHECK_ALTER_TRIGGER.setToolTipText(Messages
				.getString("TOOLTIP.ALTERTRIGSTATE"));
	}

}
