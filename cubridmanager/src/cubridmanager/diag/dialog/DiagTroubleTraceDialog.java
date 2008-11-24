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
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.List;

public class DiagTroubleTraceDialog extends Dialog {
	private int DiagEndCode = Window.CANCEL;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="31,12"
	private Label label1 = null;
	private Label label2 = null;
	private Label label3 = null;
	private Group group = null;
	private Label label5 = null;
	private Table table = null;
	private List list = null;
	private Button button2 = null;
	private Label label6 = null;
	private Label label7 = null;
	private Label label8 = null;
	private Text text3 = null;
	private Button button3 = null;
	private Button button5 = null;
	private Label label10 = null;
	private Label label11 = null;
	private Label label12 = null;
	private Label label13 = null;
	private Group group1 = null;
	private Label label = null;
	private Label label14 = null;
	private Label label4 = null;
	private Label label9 = null;
	private Button button4 = null;
	private Group group2 = null;
	private Text textArea = null;

	public DiagTroubleTraceDialog(Shell parent) {
		super(parent);
		// TODO Auto-generated constructor stub
	}

	public DiagTroubleTraceDialog(Shell parent, int style) {
		super(parent, style);
		// TODO Auto-generated constructor stub
	}

	public int doModal() {
		createSShell();
		sShell.open();
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
		sShell = new Shell(SWT.DIALOG_TRIM);
		sShell.setText("Problem packaging");
		sShell.setSize(new org.eclipse.swt.graphics.Point(460, 629));
		createGroup();
		label8 = new Label(sShell, SWT.NONE);
		label8.setBounds(new org.eclipse.swt.graphics.Rectangle(25, 531, 101, 18));
		label8.setText("Problem package file");
		text3 = new Text(sShell, SWT.BORDER);
		text3.setBounds(new org.eclipse.swt.graphics.Rectangle(128, 528, 269, 18));
		button3 = new Button(sShell, SWT.NONE);
		button3.setBounds(new org.eclipse.swt.graphics.Rectangle(399, 527, 32, 18));
		button3.setText("...");
		button5 = new Button(sShell, SWT.NONE);
		button5.setBounds(new org.eclipse.swt.graphics.Rectangle(340, 566, 81, 21));
		button5.setText("CANCEL");

		button5.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DiagEndCode = Window.CANCEL;
						sShell.dispose();
					}
				});
		createGroup1();
		button4 = new Button(sShell, SWT.NONE);
		button4.setBounds(new org.eclipse.swt.graphics.Rectangle(233, 565, 81, 21));
		button4.setText("OK");
		createGroup2();
		button4
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DiagEndCode = Window.OK;
						sShell.dispose();
					}
				});
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		group = new Group(sShell, SWT.NONE);
		group.setText("View thread call stack");
		createTable();
		group.setBounds(new org.eclipse.swt.graphics.Rectangle(16, 289, 420, 226));
		list = new List(group, SWT.NONE);
		list.setBounds(new org.eclipse.swt.graphics.Rectangle(221, 22, 183, 135));
		button2 = new Button(group, SWT.NONE);
		button2.setBounds(new org.eclipse.swt.graphics.Rectangle(222, 192, 185, 23));
		button2.setText("Include call stack in Problem package");
		label6 = new Label(group, SWT.NONE);
		label6.setBounds(new org.eclipse.swt.graphics.Rectangle(27, 171, 139, 18));
		label6.setText("DB Server thread list");
		label7 = new Label(group, SWT.NONE);
		label7.setBounds(new org.eclipse.swt.graphics.Rectangle(265, 167, 96, 17));
		label7.setText("Thread call stack");
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		table = new Table(group, SWT.NONE);
		table.setHeaderVisible(true);
		table.setLinesVisible(true);
		table.setBounds(new org.eclipse.swt.graphics.Rectangle(13, 20, 183, 138));
	}

	/**
	 * This method initializes group1
	 * 
	 */
	private void createGroup1() {
		group1 = new Group(sShell, SWT.NONE);
		group1.setText("Problem information");
		group1.setBounds(new org.eclipse.swt.graphics.Rectangle(16, 5, 420, 166));
		label = new Label(group1, SWT.NONE);
		label.setBounds(new org.eclipse.swt.graphics.Rectangle(10, 22, 70, 16));
		label.setText("Error file :");
		label14 = new Label(group1, SWT.NONE);
		label14.setBounds(new org.eclipse.swt.graphics.Rectangle(106, 23, 302, 12));
		label14.setText("/home/CUBRID/subway/subway.err");
		label1 = new Label(group1, SWT.NONE);
		label1.setBounds(new org.eclipse.swt.graphics.Rectangle(10, 72, 94, 16));
		label1.setText("Error number :");
		label2 = new Label(group1, SWT.NONE);
		label2.setBounds(new org.eclipse.swt.graphics.Rectangle(10, 96, 92, 16));
		label2.setText("occur time :");
		label3 = new Label(group1, SWT.NONE);
		label3.setBounds(new org.eclipse.swt.graphics.Rectangle(10, 120, 81, 16));
		label3.setText("core file :");

		label5 = new Label(group1, SWT.NONE);
		label5.setBounds(new org.eclipse.swt.graphics.Rectangle(105, 44, 285, 17));
		label5.setText("subway");
		label10 = new Label(group1, SWT.NONE);
		label10.setBounds(new org.eclipse.swt.graphics.Rectangle(104, 143, 300, 15));
		label10.setText("/home/CUBRID/conf/cubrid.conf");
		label11 = new Label(group1, SWT.NONE);
		label11.setBounds(new org.eclipse.swt.graphics.Rectangle(106, 70, 181, 15));
		label11.setText("-110");
		label12 = new Label(group1, SWT.NONE);
		label12.setBounds(new org.eclipse.swt.graphics.Rectangle(106, 94, 180, 15));
		label12.setText("2006/07/21 10:12:16");
		label13 = new Label(group1, SWT.NONE);
		label13.setBounds(new org.eclipse.swt.graphics.Rectangle(106, 118, 294, 17));
		label13.setText("/home/CUBRID/core");
		label4 = new Label(group1, SWT.NONE);
		label4.setBounds(new org.eclipse.swt.graphics.Rectangle(8, 143, 75, 16));
		label4.setText("Setting file :");
		label9 = new Label(group1, SWT.NONE);
		label9.setBounds(new org.eclipse.swt.graphics.Rectangle(11, 46, 83, 18));
		label9.setText("Database :");
	}

	/**
	 * This method initializes group2
	 * 
	 */
	private void createGroup2() {
		group2 = new Group(sShell, SWT.NONE);
		group2.setText("Error information");
		group2.setBounds(new org.eclipse.swt.graphics.Rectangle(17, 178, 417, 105));
		textArea = new Text(group2, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL | SWT.BORDER);
		textArea.setBounds(new org.eclipse.swt.graphics.Rectangle(9, 17, 398, 79));
		textArea.setEditable(false);
	}
}
