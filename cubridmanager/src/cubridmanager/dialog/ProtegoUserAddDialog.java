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

import java.util.Calendar;

import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.FillLayout;
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
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.ProtegoReadCert;
import cubridmanager.action.ProtegoUserManagementAction;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Combo;

public class ProtegoUserAddDialog extends Dialog {
	private boolean ret = false;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="39,60"
	private Composite compositeBody = null;
	private Composite compositeButton = null;
	private Group groupBody = null;
	private Label labelAPID = null;
	private Label labelUserDN = null;
	private Label labelDBName = null;
	private Label labelDBUser = null;
	private Label labelExpDate = null;
	private Combo comboAPID = null;
	private Text textUserDN = null;
	private Text textDBName = null;
	private Text textDBUser = null;
	private Text textExpDate = null;
	private Label labelNote = null;
	private Text textNote = null;
	private Label labelExpDateEx = null;
	private Button buttonSelectCert = null;
	private Button buttonSave = null;
	private Button buttonCancel = null;

	public ProtegoUserAddDialog(Shell parent) {
		super(parent);
	}

	public ProtegoUserAddDialog(Shell parent, int style) {
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
	 * This method initializes sShell1
	 * 
	 */
	private void createSShell() {
		sShell = new Shell(getParent(), SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		// sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM); // for
		// visual-editor
		sShell.setLayout(new GridLayout());
		createCompositeBody();
		createCompositeButton();
		sShell.pack();
		sShell.setText(Messages.getString("MENU.ADDAUTHINFO"));
	}

	/**
	 * This method initializes compositeBody
	 * 
	 */
	private void createCompositeBody() {
		compositeBody = new Composite(sShell, SWT.NONE);
		compositeBody.setLayout(new FillLayout());
		createGroupBody();
	}

	/**
	 * This method initializes compositeButton
	 * 
	 */
	private void createCompositeButton() {
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.heightHint = 22;
		gridData12.widthHint = 80;
		GridData gridData10 = new org.eclipse.swt.layout.GridData();
		gridData10.heightHint = 22;
		gridData10.widthHint = 80;
		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData9.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 2;
		compositeButton = new Composite(sShell, SWT.NONE);
		compositeButton.setLayout(gridLayout1);
		compositeButton.setLayoutData(gridData9);
		buttonSave = new Button(compositeButton, SWT.NONE);
		buttonSave.setText(Messages.getString("BUTTON.SAVE"));
		buttonSave.setLayoutData(gridData12);
		buttonSave.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						// TODO : validate user input
						// 1. apid
						String apid = comboAPID.getText();
						// 2. User DN
						String UserDN = textUserDN.getText().trim();
						if (UserDN.length() == 0) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.INPUTUSERDN"));
							return;
						}
						// 3. dbname
						String dbName = textDBName.getText().trim();
						if (dbName.length() == 0) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.INPUTTARGETDB"));
							return;
						}
						// 4. dbuser
						String dbUser = textDBUser.getText().trim();
						if (dbUser.length() == 0) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.INPUTDBUSER"));
							return;
						}
						// 5. Expire Time
						String expTime = textExpDate.getText().trim();
						if (expTime.length() == 0) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.INPUTEXPTIME"));
							return;
						}
						int spaceIndex = expTime.indexOf(" ");
						if (spaceIndex == -1) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.INVALIDEXPTIME"));
							return;
						}

						String date = expTime.substring(0, spaceIndex);
						String time = expTime.substring(spaceIndex + 1);

						try {
							String[] dateArray = date.split("/");
							String[] timeArray = time.split(":");
							if (dateArray.length == 3) {
								int month, day;
								month = Integer.parseInt(dateArray[1]);
								day = Integer.parseInt(dateArray[2]);
								if ((month < 1) || (month > 12) || (day > 31)
										|| (day < 1)) {
									CommonTool.ErrorBox(sShell, Messages
											.getString("ERROR.INVALIDEXPTIME"));
									return;
								}
							} else {
								CommonTool.ErrorBox(sShell, Messages
										.getString("ERROR.INVALIDEXPTIME"));
								return;
							}

							if (timeArray.length == 3) {
								int hh, mm, ss;
								hh = Integer.parseInt(timeArray[0]);
								mm = Integer.parseInt(timeArray[1]);
								ss = Integer.parseInt(timeArray[2]);
								if ((hh > 24) || (mm > 60) || (ss > 60)) {
									CommonTool.ErrorBox(sShell, Messages
											.getString("ERROR.INVALIDEXPTIME"));
									return;
								}
							} else {
								CommonTool.ErrorBox(sShell, Messages
										.getString("ERROR.INVALIDEXPTIME"));
								return;
							}
						} catch (Exception ee) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.INVALIDEXPTIME"));
							return;
						}
						// 6. note
						String note = textNote.getText().trim();
						if (note.length() == 0) {
							note = " ";
						}

						// 7 make register time
						String regTime = new String();
						Calendar oCalendar = Calendar.getInstance();

						regTime = String.valueOf(oCalendar.get(Calendar.YEAR))
								+ "/"
								+ String.valueOf(oCalendar.get(Calendar.MONTH))
								+ "/"
								+ String.valueOf(oCalendar
										.get(Calendar.DAY_OF_MONTH))
								+ " "
								+ String.valueOf(oCalendar
										.get(Calendar.HOUR_OF_DAY))
								+ ":"
								+ String
										.valueOf(oCalendar.get(Calendar.MINUTE))
								+ ":"
								+ String
										.valueOf(oCalendar.get(Calendar.SECOND));

						UpaUserInfo usrInfo = new UpaUserInfo(apid, UserDN,
								dbName, dbUser, regTime, expTime,
								MainRegistry.UserID, note);
						try {
							UpaClient.admUserAdd(
									ProtegoUserManagementAction.dlg.upaKey,
									usrInfo);
						} catch (UpaException ee) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("TEXT.FAILEDADDUSERAUTH"));
							return;
						}

						ret = true;
						sShell.dispose();
					}
				});
		buttonCancel = new Button(compositeButton, SWT.NONE);
		buttonCancel.setText(Messages.getString("BUTTON.CANCEL"));
		buttonCancel.setLayoutData(gridData10);
		buttonCancel
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						sShell.dispose();
					}
				});
	}

	/**
	 * This method initializes groupBody
	 * 
	 */
	private void createGroupBody() {
		GridData gridData81 = new org.eclipse.swt.layout.GridData();
		gridData81.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData81.heightHint = 22;
		gridData81.widthHint = 100;
		gridData81.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.heightHint = 15;
		gridData7.widthHint = 180;
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.heightHint = 20;
		gridData6.grabExcessHorizontalSpace = true;
		gridData6.widthHint = 130;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.heightHint = 20;
		gridData5.grabExcessHorizontalSpace = true;
		gridData5.widthHint = 130;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.heightHint = 20;
		gridData4.grabExcessHorizontalSpace = true;
		gridData4.widthHint = 130;
		GridData gridData31 = new org.eclipse.swt.layout.GridData(
				GridData.FILL_HORIZONTAL);
		gridData31.heightHint = 20;
		gridData31.grabExcessHorizontalSpace = true;
		gridData31.widthHint = 130;
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData21.heightHint = 20;
		gridData21.widthHint = 130;
		gridData21.grabExcessHorizontalSpace = true;
		gridData21.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.heightHint = 20;
		gridData11.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData11.widthHint = 130;
		gridData11.grabExcessHorizontalSpace = true;
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalSpan = 2;
		gridData3.widthHint = 320;
		gridData3.heightHint = 80;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 2;
		gridData2.widthHint = 180;
		gridData2.heightHint = 15;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		gridData1.widthHint = 180;
		gridData1.heightHint = 15;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.widthHint = 320;
		gridData.grabExcessHorizontalSpace = false;
		gridData.heightHint = 15;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		gridLayout.makeColumnsEqualWidth = false;
		groupBody = new Group(compositeBody, SWT.NONE);
		labelAPID = new Label(groupBody, SWT.NONE);
		labelAPID.setText(Messages.getString("TABLE.APID"));
		labelAPID.setLayoutData(gridData11);
		createComboAPID();
		groupBody.setLayout(gridLayout);
		buttonSelectCert = new Button(groupBody, SWT.NONE);
		buttonSelectCert.setText(Messages.getString("TEXT.SELECTCERT"));
		buttonSelectCert.setLayoutData(gridData81);
		buttonSelectCert
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String[] ret = null;
						ProtegoReadCert reader = new ProtegoReadCert();
						ret = reader.protegoSelectCert();
						if (ret != null) {
							textUserDN.setText(ret[0]);
						}
					}
				});
		labelUserDN = new Label(groupBody, SWT.NONE);
		labelUserDN.setText(Messages.getString("TABLE.USERDN"));
		labelUserDN.setLayoutData(gridData21);
		textUserDN = new Text(groupBody, SWT.BORDER);
		textUserDN.setLayoutData(gridData);
		labelDBName = new Label(groupBody, SWT.NONE);
		labelDBName.setText(Messages.getString("TABLE.DATABASE"));
		labelDBName.setLayoutData(gridData31);
		textDBName = new Text(groupBody, SWT.BORDER);
		textDBName.setLayoutData(gridData1);
		labelDBUser = new Label(groupBody, SWT.NONE);
		labelDBUser.setText(Messages.getString("TABLE.DATABASEUSER"));
		labelDBUser.setLayoutData(gridData4);
		textDBUser = new Text(groupBody, SWT.BORDER);
		textDBUser.setLayoutData(gridData2);
		labelExpDate = new Label(groupBody, SWT.NONE);
		labelExpDate.setText(Messages.getString("TABLE.EXPIRETIME"));
		labelExpDate.setLayoutData(gridData5);
		textExpDate = new Text(groupBody, SWT.BORDER);
		textExpDate.setLayoutData(gridData7);
		labelExpDateEx = new Label(groupBody, SWT.NONE);
		labelNote = new Label(groupBody, SWT.NONE);
		labelNote.setText(Messages.getString("TABLE.NOTE"));
		labelNote.setLayoutData(gridData6);
		textNote = new Text(groupBody, SWT.BORDER | SWT.MULTI);
		textNote.setLayoutData(gridData3);
		labelExpDateEx.setText("ex) 2010/01/31 23:59:59");
	}

	/**
	 * This method initializes comboAPID
	 * 
	 */
	private void createComboAPID() {
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.heightHint = 15;
		gridData8.widthHint = 160;
		comboAPID = new Combo(groupBody, SWT.NONE | SWT.READ_ONLY);
		comboAPID.setLayoutData(gridData8);
		Object[] apList = null;
		apList = PROPAGE_ProtegoAPIDManagementDialog.getAPIDList();
		if (apList != null) {
			for (int i = 0; i < apList.length; i++) {
				comboAPID.add((String) ((String[]) apList[i])[1]);
			}
			comboAPID.select(0);
		}
	}

	/**
	 * This method initializes sShell
	 * 
	 */
}
