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
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Label;

public class DiagRunDiagDialog extends Dialog {
	private int DiagEndCode = Window.CANCEL;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="9,11"
	private Group group = null;
	private Text text = null;
	private Button button = null;
	private Button button1 = null;
	private Button button3 = null;
	private Text text1 = null;
	private Label label = null;
	private Button button5 = null;

	public DiagRunDiagDialog(Shell parent) {
		super(parent);
		// TODO Auto-generated constructor stub
	}

	public DiagRunDiagDialog(Shell parent, int style) {
		super(parent, style);
		// TODO Auto-generated constructor stub
	}

	public int doModal(String tName) {
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
		sShell.setText("Self test");
		createGroup();
		sShell.setSize(new org.eclipse.swt.graphics.Point(395, 156));
		button5 = new Button(sShell, SWT.NONE);
		button5.setBounds(new org.eclipse.swt.graphics.Rectangle(282, 103, 80,
				19));
		button5.setText("\ub2eb\uae30");
		button5.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
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
		group.setText("\uc2dc\uac04 \uc124\uc815/\ubcf4\uace0\uc11c");
		group.setBounds(new org.eclipse.swt.graphics.Rectangle(6, 7, 374, 82));
		text = new Text(group, SWT.BORDER);
		text.setBounds(new Rectangle(91, 18, 241, 20));
		button = new Button(group, SWT.NONE);
		button.setBounds(new Rectangle(330, 18, 25, 18));
		button.setText("...");
		button1 = new Button(group, SWT.CHECK);
		button1.setBounds(new org.eclipse.swt.graphics.Rectangle(8, 54, 103, 16));
		button1.setText("\uc2dc\uc791 \uc2dc\uac04 \uc124\uc815");
		button3 = new Button(group, SWT.NONE);
		button3.setBounds(new Rectangle(287, 50, 75, 19));
		button3.setText("\uc9c4\ub2e8 \uc2dc\uc791");
		text1 = new Text(group, SWT.BORDER);
		text1.setBounds(new Rectangle(113, 49, 164, 22));
		label = new Label(group, SWT.NONE);
		label.setBounds(new Rectangle(8, 22, 75, 13));
		label.setText("\ubcf4\uace0\uc11c \uc774\ub984");
	}

}
