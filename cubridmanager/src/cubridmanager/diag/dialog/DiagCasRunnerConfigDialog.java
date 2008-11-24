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

package cubridmanager.diag.dialog;

import org.eclipse.jface.window.Window;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Spinner;
import org.eclipse.swt.widgets.Button;

import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cas.CASItem;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.LogFileInfo;

import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;

public class DiagCasRunnerConfigDialog extends Dialog {
	private int DiagEndCode = Window.CANCEL;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="31,9"
	private Group group = null;
	private Label labelBrokerName = null;
	private Label labelUserName = null;
	private Label labelPassword = null;
	private Label labelNumThread = null;
	private Label labelRepeatCount = null;
	private Combo comboBrokerName = null;
	private Text textUserName = null;
	private Text textPassword = null;
	private Spinner spinnerNumThread = null;
	private Spinner spinnerRepeatCount = null;
	private Button buttonOK = null;
	private Button buttonCancel = null;
	public static String brokerName = new String();
	public static String userName = new String();
	public static String password = new String();
	public static String numThread = new String();
	public static String numRepeatCount = new String();
	public static String dbname = new String();
	public static boolean showqueryresult = false;
	public static boolean showqueryplan = false;
	private Button checkBoxShowQueryResult = null;
	private Label labelDBName = null;
	private Combo comboDBName = null;
	public String logstring = new String();
	public String logfile = new String();
	private Label label = null;
	public boolean execwithFile = false;
	private Button checkBoxShowQueryPlan = null;

	public DiagCasRunnerConfigDialog(Shell parent) {
		super(parent);
		// TODO Auto-generated constructor stub
	}

	public DiagCasRunnerConfigDialog(Shell parent, int style) {
		super(parent, style);
		// TODO Auto-generated constructor stub
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(sShell);
		sShell.open();

		if (execwithFile) {
			checkBoxShowQueryResult.setSelection(false);
			checkBoxShowQueryResult.setEnabled(false);
		}

		for (int i = 0, n = MainRegistry.Authinfo.size(); i < n; i++) {
			AuthItem aitem = (AuthItem) MainRegistry.Authinfo.get(i);
			comboDBName.add(aitem.dbname);
		}

		if ((dbname.length() == 0) && (userName.length() == 0)) {
			if (logstring.length() > 0) {
				int dbnameindex = logstring.indexOf("connect");
				if (dbnameindex > 0) {
					dbnameindex += 8;
					int idbuser_start, idbuser_end;
					idbuser_start = logstring.indexOf(" ", dbnameindex);
					idbuser_end = logstring.indexOf("\n", idbuser_start);
					if ((idbuser_start > 0) && (idbuser_end > idbuser_start)) {
						String dbname = logstring.substring(dbnameindex,
								idbuser_start);
						String username = logstring.substring(
								idbuser_start + 1, idbuser_end - 1);
						for (int i = 0, n = comboDBName.getItemCount(); i < n; i++) {
							if (comboDBName.getItem(i).equals(dbname)) {
								comboDBName.setText(dbname);
								break;
							}
						}

						textUserName.setText(username);
					}
				}
			}
		} else {
			comboDBName.setText(dbname);
			textUserName.setText(userName);
		}

		if (brokerName.length() == 0) {
			int numbroker = MainRegistry.CASinfo.size();
			String targetBroker = new String("");
			for (int i = 0; i < numbroker; i++) {
				CASItem item = (CASItem) MainRegistry.CASinfo.get(i);
				comboBrokerName.add(item.broker_name);
				if (targetBroker.equals("")) {
					for (int j = 0, n = item.loginfo.size(); j < n; j++) {
						LogFileInfo loginfo = (LogFileInfo) item.loginfo.get(j);
						if (loginfo.path.equals(logfile)) {
							targetBroker = item.broker_name;
							break;
						}
					}
				}
			}

			comboBrokerName.setText(targetBroker);
		} else {
			comboBrokerName.setText(brokerName);
		}

		if (password.length() != 0)
			textPassword.setText(password);
		if (numThread.length() != 0) {
			try {
				spinnerNumThread.setSelection(Integer.parseInt(numThread));
			} catch (Exception ee) {
				spinnerNumThread.setSelection(1);
			}
		}
		if (numRepeatCount.length() != 0) {
		}
		{
			try {
				spinnerRepeatCount.setSelection(Integer
						.parseInt(numRepeatCount));
			} catch (Exception ee) {
				spinnerRepeatCount.setSelection(1);
			}

			if (!execwithFile) {
				checkBoxShowQueryResult.setEnabled(true);
				checkBoxShowQueryResult.setSelection(showqueryresult);
				checkBoxShowQueryPlan.setEnabled(showqueryresult);
				checkBoxShowQueryPlan.setSelection(showqueryplan);
			}
		}

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}

