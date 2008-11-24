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
import cubridmanager.cas.BrokerJobStatus;
import cubridmanager.cas.view.CASView;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class BROKER_JOB_PRIODialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Table LIST_JOB_PRIO = null;
	private Button BUTTON_JOB_UP = null;
	private Button BUTTON_JOB_DOWN = null;
	private Button BUTTON_JOB_PRIO_APPLY = null;
	private Button IDOK = null;
	private boolean ret = false;
	private Label label = null;

	public BROKER_JOB_PRIODialog(Shell parent) {
		super(parent);
	}

	public BROKER_JOB_PRIODialog(Shell parent, int style) {
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
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TOOL.JOBPRIORITYACTION"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData38 = new org.eclipse.swt.layout.GridData();
		gridData38.widthHint = 50;
		gridData38.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData38.verticalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData38.grabExcessVerticalSpace = true;
		GridData gridData37 = new org.eclipse.swt.layout.GridData();
		gridData37.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData37.widthHint = 50;
		gridData37.grabExcessVerticalSpace = true;
		gridData37.verticalAlignment = org.eclipse.swt.layout.GridData.END;
		GridLayout gridLayout36 = new GridLayout();
		gridLayout36.numColumns = 2;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 100;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 100;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessHorizontalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 3;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.JOBPRIORITY"));
		group1.setLayoutData(gridData);
		group1.setLayout(gridLayout36);
		createTable1();
		BUTTON_JOB_UP = new Button(group1, SWT.NONE);
		BUTTON_JOB_UP.setText(Messages.getString("BUTTON.UP"));
		BUTTON_JOB_UP.setLayoutData(gridData37);
		BUTTON_JOB_UP
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int selidx = LIST_JOB_PRIO.getSelectionIndex();
						int idxcnt = LIST_JOB_PRIO.getItemCount();
						if (selidx <= 0 || idxcnt <= 1)
							return;
						TableItem ti1 = LIST_JOB_PRIO.getItem(selidx);
						TableItem ti2 = LIST_JOB_PRIO.getItem(selidx - 1);
						for (int i = 0; i < 4; i++) {
							String tmp = ti1.getText(i);
							ti1.setText(i, ti2.getText(i));
							ti2.setText(i, tmp);
						}
						LIST_JOB_PRIO.redraw();
					}
				});
		BUTTON_JOB_DOWN = new Button(group1, SWT.NONE);
		BUTTON_JOB_DOWN.setText(Messages.getString("BUTTON.DOWN"));
		BUTTON_JOB_DOWN.setLayoutData(gridData38);
		BUTTON_JOB_DOWN
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int selidx = LIST_JOB_PRIO.getSelectionIndex();
						int idxcnt = LIST_JOB_PRIO.getItemCount();
						if (selidx < 0 || idxcnt <= 1 || (selidx + 1) >= idxcnt)
							return;
						TableItem ti1 = LIST_JOB_PRIO.getItem(selidx);
						TableItem ti2 = LIST_JOB_PRIO.getItem(selidx + 1);
						for (int i = 0; i < 4; i++) {
							String tmp = ti1.getText(i);
							ti1.setText(i, ti2.getText(i));
							ti2.setText(i, tmp);
						}
						LIST_JOB_PRIO.redraw();
					}
				});
		label = new Label(sShell, SWT.NONE);
		label.setLayoutData(gridData1);
		BUTTON_JOB_PRIO_APPLY = new Button(sShell, SWT.NONE);
		BUTTON_JOB_PRIO_APPLY.setText(Messages.getString("BUTTON.APPLY"));
		BUTTON_JOB_PRIO_APPLY.setLayoutData(gridData2);
		BUTTON_JOB_PRIO_APPLY
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int idxcnt = LIST_JOB_PRIO.getItemCount();
						if (idxcnt <= 1)
							return;
						for (int i = idxcnt - 1; i >= 0; i--) {
							ClientSocket cs = new ClientSocket();
							if (!cs.SendClientMessage(dlgShell, "bname:"
									+ CASView.Current_broker + "\njobnum:"
									+ LIST_JOB_PRIO.getItem(i).getText(0),
									"broker_job_first")) {
								CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
								return;
							}
						}
						CommonTool.MsgBox(dlgShell, Messages
								.getString("MSG.SUCCESS"), Messages
								.getString("MSG.JOBPRISUCCESS"));
						setinfo();
						ret = true;
					}
				});
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.CLOSE"));
		IDOK.setLayoutData(gridData3);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	private void createTable1() {
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.heightHint = 190;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData4.verticalSpan = 2;
		gridData4.widthHint = 300;
		LIST_JOB_PRIO = new Table(group1, SWT.FULL_SELECTION | SWT.BORDER);
		LIST_JOB_PRIO.setLinesVisible(true);
		LIST_JOB_PRIO.setLayoutData(gridData4);
		LIST_JOB_PRIO.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST_JOB_PRIO.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_JOB_PRIO, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.JOBID"));
		tblcol = new TableColumn(LIST_JOB_PRIO, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.IP"));
		tblcol = new TableColumn(LIST_JOB_PRIO, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.TIME"));
		tblcol = new TableColumn(LIST_JOB_PRIO, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.REQUEST"));
	}

	private void setinfo() {
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(dlgShell, "bname:" + CASView.Current_broker,
				"broker_job_info", Messages.getString("WAITING.GETJOBINFO"))) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			return;
		}

		BrokerJobStatus bjs = null;
		LIST_JOB_PRIO.removeAll();
		for (int i = 0, n = MainRegistry.Tmpchkrst.size(); i < n; i++) {
			bjs = (BrokerJobStatus) MainRegistry.Tmpchkrst.get(i);
			TableItem item = new TableItem(LIST_JOB_PRIO, SWT.NONE);
			item.setText(0, bjs.ID);
			item.setText(1, bjs.IP);
			item.setText(2, bjs.jobTime);
			item.setText(3, bjs.Request);
		}
		for (int i = 0, n = LIST_JOB_PRIO.getColumnCount(); i < n; i++) {
			LIST_JOB_PRIO.getColumn(i).pack();
		}
	}
}
