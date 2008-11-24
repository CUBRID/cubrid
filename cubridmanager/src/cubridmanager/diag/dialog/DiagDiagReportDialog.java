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
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Composite;

public class DiagDiagReportDialog extends Dialog {
	private int DiagEndCode = Window.CANCEL;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="45,8"
	private Button button6 = null;
	private Button button7 = null;
	private Composite composite = null;
	private Label label1 = null;
	private Label label2 = null;
	private Text text3 = null;
	private Text text4 = null;
	private Label label = null;
	private Text textArea = null;

	/**
	 * This method initializes sShell
	 * 
	 */
	public DiagDiagReportDialog(Shell parent) {
		super(parent);
		// TODO Auto-generated constructor stub
	}

	public DiagDiagReportDialog(Shell parent, int style) {
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

	private void createSShell() {
		sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setText("Analysis report");
		sShell.setSize(new org.eclipse.swt.graphics.Point(404, 496));
		button6 = new Button(sShell, SWT.NONE);
		button6.setBounds(new org.eclipse.swt.graphics.Rectangle(316, 443, 71,
				22));
		button6.setText("Close");
		button6.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						// System.out.println("widgetSelected()"); // TODO
						// Auto-generated Event stub widgetSelected()
						DiagEndCode = Window.OK;
						sShell.dispose();
					}
				});
		button7 = new Button(sShell, SWT.NONE);
		button7.setBounds(new org.eclipse.swt.graphics.Rectangle(232, 443, 71, 22));
		button7.setText("Save");

		createComposite();
	}

	/**
	 * This method initializes composite
	 * 
	 */
	private void createComposite() {
		composite = new Composite(sShell, SWT.NONE);
		composite.setBounds(new org.eclipse.swt.graphics.Rectangle(0, -4, 396,
				438));
		label1 = new Label(composite, SWT.NONE);
		label1.setBounds(new org.eclipse.swt.graphics.Rectangle(8, 234, 105, 12));
		label1.setText("\uc870\uce58 \ub0b4\uc5ed");
		label2 = new Label(composite, SWT.NONE);
		label2.setBounds(new org.eclipse.swt.graphics.Rectangle(10, 333, 111, 12));
		label2.setText("\uc694\uad6c \uc0ac\ud56d");
		text3 = new Text(composite, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL);
		text3.setBounds(new org.eclipse.swt.graphics.Rectangle(8, 250, 379, 64));
		text4 = new Text(composite, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL);
		text4.setBounds(new org.eclipse.swt.graphics.Rectangle(8, 351, 379, 75));
		label = new Label(composite, SWT.NONE);
		label.setBounds(new org.eclipse.swt.graphics.Rectangle(12, 10, 124, 15));
		label.setText("Analysis result");
		textArea = new Text(composite, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL);
		textArea.setBounds(new org.eclipse.swt.graphics.Rectangle(8, 25, 379, 196));
	}
}
