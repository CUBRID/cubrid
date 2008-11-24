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
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Button;

import cubridmanager.CommonTool;
import cubridmanager.Messages;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.widgets.Label;

public class DiagCasRunnerResultDialog extends Dialog {

	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="32,3"
	private Text textResult = null;
	private Button buttonOK = null;
	private int DiagEndCode = Window.CANCEL;
	private Label label = null;
	private Label label1 = null;
	private Label label2 = null;
	private Label label3 = null;

	public DiagCasRunnerResultDialog(Shell parent) {
		super(parent);
		// TODO Auto-generated constructor stub
	}

	public DiagCasRunnerResultDialog(Shell parent, int style) {
		super(parent, style);
		// TODO Auto-generated constructor stub
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 210;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.widthHint = 80;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 5;
		gridData.widthHint = 280;
		gridData.heightHint = 150;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 5;
		sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setText(Messages.getString("TITLE.EXECUTECASRUNNERRESULT"));
		sShell.setLayout(gridLayout);
		textResult = new Text(sShell, SWT.BORDER | SWT.MULTI | SWT.V_SCROLL);
		textResult.setLayoutData(gridData);
		label = new Label(sShell, SWT.NONE);
		label.setLayoutData(gridData2);
		label1 = new Label(sShell, SWT.NONE);
		label2 = new Label(sShell, SWT.NONE);
		label3 = new Label(sShell, SWT.NONE);
		buttonOK = new Button(sShell, SWT.NONE);
		buttonOK.setText(Messages.getString("BUTTON.OK"));
		buttonOK.setLayoutData(gridData1);
		buttonOK.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DiagEndCode = Window.OK;
						sShell.dispose();
					}
				});
		sShell.pack(true);
	}

	public int doModal(String result) {
		createSShell();
		CommonTool.centerShell(sShell);
		sShell.open();
		textResult.setText(result);

		Display display = sShell.getDisplay();

		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return DiagEndCode;
	}
}