		return DiagEndCode;
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		GridData gridData16 = new org.eclipse.swt.layout.GridData(
				GridData.FILL_HORIZONTAL);
		GridData gridData15 = new org.eclipse.swt.layout.GridData();
		gridData15.widthHint = 75;
		GridData gridData14 = new org.eclipse.swt.layout.GridData();
		gridData14.widthHint = 75;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 3;
		// sShell = new Shell(SWT.DIALOG_TRIM | SWT.APPLICATION_MODAL);
		sShell = new Shell(getParent(), SWT.DIALOG_TRIM | SWT.APPLICATION_MODAL);
		sShell.setText(Messages.getString("TITLE.SETCASRUNNERCONFIG"));
		sShell.setLayout(gridLayout1);
		createGroup();
		label = new Label(sShell, SWT.NONE);
		label.setLayoutData(gridData16);
		buttonOK = new Button(sShell, SWT.NONE);
		buttonOK.setText(Messages.getString("BUTTON.OK"));
		buttonOK.setLayoutData(gridData14);
		buttonOK.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (textUserName.getText().length() < 1) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.INSERTUSERID"));
							return;
						}
						if (comboDBName.getText().length() < 1) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.NODBNAME"));
							return;
						}
						if (comboBrokerName.getText().length() < 1) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.SELECTBROKER"));
							return;
						}

						brokerName = comboBrokerName.getText();
						userName = textUserName.getText();
						password = textPassword.getText();
						numThread = Integer.toString(spinnerNumThread
								.getSelection());
						numRepeatCount = Integer.toString(spinnerRepeatCount
								.getSelection());
						showqueryresult = checkBoxShowQueryResult
								.getSelection();
						showqueryplan = checkBoxShowQueryPlan.getSelection();
						dbname = comboDBName.getText();
						DiagEndCode = Window.OK;
						sShell.dispose();
					}
				});
		buttonCancel = new Button(sShell, SWT.NONE);
		buttonCancel.setText(Messages.getString("BUTTON.CANCEL"));
		buttonCancel.setLayoutData(gridData15);
		buttonCancel.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DiagEndCode = Window.CANCEL;
						sShell.dispose();
					}
				});

		sShell.pack(true);
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		GridData gridData17 = new org.eclipse.swt.layout.GridData();
		gridData17.horizontalSpan = 2;
		GridData gridData13 = new org.eclipse.swt.layout.GridData();
		gridData13.horizontalSpan = 3;
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.horizontalAlignment = GridData.FILL;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.horizontalAlignment = GridData.FILL;
		GridData gridData10 = new org.eclipse.swt.layout.GridData();
		gridData10.horizontalAlignment = GridData.FILL;
		gridData10.heightHint = 15;
		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.horizontalAlignment = GridData.FILL;
		gridData9.heightHint = 15;
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.horizontalSpan = 2;
		gridData6.widthHint = 196;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		group = new Group(sShell, SWT.NONE);
		labelDBName = new Label(group, SWT.NONE);
		createComboDBName();
		group.setLayoutData(gridData13);
		group.setLayout(gridLayout);
		labelBrokerName = new Label(group, SWT.NONE);
		labelBrokerName.setText(Messages.getString("LABEL.BROKERNAME"));
		createComboBrokerName();
		labelUserName = new Label(group, SWT.NONE);
		labelUserName.setText(Messages.getString("LABEL.USERID"));
		textUserName = new Text(group, SWT.BORDER);
		textUserName.setLayoutData(gridData9);
		labelPassword = new Label(group, SWT.NONE);
		labelPassword.setText(Messages.getString("LABEL.PASSWORD1"));
		textPassword = new Text(group, SWT.BORDER | SWT.PASSWORD);
		textPassword.setLayoutData(gridData10);
		labelNumThread = new Label(group, SWT.NONE);
		labelNumThread.setText(Messages.getString("LABEL.NUMTHREAD"));
		spinnerNumThread = new Spinner(group, SWT.BORDER);
		spinnerNumThread.setMaximum(20);
		spinnerNumThread.setLayoutData(gridData11);
		spinnerNumThread.setMinimum(1);
		labelRepeatCount = new Label(group, SWT.NONE);
		labelRepeatCount.setText(Messages.getString("LABEL.NUMREPEATCOUNT"));
		spinnerRepeatCount = new Spinner(group, SWT.BORDER);
		spinnerRepeatCount.setMaximum(1000);
		spinnerRepeatCount.setMinimum(1);
		spinnerRepeatCount.setLayoutData(gridData12);
		spinnerRepeatCount.setDigits(0);
		checkBoxShowQueryResult = new Button(group, SWT.CHECK);
		checkBoxShowQueryResult.setText(Messages
				.getString("LABEL.VIEWCASRUNNERQUERYRESULT"));
		checkBoxShowQueryResult.setLayoutData(gridData6);
		checkBoxShowQueryResult
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						checkBoxShowQueryPlan
								.setEnabled(checkBoxShowQueryResult
										.getSelection());
					}
				});
		checkBoxShowQueryPlan = new Button(group, SWT.CHECK);
		checkBoxShowQueryPlan.setEnabled(false);
		checkBoxShowQueryPlan.setLayoutData(gridData17);
		checkBoxShowQueryPlan.setText(Messages
				.getString("LABEL.VIEWCASRUNNERQUERYPLAN"));
		labelDBName.setText(Messages.getString("LABEL.DATABASE"));
	}

	/**
	 * This method initializes comboBrokerName
	 * 
	 */
	private void createComboBrokerName() {
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.horizontalAlignment = GridData.FILL;
		comboBrokerName = new Combo(group, SWT.NONE);
		comboBrokerName.setLayoutData(gridData8);
	}

	/**
	 * This method initializes comboDBName
	 * 
	 */
	private void createComboDBName() {
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.horizontalAlignment = GridData.FILL;
		comboDBName = new Combo(group, SWT.NONE);
		comboDBName.setLayoutData(gridData7);
	}

}
