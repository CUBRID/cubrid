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

package cubridmanager.dialog;

import java.util.ArrayList;

import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import cubridmanager.Application;
import cubridmanager.CommonTool;
import org.eclipse.swt.widgets.Group;

public class ProtegoMTUserAddResultDialog extends Dialog {
	private boolean ret = false;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="39,11"
	private Composite compositeBody = null;
	private Composite compositeButton = null;
	private Group groupResult = null;
	private Text textResult = null;
	private Button buttonOK = null;
	public ArrayList successList = null;
	public ArrayList failedList = null;

	public ProtegoMTUserAddResultDialog(Shell parent) {
		super(parent);
	}

	public ProtegoMTUserAddResultDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(sShell);
		// sShell.setDefaultButton(IDOK);
		sShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		sShell = new Shell(Application.mainwindow.getShell(),
				SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		// sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setLayout(new GridLayout());
		createCompositeBody();
		createCompositeButton();
		sShell.pack();
	}

	/**
	 * This method initializes compositeBody
	 * 
	 */
	private void createCompositeBody() {
		compositeBody = new Composite(sShell, SWT.NONE);
		compositeBody.setLayout(new GridLayout());
		createGroupResult();
	}

	/**
	 * This method initializes compositeButton
	 * 
	 */
	private void createCompositeButton() {
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData11.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData.heightHint = 25;
		gridData.widthHint = 80;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 1;
		compositeButton = new Composite(sShell, SWT.NONE);
		compositeButton.setLayout(gridLayout);
		compositeButton.setLayoutData(gridData11);
		buttonOK = new Button(compositeButton, SWT.NONE);
		buttonOK.setText("OK");
		buttonOK.setLayoutData(gridData);
		buttonOK.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
	}

	/**
	 * This method initializes groupResult
	 * 
	 */
	private void createGroupResult() {
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.heightHint = 250;
		gridData3.widthHint = 300;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		groupResult = new Group(compositeBody, SWT.NONE);
		groupResult.setText("result");
		groupResult.setLayout(new GridLayout());
		textResult = new Text(groupResult, SWT.BORDER | SWT.MULTI);
		textResult.setEnabled(true);
		textResult.setEditable(false);
		textResult.setLayoutData(gridData3);
		setResult();
	}

	private void setResult() {
		String str = new String("Result");
		textResult.setText(str);
	}
}
