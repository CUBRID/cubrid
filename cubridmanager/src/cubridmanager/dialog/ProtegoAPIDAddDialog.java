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

import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import cubrid.upa.UpaClient;
import cubrid.upa.UpaException;
import cubrid.upa.UpaUserInfo;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.action.ProtegoUserManagementAction;

public class ProtegoAPIDAddDialog extends Dialog {
	private boolean ret = false;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="43,11"
	private Composite compositeBody = null;
	private Composite compositeButton = null;
	private Label labelAPID = null;
	private Label labelNote = null;
	private Text textAPID = null;
	private Text textNote = null;
	private Button buttonSave = null;
	private Button buttonCancel = null;

	public ProtegoAPIDAddDialog(Shell parent) {
		super(parent);
	}

	public ProtegoAPIDAddDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(sShell);
		sShell.setDefaultButton(buttonSave);
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
		sShell = new Shell(getParent(), SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setLayout(new GridLayout());
		createCompositeBody();
		createCompositeButton();
		sShell.pack();
		sShell.setText(Messages.getString("TITLE.APIDREG"));
	}

	/**
	 * This method initializes compositeBody
	 * 
	 */
	private void createCompositeBody() {
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.heightHint = 80;
		gridData4.widthHint = 200;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.heightHint = 15;
		gridData3.widthHint = 200;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		compositeBody = new Composite(sShell, SWT.NONE);
		compositeBody.setLayout(gridLayout);
		labelAPID = new Label(compositeBody, SWT.NONE);
		labelAPID.setText(Messages.getString("TABLE.APID"));
		textAPID = new Text(compositeBody, SWT.BORDER);
		textAPID.setLayoutData(gridData3);
		labelNote = new Label(compositeBody, SWT.NONE);
		labelNote.setText(Messages.getString("TABLE.NOTE"));
		textNote = new Text(compositeBody, SWT.BORDER | SWT.MULTI);
		textNote.setLayoutData(gridData4);
	}

	/**
	 * This method initializes compositeButton
	 * 
	 */
	private void createCompositeButton() {
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.heightHint = 20;
		gridData2.widthHint = 60;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.heightHint = 20;
		gridData1.widthHint = 60;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 2;
		compositeButton = new Composite(sShell, SWT.NONE);
		compositeButton.setLayout(gridLayout1);
		compositeButton.setLayoutData(gridData);
		buttonSave = new Button(compositeButton, SWT.NONE);
		buttonSave.setText(Messages.getString("BUTTON.SAVE"));
		buttonSave.setLayoutData(gridData1);
		buttonSave
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						/* 1. apid check */
						String apId = textAPID.getText().trim();
						if (apId.length() == 0) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.INPUTAPID"));
							return;
						}

						/* 2. note message */
						String noteMessage = textNote.getText();
						if (noteMessage.length() == 0) {
							noteMessage = " ";
						}
						UpaUserInfo usrInfo = new UpaUserInfo(apId, "", "", "",
								"", "", "", noteMessage);

						try {
							UpaClient.admAppCmd(
									ProtegoUserManagementAction.dlg.upaKey,
									UpaClient.UPA_USER_APID_ADD, usrInfo);
						} catch (UpaException ee) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("TEXT.FAILEDADDAPID"));
							return;
						}
						ret = true;
						sShell.dispose();
					}
				});

		buttonCancel = new Button(compositeButton, SWT.NONE);
		buttonCancel.setText(Messages.getString("BUTTON.CANCEL"));
		buttonCancel.setLayoutData(gridData2);
		buttonCancel
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						sShell.dispose();
					}
				});
	}
}
