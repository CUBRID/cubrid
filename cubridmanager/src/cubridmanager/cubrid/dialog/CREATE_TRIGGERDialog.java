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
import cubridmanager.CommonTool;
import cubridmanager.cubrid.view.CubridView;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.widgets.Spinner;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class CREATE_TRIGGERDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Label label2 = null;
	private Text EDIT_TRIGGER_NAME = null;
	private Combo COMBO_TRIGGER_EVENTTIME = null;
	private Combo COMBO_TRIGGER_EVENTTYPE = null;
	private Label label3 = null;
	private Text EDIT_TRIGGER_EVENTTARGET = null;
	private Label label4 = null;
	private Text EDIT_TRIGGER_CONDITION = null;
	private Label label5 = null;
	private Combo COMBO_TRIGGER_DELAYED_TIME = null;
	private Combo COMBO_TRIGGER_ACTION = null;
	private Text EDIT_TRIGGER_ACTION = null;
	private Group group1 = null;
	private Button RADIO_TRIGGER_STATUS = null;
	private Button RADIO_TRIGGER_PRIORITY = null;
	private CLabel clabel1 = null;
	private Label label6 = null;
	private Combo COMBO_TRIGGER_STATUS = null;
	private Label label7 = null;
	private Spinner SPIN_TRIGGER_PRIORITY = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;
	private Label label = null;
	private Label label8 = null;
	private Group grpCreateTrigger = null;

	public CREATE_TRIGGERDialog(Shell parent) {
		super(parent);
	}

	public CREATE_TRIGGERDialog(Shell parent, int style) {
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
		dlgShell.setText(Messages.getString("TITLE.CREATE_TRIGGERDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.heightHint = -1;
		gridData.widthHint = 400;
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.SQLXTRIGGER"));
		label1.setLayoutData(gridData);

		createGrpCreateTrigger();
		createGroup1();

		GridData gridData13 = new org.eclipse.swt.layout.GridData();
		gridData13.widthHint = 80;
		gridData13.grabExcessHorizontalSpace = true;
		gridData13.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData13);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String CR = "\r";
						String NL = "\n";
						String evtime = COMBO_TRIGGER_EVENTTIME.getText();
						String evtype = COMBO_TRIGGER_EVENTTYPE.getText();
						String dlytime = COMBO_TRIGGER_DELAYED_TIME.getText();
						String trigact = COMBO_TRIGGER_ACTION.getText();
						String trigstat = COMBO_TRIGGER_STATUS.getText();
						String edittrigact = EDIT_TRIGGER_ACTION.getText()
								.trim();
						edittrigact = edittrigact.replaceAll(CR,"");
						edittrigact = edittrigact.replaceAll(NL," ");
						int trigpri = SPIN_TRIGGER_PRIORITY.getSelection();
						String trigname = EDIT_TRIGGER_NAME.getText().trim();
						String trigdest = EDIT_TRIGGER_EVENTTARGET.getText()
								.trim();
						trigdest = trigdest.replaceAll(CR,"");
						trigdest = trigdest.replaceAll(NL," ");
						String trigcond = EDIT_TRIGGER_CONDITION.getText()
								.trim();
						trigcond = trigcond.replaceAll(CR,"");
						trigcond = trigcond.replaceAll(NL," ");

						if (trigact.equals("OTHER STATEMENT")) {
							trigact = edittrigact;
						} else if (trigact.equals("PRINT")) {
							trigact = "PRINT '" + edittrigact + "'";
						}

						String chkstr = CommonTool
								.ValidateCheckInIdentifier(trigname);
						if (chkstr.length() > 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INVALIDTRIGGERNAME"));
							return;
						}

						if (!evtype.equals("COMMIT")
								&& !evtype.equals("ROLLBACK")
								&& trigdest.length() <= 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.ENTEREVENTTARGET"));
							return;
						}

						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "triggername:" + trigname + "\n";
						msg += "conditiontime:" + evtime + "\n";
						msg += "eventtype:" + evtype + "\n";
						msg += "action:" + trigact + "\n";
						if (trigdest.length() > 0) {
							msg += "eventtarget:" + trigdest + "\n";
						}
						if (trigcond.length() > 0) {
							msg += "condition:" + trigcond + "\n";
						}
						if (!dlytime.startsWith("--")) { // action time selected
							msg += "actiontime:" + dlytime + "\n";
						}
						if (RADIO_TRIGGER_STATUS.getSelection()) {// is checked
							msg += "status:" + trigstat + "\n";
						}
						if (RADIO_TRIGGER_PRIORITY.getSelection()) {// is checked
							msg += "priority:" + (trigpri / 100.0) + "\n";
						}

						ClientSocket cs = new ClientSocket();
						if (!cs.SendBackGround(dlgShell, msg, "addtrigger",
								Messages.getString("WAITING.NEWTRIGGER"))) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}

						CommonTool.MsgBox(dlgShell, Messages
								.getString("MSG.SUCCESS"), Messages
								.getString("MSG.NEWTRIGGERSUCCESS"));

						cs = new ClientSocket();
						if (!cs.SendClientMessage(dlgShell, "dbname:"
								+ CubridView.Current_db, "gettriggerinfo")) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}
						ret = true;
						dlgShell.dispose();
					}
				});

		GridData gridData14 = new org.eclipse.swt.layout.GridData();
		gridData14.widthHint = 80;
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData14);
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

	private void createGrpCreateTrigger() {
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.grabExcessHorizontalSpace = true;
		grpCreateTrigger = new Group(sShell, SWT.NONE);
		grpCreateTrigger.setText(Messages.getString("LABEL.CREATETRIGGER"));
		grpCreateTrigger.setLayoutData(gridData1);
		grpCreateTrigger.setLayout(new GridLayout(2, false));

		label1 = new Label(grpCreateTrigger, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("TABLE.NAME"));
		label1.setLayoutData(new GridData(120, -1));

		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		EDIT_TRIGGER_NAME = new Text(grpCreateTrigger, SWT.BORDER);
		EDIT_TRIGGER_NAME.setLayoutData(gridData2);

		label2 = new Label(grpCreateTrigger, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("TABLE.CONDITIONAPPLY"));
		createCombo1();

		label = new Label(grpCreateTrigger, SWT.LEFT | SWT.WRAP);
		label.setText(Messages.getString("TABLE.EVENT"));
		createCombo2();

		label3 = new Label(grpCreateTrigger, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("TABLE.COMPENSATION"));

		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		EDIT_TRIGGER_EVENTTARGET = new Text(grpCreateTrigger, SWT.BORDER);
		EDIT_TRIGGER_EVENTTARGET.setLayoutData(gridData6);

		label4 = new Label(grpCreateTrigger, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("TABLE.CONDITION"));

		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		EDIT_TRIGGER_CONDITION = new Text(grpCreateTrigger, SWT.BORDER);
		EDIT_TRIGGER_CONDITION.setLayoutData(gridData8);

		label5 = new Label(grpCreateTrigger, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("TABLE.EXECUTIONTIME"));
		createCombo3();

		label8 = new Label(grpCreateTrigger, SWT.LEFT | SWT.WRAP);
		label8.setText(Messages.getString("TABLE.CONTENT"));
		createCombo4();

		new Label(grpCreateTrigger, SWT.NONE);

		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData11.heightHint = 50;
		EDIT_TRIGGER_ACTION = new Text(grpCreateTrigger, SWT.BORDER | SWT.MULTI
				| SWT.WRAP | SWT.V_SCROLL);
		EDIT_TRIGGER_ACTION.setLayoutData(gridData11);
	}

	private void createGroup1() {
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 4;
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.horizontalSpan = 2;
		gridData12.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData12.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.OPTIONAL"));
		group1.setLayout(gridLayout2);
		group1.setLayoutData(gridData12);

		GridData gridData61 = new org.eclipse.swt.layout.GridData();
		gridData61.grabExcessHorizontalSpace = true;
		RADIO_TRIGGER_STATUS = new Button(group1, SWT.CHECK);
		RADIO_TRIGGER_STATUS.setText(Messages.getString("RADIO.SELECTTRIGGER"));
		RADIO_TRIGGER_STATUS.setLayoutData(gridData61);
		RADIO_TRIGGER_STATUS
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (RADIO_TRIGGER_STATUS.getSelection())
							COMBO_TRIGGER_STATUS.setEnabled(true);
						else
							COMBO_TRIGGER_STATUS.setEnabled(false);
					}
				});

		GridData gridData31 = new org.eclipse.swt.layout.GridData();
		gridData31.widthHint = 3;
		gridData31.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData31.verticalSpan = 2;
		gridData31.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		clabel1 = new CLabel(group1, SWT.SHADOW_IN);
		clabel1.setLayoutData(gridData31);

		label6 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.TRIGGERSTATUS"));

		createCombo5();

		RADIO_TRIGGER_PRIORITY = new Button(group1, SWT.CHECK);
		RADIO_TRIGGER_PRIORITY.setText(Messages
				.getString("RADIO.SETTRIGGERPRIORITY"));
		RADIO_TRIGGER_PRIORITY
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (RADIO_TRIGGER_PRIORITY.getSelection())
							SPIN_TRIGGER_PRIORITY.setEnabled(true);
						else
							SPIN_TRIGGER_PRIORITY.setEnabled(false);
					}
				});
		label7 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label7.setText(Messages.getString("LABEL.TRIGGERPRIORITY"));

		GridData gridData41 = new org.eclipse.swt.layout.GridData();
		gridData41.widthHint = 114;
		SPIN_TRIGGER_PRIORITY = new Spinner(group1, SWT.BORDER);
		SPIN_TRIGGER_PRIORITY.setMaximum(999999);
		SPIN_TRIGGER_PRIORITY.setLayoutData(gridData41);
		SPIN_TRIGGER_PRIORITY.setDigits(2);
	}

	private void createCombo1() {
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		COMBO_TRIGGER_EVENTTIME = new Combo(grpCreateTrigger, SWT.DROP_DOWN);
		COMBO_TRIGGER_EVENTTIME.setLayoutData(gridData3);
	}

	private void createCombo2() {
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		COMBO_TRIGGER_EVENTTYPE = new Combo(grpCreateTrigger, SWT.DROP_DOWN);
		COMBO_TRIGGER_EVENTTYPE.setLayoutData(gridData4);
		COMBO_TRIGGER_EVENTTYPE
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						String curtype = COMBO_TRIGGER_EVENTTYPE.getText();
						if (curtype.equals("COMMIT")
								|| curtype.equals("ROLLBACK")) {
							EDIT_TRIGGER_EVENTTARGET.setText("");
							EDIT_TRIGGER_CONDITION.setText("");
							EDIT_TRIGGER_EVENTTARGET.setEnabled(false);
							EDIT_TRIGGER_CONDITION.setEnabled(false);
						} else {
							EDIT_TRIGGER_EVENTTARGET.setEnabled(true);
							EDIT_TRIGGER_CONDITION.setEnabled(true);
						}
					}
				});
	}

	private void createCombo3() {
		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		COMBO_TRIGGER_DELAYED_TIME = new Combo(grpCreateTrigger, SWT.DROP_DOWN);
		COMBO_TRIGGER_DELAYED_TIME.setLayoutData(gridData9);
	}

	private void createCombo4() {
		GridData gridData10 = new org.eclipse.swt.layout.GridData();
		gridData10.horizontalAlignment = SWT.FILL;
		COMBO_TRIGGER_ACTION = new Combo(grpCreateTrigger, SWT.DROP_DOWN);
		COMBO_TRIGGER_ACTION.setLayoutData(gridData10);
		COMBO_TRIGGER_ACTION
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						String curact = COMBO_TRIGGER_ACTION.getText();
						if (curact.equals("REJECT")
								|| curact.equals("INVALIDATE TRANSACTION")) {
							EDIT_TRIGGER_ACTION.setEnabled(false);
						} else {
							EDIT_TRIGGER_ACTION.setEnabled(true);
							EDIT_TRIGGER_ACTION.setFocus();
						}
					}
				});
	}

	private void createCombo5() {
		GridData gridData16 = new org.eclipse.swt.layout.GridData();
		gridData16.widthHint = 114;
		gridData16.grabExcessHorizontalSpace = true;
		COMBO_TRIGGER_STATUS = new Combo(group1, SWT.DROP_DOWN);
		COMBO_TRIGGER_STATUS.setLayoutData(gridData16);
	}

	private void setinfo() {
		COMBO_TRIGGER_EVENTTIME.add("BEFORE", 0);
		COMBO_TRIGGER_EVENTTIME.add("AFTER", 1);
		COMBO_TRIGGER_EVENTTIME.add("DEFERRED", 2);
		COMBO_TRIGGER_EVENTTIME.select(0);

		COMBO_TRIGGER_EVENTTYPE.add("INSERT", 0);
		COMBO_TRIGGER_EVENTTYPE.add("UPDATE", 1);
		COMBO_TRIGGER_EVENTTYPE.add("DELETE", 2);
		COMBO_TRIGGER_EVENTTYPE.add("STATEMENT INSERT", 3);
		COMBO_TRIGGER_EVENTTYPE.add("STATEMENT UPDATE", 4);
		COMBO_TRIGGER_EVENTTYPE.add("STATEMENT DELETE", 5);
		COMBO_TRIGGER_EVENTTYPE.add("COMMIT", 6);
		COMBO_TRIGGER_EVENTTYPE.add("ROLLBACK", 7);
		COMBO_TRIGGER_EVENTTYPE.select(0);

		COMBO_TRIGGER_DELAYED_TIME.add("--------", 0);
		COMBO_TRIGGER_DELAYED_TIME.add("AFTER", 1);
		COMBO_TRIGGER_DELAYED_TIME.add("DEFERRED", 2);
		COMBO_TRIGGER_DELAYED_TIME.select(0);

		COMBO_TRIGGER_ACTION.add("REJECT", 0);
		COMBO_TRIGGER_ACTION.add("INVALIDATE TRANSACTION", 1);
		COMBO_TRIGGER_ACTION.add("PRINT", 2);
		COMBO_TRIGGER_ACTION.add("OTHER STATEMENT", 3);
		COMBO_TRIGGER_ACTION.select(0);

		EDIT_TRIGGER_ACTION.setEnabled(false);

		RADIO_TRIGGER_STATUS.setSelection(false);
		RADIO_TRIGGER_PRIORITY.setSelection(false);
		COMBO_TRIGGER_STATUS.add("ACTIVE", 0);
		COMBO_TRIGGER_STATUS.add("INACTIVE", 1);
		COMBO_TRIGGER_STATUS.select(0);
		COMBO_TRIGGER_STATUS.setEnabled(false);
		SPIN_TRIGGER_PRIORITY.setEnabled(false);

		COMBO_TRIGGER_EVENTTIME.setToolTipText(Messages
				.getString("TOOLTIP.NEWTRIGGERCOMBOEVENTTIME"));
		SPIN_TRIGGER_PRIORITY.setToolTipText(Messages
				.getString("TOOLTIP.NEWTRIGGERSPINPRIORITY"));
		COMBO_TRIGGER_STATUS.setToolTipText(Messages
				.getString("TOOLTIP.NEWTRIGGERCOMBOSTATUS"));
		EDIT_TRIGGER_ACTION.setToolTipText(Messages
				.getString("TOOLTIP.NEWTRIGGEREDITACTION"));
		COMBO_TRIGGER_ACTION.setToolTipText(Messages
				.getString("TOOLTIP.NEWTRIGGERCOMBOACTION"));
		COMBO_TRIGGER_DELAYED_TIME.setToolTipText(Messages
				.getString("TOOLTIP.NEWTRIGGERCOMBODELAYEDTIME"));
		COMBO_TRIGGER_EVENTTYPE.setToolTipText(Messages
				.getString("TOOLTIP.NEWTRIGGERCOMBOEVENTTYPE"));
		EDIT_TRIGGER_NAME.setToolTipText(Messages
				.getString("TOOLTIP.NEWTRIGGEREDITNAME"));
		EDIT_TRIGGER_EVENTTARGET.setToolTipText(Messages
				.getString("TOOLTIP.NEWTRIGGEREDITEVENTTARGET"));
		EDIT_TRIGGER_CONDITION.setToolTipText(Messages
				.getString("TOOLTIP.NEWTRIGGEREDITCONDITION"));
	}
}
