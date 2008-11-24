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
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;

public class DiagActivityImportCASLogDialog extends Dialog {
	private int DiagEndCode = Window.CANCEL;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="38,0"
	private Button button = null;
	private Button button1 = null;
	private Label label = null;
	private Text text = null;
	private Button button2 = null;
	private Label label1 = null;
	private Text text1 = null;
	private Composite compositeBody = null;
	private Label label2 = null;
	private Label label3 = null;
	private Label label12 = null;

	public DiagActivityImportCASLogDialog(Shell parent) {
		super(parent);
		// TODO Auto-generated constructor stub
	}

	public DiagActivityImportCASLogDialog(Shell parent, int style) {
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
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.widthHint = 70;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 70;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 50;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 50;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.widthHint = 100;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 5;
		sShell = new Shell(SWT.DIALOG_TRIM);
		sShell.setText("Convert CUBRID CAS log to active log");
		sShell.setLayout(gridLayout);
		// sShell.setSize(new org.eclipse.swt.graphics.Point(409,138));
		createCompositeBody();
		button
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DiagEndCode = Window.OK;
						sShell.dispose();
					}
				});
		label2 = new Label(sShell, SWT.NONE);
		label2.setLayoutData(gridData1);
		label12 = new Label(sShell, SWT.NONE);
		label12.setLayoutData(gridData2);
		label3 = new Label(sShell, SWT.NONE);
		label3.setLayoutData(gridData3);
		button = new Button(sShell, SWT.NONE);
		button1 = new Button(sShell, SWT.NONE);
		// button.setBounds(new
		// org.eclipse.swt.graphics.Rectangle(74,78,83,22));
		button.setText("OK");
		// button1.setBounds(new
		// org.eclipse.swt.graphics.Rectangle(231,78,83,22));
		button.setLayoutData(gridData4);
		button1.setText("CANCEL");
		button1.setLayoutData(gridData5);
		button1
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DiagEndCode = Window.CANCEL;
						sShell.dispose();
					}
				});

		sShell.pack(true);
	}

	/**
	 * This method initializes compositeBody
	 * 
	 */
	private void createCompositeBody() {
		GridData gridData31 = new org.eclipse.swt.layout.GridData();
		gridData31.widthHint = 200;
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.widthHint = 200;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 3;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 5;
		compositeBody = new Composite(sShell, SWT.NONE);
		// compositeBody.setBounds(new
		// org.eclipse.swt.graphics.Rectangle(348,51,64,64));
		compositeBody.setLayoutData(gridData);
		compositeBody.setLayout(gridLayout1);
		label = new Label(compositeBody, SWT.NONE);
		label.setText("CAS log file location");
		text = new Text(compositeBody, SWT.BORDER);
		text.setLayoutData(gridData21);
		button2 = new Button(compositeBody, SWT.NONE);
		button2.setText("...");
		label1 = new Label(compositeBody, SWT.NONE);
		label1.setText("active log name");
		text1 = new Text(compositeBody, SWT.BORDER);
		text1.setLayoutData(gridData31);
	}
}
