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

package cubridmanager.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;

import cubrid.upa.UpaClient;
import cubrid.upa.UpaException;
import cubrid.upa.UpaKey;
import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.ProtegoReadCert;
import cubridmanager.dialog.PROPAGE_ProtegoAPIDManagementDialog;
import cubridmanager.dialog.PROPAGE_ProtegoMTUserManagementDialog;
import cubridmanager.dialog.PROPAGE_ProtegoUserManagementDialog;
import cubridmanager.dialog.UPAPortDialog;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;

public class ProtegoUserManagementAction extends Action {
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="10,51"
	private TabFolder tabFolder = null;
	private Composite composite1 = null;
	private Composite composite2 = null;
	private Composite composite3 = null;
	private Button buttonOK = null;
	private Button buttonChangeManager = null;
	private Composite compositeButton = null;
	private Composite compositeTab = null;
	final PROPAGE_ProtegoUserManagementDialog part1 = new PROPAGE_ProtegoUserManagementDialog(
			sShell);
	final PROPAGE_ProtegoMTUserManagementDialog part2 = null;// new PROPAGE_ProtegoMTUserManagementDialog(sShell);
	final PROPAGE_ProtegoAPIDManagementDialog part3 = new PROPAGE_ProtegoAPIDManagementDialog(
			sShell);

	public static ProtegoUserManagementAction dlg = null;
	public UpaKey upaKey = null;
	public UpaKey upaMtKey = null;
	public boolean isLoggedIn = false;
	public ProtegoUserManagementAction(String text, String image) {
		super(text);

		setId("ProtegoUserManagementAction");
		setActionDefinitionId("ProtegoUserManagementAction");
		if (image != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(image));
		setToolTipText(text);
		dlg = this;
	}

	public boolean protegoLogin() {
		Shell shell = new Shell();

		if (MainRegistry.upaPort == 0) {
			UPAPortDialog dlg = new UPAPortDialog(shell);
			if (dlg.doModal() == false)
				return false;
		}

		UpaClient.setIpPort(MainRegistry.HostAddr, MainRegistry.upaPort);
		try {
			upaKey = UpaClient.loginSM(MainRegistry.UserSignedData);
		} catch (UpaException e1) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), Messages
					.getString("ERROR.ADMAUTH"));
			CommonTool.ErrorBox(Application.mainwindow.getShell(), String
					.valueOf(e1.getErrorCode()));
			return false;
		}

		isLoggedIn = true;
		MainRegistry.HostToken = upaKey.getKey();
		return true;
	}

	public void run() {
		if (protegoLogin() == false)
			return;
		runpage();
	}

	public void runpage() {
		sShell = new Shell(Application.mainwindow.getShell(),
				SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		// sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM); /* for
		// visual-editor */
		sShell.setText(Messages.getString("TITLE.PROTEGOUSERMANAGEMENT"));
		sShell.setLayout(new GridLayout());

		createCompositeTab();

		createCompositeButton();

		sShell.pack();
		CommonTool.centerShell(sShell);
		sShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
	}

	/**
	 * This method initializes compositeButton
	 * 
	 */
	private void createCompositeButton() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.heightHint = 25;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.widthHint = 100;
		compositeButton = new Composite(sShell, SWT.NONE);
		compositeButton.setLayoutData(gridData1);
		compositeButton.setLayout(gridLayout);
		buttonChangeManager = new Button(compositeButton, SWT.NONE);
		buttonChangeManager.setText(Messages
				.getString("BUTTON.CHANGEPROTEGOMANAGER"));
		buttonChangeManager
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String[] ret = null;
						ProtegoReadCert reader = new ProtegoReadCert();
						ret = reader.protegoSelectCert();
						if (ret != null) {
							try {
								UpaClient.admChangeSM(dlg.upaKey, ret[0]);
								CommonTool
										.MsgBox(
												sShell,
												SWT.OK,
												Messages
														.getString("BUTTON.CHANGEPROTEGOMANAGER"),
												Messages
														.getString("TEXT.ADMCHANGED"));
								UpaClient.logout(dlg.upaKey);
							} catch (UpaException ee) {
								CommonTool.ErrorBox(sShell, Messages
										.getString("TEXT.CHANGEADMFAILED"));
								return;
							}

							if (protegoLogin()) {
								ProtegoUserManagementAction.refresh();
							}
						}
					}
				});
		buttonChangeManager.setLayoutData(gridData);
		buttonOK = new Button(compositeButton, SWT.NONE);
		buttonOK.setText(Messages.getString("BUTTON.OK"));
		buttonOK.setLayoutData(gridData);
		buttonOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
	}

	/**
	 * This method initializes compositeTab
	 * 
	 */
	private void createCompositeTab() {
		compositeTab = new Composite(sShell, SWT.NONE);
		compositeTab.setLayout(new GridLayout());
		tabFolder = new TabFolder(compositeTab, SWT.NONE);

		composite1 = part1.SetTabPart(tabFolder);
		// composite2 = part2.SetTabPart(tabFolder);
		composite3 = part3.SetTabPart(tabFolder);

		TabItem tabItem1 = new TabItem(tabFolder, SWT.NONE);
		tabItem1.setControl(composite1);
		tabItem1.setText(Messages.getString("TITLE.PROTEGOAUTHMANAGEMENT"));

		/*
		 * remove auth transfer.
		 * TabItem tabItem2 = new TabItem(tabFolder, SWT.NONE);
		 * tabItem2.setControl(composite2); tabItem2.setText("Auth transfer management");
		 */

		TabItem tabItem3 = new TabItem(tabFolder, SWT.NONE);
		tabItem3.setControl(composite3);
		tabItem3.setText(Messages.getString("TITLE.PROTEGOAPIDMANAGEMENT"));
	}

	public static void refresh() {
		if (dlg == null)
			return;

		switch (dlg.tabFolder.getSelectionIndex()) {
		case 0:
			PROPAGE_ProtegoUserManagementDialog.refresh();
			break;
		case 1:
			/*
			 * remove auth transfer. 
			 * PROPAGE_ProtegoMTUserManagementDialog.refresh();
			 */
			break;
		case 2:
			PROPAGE_ProtegoAPIDManagementDialog.refresh();
			break;
		default:
			break;
		}
	}
}
