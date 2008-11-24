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
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.Parameters;
import cubridmanager.cubrid.view.CubridView;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Spinner;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class DATABASE_SERVERPROPERTYDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Spinner SPIN_SERVERPROPERTY_ACTIVEREQUEST = null;
	private Spinner SPIN_SERVERPROPERTY_MAXCLIENT = null;
	private Spinner SPIN_SERVERPROPERTY_MAXTHREADS = null;
	private Group group2 = null;
	private Spinner SPIN_SERVERPROPERTY_DEADLOCKINTERVAL = null;
	private Group group3 = null;
	private Spinner SPIN_SERVERPROPERTY_NUMDATABUFFERS = null;
	private Spinner SPIN_SERVERPROPERTY_NUMLOGBUFFERS = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private Label label1 = null;
	private Label label2 = null;
	private Label label3 = null;
	private Label label4 = null;
	private Label label5 = null;
	private Label label6 = null;
	private Label label7 = null;
	private Text EDITLOCKTIMEOUT = null;
	private AuthItem ai = null;

	public DATABASE_SERVERPROPERTYDialog(Shell parent) {
		super(parent);
	}

	public DATABASE_SERVERPROPERTYDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
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
		dlgShell.setText(Messages
				.getString("TITLE.DATABASE_SERVERPROPERTYDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData19 = new org.eclipse.swt.layout.GridData();
		gridData19.widthHint = 95;
		gridData19.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData19.grabExcessHorizontalSpace = true;
		gridData19.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData18 = new org.eclipse.swt.layout.GridData();
		gridData18.widthHint = 95;
		gridData18.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData18.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridLayout gridLayout17 = new GridLayout();
		gridLayout17.numColumns = 2;
		GridData gridData16 = new org.eclipse.swt.layout.GridData();
		gridData16.widthHint = 95;
		gridData16.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData16.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData15 = new org.eclipse.swt.layout.GridData();
		gridData15.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData15.widthHint = 97;
		gridData15.grabExcessHorizontalSpace = true;
		gridData15.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout14 = new GridLayout();
		gridLayout14.numColumns = 2;
		GridData gridData13 = new org.eclipse.swt.layout.GridData();
		gridData13.widthHint = 95;
		gridData13.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData13.grabExcessHorizontalSpace = true;
		gridData13.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.widthHint = 95;
		gridData12.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData12.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData11.widthHint = 95;
		gridData11.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout10 = new GridLayout();
		gridLayout10.numColumns = 2;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 100;
		gridData4.grabExcessHorizontalSpace = true;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData3.widthHint = 100;
		gridData3.grabExcessHorizontalSpace = true;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 2;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.RELATEDTOCLIENT"));
		group1.setLayout(gridLayout10);
		group1.setLayoutData(gridData);
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.RELATEDTOLOCKING"));
		group2.setLayout(gridLayout14);
		group2.setLayoutData(gridData1);
		group3 = new Group(sShell, SWT.NONE);
		group3.setText(Messages.getString("GROUP.RELATEDTOMEMORY"));
		group3.setLayout(gridLayout17);
		group3.setLayoutData(gridData2);
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.ACTIVEREQUESTS"));
		SPIN_SERVERPROPERTY_ACTIVEREQUEST = new Spinner(group1, SWT.BORDER);
		SPIN_SERVERPROPERTY_ACTIVEREQUEST.setMinimum(1);
		SPIN_SERVERPROPERTY_ACTIVEREQUEST.setLayoutData(gridData11);
		SPIN_SERVERPROPERTY_ACTIVEREQUEST.setMaximum(65535);
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.MAXCLIENTS"));
		SPIN_SERVERPROPERTY_MAXCLIENT = new Spinner(group1, SWT.BORDER);
		SPIN_SERVERPROPERTY_MAXCLIENT.setMaximum(65535);
		SPIN_SERVERPROPERTY_MAXCLIENT.setLayoutData(gridData12);
		SPIN_SERVERPROPERTY_MAXCLIENT.setMinimum(1);
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.MAXTHREADS"));
		SPIN_SERVERPROPERTY_MAXTHREADS = new Spinner(group1, SWT.BORDER);
		SPIN_SERVERPROPERTY_MAXTHREADS.setMinimum(2);
		SPIN_SERVERPROPERTY_MAXTHREADS.setLayoutData(gridData13);
		SPIN_SERVERPROPERTY_MAXTHREADS.setMaximum(10240);
		label4 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.LOCKTIMEOUTINSECS"));
		EDITLOCKTIMEOUT = new Text(group2, SWT.BORDER);
		EDITLOCKTIMEOUT.setLayoutData(gridData15);
		label5 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.DEADLOCKDETECTION"));
		SPIN_SERVERPROPERTY_DEADLOCKINTERVAL = new Spinner(group2, SWT.BORDER);
		SPIN_SERVERPROPERTY_DEADLOCKINTERVAL.setMaximum(65535);
		SPIN_SERVERPROPERTY_DEADLOCKINTERVAL.setLayoutData(gridData16);
		SPIN_SERVERPROPERTY_DEADLOCKINTERVAL.setMinimum(1);
		label6 = new Label(group3, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.NUMDATABUFFERS"));
		SPIN_SERVERPROPERTY_NUMDATABUFFERS = new Spinner(group3, SWT.BORDER);
		SPIN_SERVERPROPERTY_NUMDATABUFFERS.setMinimum(5);
		SPIN_SERVERPROPERTY_NUMDATABUFFERS.setLayoutData(gridData18);
		SPIN_SERVERPROPERTY_NUMDATABUFFERS.setMaximum(65535);
		label7 = new Label(group3, SWT.LEFT | SWT.WRAP);
		label7.setText(Messages.getString("LABEL.NUMLOGBUFFERS"));
		SPIN_SERVERPROPERTY_NUMLOGBUFFERS = new Spinner(group3, SWT.BORDER);
		SPIN_SERVERPROPERTY_NUMLOGBUFFERS.setMaximum(65535);
		SPIN_SERVERPROPERTY_NUMLOGBUFFERS.setLayoutData(gridData19);
		SPIN_SERVERPROPERTY_NUMLOGBUFFERS.setMinimum(3);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData3);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int tmp = CommonTool.atoi(EDITLOCKTIMEOUT.getText());
						if (tmp < -1
								|| tmp > 65520
								|| (tmp == 0 && !EDITLOCKTIMEOUT.getText()
										.equals("0"))) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.LOCKTIMEOUTINPUT"));
							return;
						}
						String msg = "dbname:" + CubridView.Current_db + "\n";
						boolean ischanged = false;
						Parameters para;
						for (int i = 0, n = ai.ParaInfo.size(); i < n; i++) {
							para = (Parameters) ai.ParaInfo.get(i);
							int wrk = -999;
							if (para.name.equals("num_log_buffers"))
								wrk = SPIN_SERVERPROPERTY_NUMLOGBUFFERS
										.getSelection();
							else if (para.name.equals("num_data_buffers"))
								wrk = SPIN_SERVERPROPERTY_NUMDATABUFFERS
										.getSelection();
							else if (para.name
									.equals("deadlock_detection_interval"))
								wrk = SPIN_SERVERPROPERTY_DEADLOCKINTERVAL
										.getSelection();
							else if (para.name.equals("lock_timeout_in_secs"))
								wrk = CommonTool
										.atoi(EDITLOCKTIMEOUT.getText());
							else if (para.name.equals("max_threads"))
								wrk = SPIN_SERVERPROPERTY_MAXTHREADS
										.getSelection();
							else if (para.name.equals("max_clients"))
								wrk = SPIN_SERVERPROPERTY_MAXCLIENT
										.getSelection();
							else if (para.name.equals("active_requests"))
								wrk = SPIN_SERVERPROPERTY_ACTIVEREQUEST
										.getSelection();
							if (wrk != -999) { // parameter catch
								if (wrk != CommonTool.atoi(para.value)) { // changed
									msg += "param:" + para.name + "\n";
									msg += "paramval:" + wrk + "\n";
									ischanged = true;
								}
							}
						}
						if (ischanged) {
							ClientSocket cs = new ClientSocket();
							if (!cs
									.SendBackGround(
											dlgShell,
											msg,
											"setsysparam",
											Messages
													.getString("WAITING.CHANGINGSERVERPROPERTY"))) {
								CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
								return;
							}
							CommonTool.MsgBox(dlgShell, Messages
									.getString("MSG.SUCCESS"), Messages
									.getString("MSG.SETSERVERSUCCESS"));
						}
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
		setinfo();
	}

	private void setinfo() {
		ai = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (ai == null) {
			CommonTool.ErrorBox(dlgShell, Messages.getString("MSG.SELECTDB"));
			dlgShell.dispose();
			return;
		}
		Parameters para;
		for (int i = 0, n = ai.ParaInfo.size(); i < n; i++) {
			para = (Parameters) ai.ParaInfo.get(i);
			if (para.name.equals("num_log_buffers"))
				SPIN_SERVERPROPERTY_NUMLOGBUFFERS.setSelection(CommonTool
						.atoi(para.value));
			else if (para.name.equals("num_data_buffers"))
				SPIN_SERVERPROPERTY_NUMDATABUFFERS.setSelection(CommonTool
						.atoi(para.value));
			else if (para.name.equals("deadlock_detection_interval"))
				SPIN_SERVERPROPERTY_DEADLOCKINTERVAL.setSelection(CommonTool
						.atoi(para.value));
			else if (para.name.equals("lock_timeout_in_secs"))
				EDITLOCKTIMEOUT.setText(para.value);
			else if (para.name.equals("max_threads"))
				SPIN_SERVERPROPERTY_MAXTHREADS.setSelection(CommonTool
						.atoi(para.value));
			else if (para.name.equals("max_clients"))
				SPIN_SERVERPROPERTY_MAXCLIENT.setSelection(CommonTool
						.atoi(para.value));
			else if (para.name.equals("active_requests"))
				SPIN_SERVERPROPERTY_ACTIVEREQUEST.setSelection(CommonTool
						.atoi(para.value));
		}
	}
}
