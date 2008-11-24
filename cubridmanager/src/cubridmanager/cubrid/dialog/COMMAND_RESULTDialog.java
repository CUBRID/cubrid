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

import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.cubrid.action.LoadAction;

import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class COMMAND_RESULTDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Button IDOK = null;
	private Text EDIT_COMMAND_RESULT = null;

	public COMMAND_RESULTDialog(Shell parent) {
		super(parent);
	}

	public COMMAND_RESULTDialog(Shell parent, int style) {
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
		dlgShell.setText(Messages.getString("TITLE.COMMAND_RESULTDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData1.widthHint = 100;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.heightHint = 280;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.widthHint = 460;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(new GridLayout());
		EDIT_COMMAND_RESULT = new Text(sShell, SWT.BORDER | SWT.MULTI
				| SWT.V_SCROLL | SWT.WRAP);
		EDIT_COMMAND_RESULT.setEditable(false);
		EDIT_COMMAND_RESULT.setLayoutData(gridData);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData1);
		IDOK
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
		EDIT_COMMAND_RESULT.setText(LoadAction.resultMsg.toString());
	}

}
