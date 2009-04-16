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

package cubridmanager.query.dialog;

import java.io.UnsupportedEncodingException;

import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.WorkView;
import cubridmanager.Messages;
import cubridmanager.cas.CASItem;
import cubridmanager.query.view.QueryEditor;
import org.eclipse.ui.IWorkbenchPart;
import org.eclipse.ui.IViewReference;

import org.eclipse.swt.graphics.FontData;
import org.eclipse.swt.graphics.RGB;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.FontDialog;
import org.eclipse.swt.widgets.Spinner;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;

public class QueryEditorOptionDialog extends Dialog {

	private Shell sShell = null;
	private Composite cmpInitValueArea = null;
	private Composite cmpActionValueArea = null;
	private Composite cmpConnValueArea = null;
	private Composite cmpBtnArea = null;
	private Button chkAutoCommit = null;
	private Button chkGetQueryPlan = null;
	private Button chkRecordLimit = null;
	private Spinner spnRecordLimit = null;
	private Button chkGetOidInfo = null;
	private Label lblCasPort = null;
	private Combo cmbCasPort = null;
	private Button chkCharSet = null;
	private Text txtCharSet = null;
	private Button btnConfirm = null;
	private Button btnCancel = null;
	private Group groupFont = null;
	private Text textFont = null;
	private Button buttonSetFont = null;
	private Label labelFont = null;
	private String fontString = "";
	private int fontColorRed = 0;
	private int fontColorBlue = 0;
	private int fontColorGreen = 0;
	private Label labelFontSize = null;
	private Button buttonDefaultFont = null;
	private Text textFontSize = null;
	private Spinner spnPageLimit = null;

	public QueryEditorOptionDialog(Shell parent) {
		super(parent);
	}

	public QueryEditorOptionDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		sShell.pack();
		CommonTool.centerShell(sShell);
		sShell.open();
		getQueryEditorOptionInMainRegistry();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		// sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell = new Shell(getParent(), SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setText(Messages.getString("TITLE.QUERYEDITOROPTION"));
		sShell.setLayout(new GridLayout());
		createCmpInitValueArea();
		createCmpActionValueArea();
		createCmpConnValueArea();
		createGroupFont();
		createCmpBtnArea();
	}

	/**
	 * This method initializes grpInitValues
	 * 
	 */
	private void createCmpInitValueArea() {
		GridData gridData = new GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		cmpInitValueArea = new Group(sShell, SWT.NONE);
		cmpInitValueArea.setLayout(new GridLayout());
		cmpInitValueArea.setLayoutData(gridData);

		chkAutoCommit = new Button(cmpInitValueArea, SWT.CHECK);
		chkAutoCommit.setText(Messages.getString("CHECK.AUTOCOMMIT"));
		chkAutoCommit.setToolTipText(Messages.getString("TOOLTIP.INIT"));
	}

	/**
	 * This method initializes grpActnValues
	 * 
	 */
	private void createCmpActionValueArea() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		GridData gridData1 = new GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		cmpActionValueArea = new Group(sShell, SWT.NONE);
		cmpActionValueArea.setLayoutData(gridData1);
		cmpActionValueArea.setLayout(gridLayout);

		chkRecordLimit = new Button(cmpActionValueArea, SWT.CHECK);
		chkRecordLimit.setText(Messages.getString("CHECK.RECORDLIMIT"));
		chkRecordLimit.setToolTipText(Messages
				.getString("TOOLTIP.RECORDLIMIT1"));
		chkRecordLimit
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						spnRecordLimit
								.setEnabled(chkRecordLimit.getSelection());
						spnRecordLimit.setSelection(5000);
					}
				});

		GridData gridData7 = new GridData();
		gridData7.widthHint = 100;
		spnRecordLimit = new Spinner(cmpActionValueArea, SWT.BORDER);
		spnRecordLimit.setMaximum(Integer.MAX_VALUE);
		spnRecordLimit.setLayoutData(gridData7);
		spnRecordLimit.setToolTipText(Messages
				.getString("TOOLTIP.RECORDLIMIT2"));

		Label pageLimit = new Label(cmpActionValueArea, SWT.NONE);
		pageLimit.setText(Messages.getString("LABEL.PAGELIMIT"));

		GridData gridData8 = new GridData();
		gridData8.widthHint = 100;
		spnPageLimit = new Spinner(cmpActionValueArea, SWT.BORDER);
		spnPageLimit.setMaximum(Integer.MAX_VALUE);
		spnPageLimit.setLayoutData(gridData8);
		spnPageLimit.setToolTipText(Messages.getString("TOOLTIP.PAGELIMIT"));
		GridData gridData = new GridData();
		gridData.horizontalSpan = 2;
		chkGetQueryPlan = new Button(cmpActionValueArea, SWT.CHECK);
		chkGetQueryPlan.setText(Messages.getString("TOOLTIP.QUERYPLANENABLE"));
		chkGetQueryPlan.setLayoutData(gridData);

		GridData gridData4 = new GridData();
		gridData4.horizontalSpan = 2;
		chkGetOidInfo = new Button(cmpActionValueArea, SWT.CHECK);
		chkGetOidInfo.setLayoutData(gridData4);
		chkGetOidInfo.setText(Messages.getString("CHECK.GETOIDINFO"));
		chkGetOidInfo.setToolTipText(Messages.getString("TOOLTIP.GETOIDINFO"));
	}

	/**
	 * This method initializes grpConnValues
	 * 
	 */
	private void createCmpConnValueArea() {
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 2;
		GridData gridData2 = new GridData();
		cmpConnValueArea = new Group(sShell, SWT.NONE);
		cmpConnValueArea.setLayout(gridLayout1);
		cmpConnValueArea.setLayoutData(gridData2);

		lblCasPort = new Label(cmpConnValueArea, SWT.NONE);
		lblCasPort.setText(Messages.getString("LABEL.CASPORT"));

		createCmbCasPort();

		chkCharSet = new Button(cmpConnValueArea, SWT.CHECK);
		chkCharSet.setText(Messages.getString("CHECK.CHARSET"));
		chkCharSet
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						txtCharSet.setEnabled(chkCharSet.getSelection());
					}
				});

		GridData gridData8 = new GridData();
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		txtCharSet = new Text(cmpConnValueArea, SWT.BORDER);
		txtCharSet.setLayoutData(gridData8);
		txtCharSet.setToolTipText(Messages.getString("TOOLTIP.CHARSET"));
	}

	/**
	 * This method initializes cmpBtnArea
	 * 
	 */
	private void createCmpBtnArea() {
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 3;
		GridData gridData3 = new GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.grabExcessHorizontalSpace = true;
		cmpBtnArea = new Composite(sShell, SWT.NONE);
		cmpBtnArea.setLayoutData(gridData3);
		cmpBtnArea.setLayout(gridLayout2);

		GridData gridData5 = new GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData5.grabExcessHorizontalSpace = true;
		gridData5.widthHint = 75;
		btnConfirm = new Button(cmpBtnArea, SWT.NONE);
		btnConfirm.setLayoutData(gridData5);
		btnConfirm.setText(Messages.getString("BUTTON.OK"));
		btnConfirm.setToolTipText(Messages
				.getString("TOOLTIP.QUERYEDITOROPTION"));
		btnConfirm
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (!checkValid())
							return;

						if (MainRegistry.UserID.equals("admin")) {
							if (cmbCasPort.getText() != null
									&& !cmbCasPort.getText().trim().equals("")) {
								String port = cmbCasPort.getText().substring(
										cmbCasPort.getText().indexOf('(') + 1,
										cmbCasPort.getText().length() - 1);
								MainRegistry.UserPort.put("admin", port);
							} else {
								MainRegistry.UserPort.put("admin", "30000");
							}
						}
						
						MainRegistry
								.setQueryEditorOption(
										CommonTool.BooleanYesNo(chkAutoCommit
												.getSelection()),
										CommonTool.BooleanYesNo(chkGetQueryPlan
												.getSelection()),
										(chkRecordLimit.getSelection() ? Integer
												.toString(spnRecordLimit
														.getSelection())
												: "0"), Integer
												.toString(spnPageLimit
														.getSelection()),
										CommonTool
												.BooleanYesNo(chkGetOidInfo
														.getSelection()),
														(String)MainRegistry.UserPort.get(MainRegistry.UserID),
										(chkCharSet.getSelection() ? txtCharSet
												.getText() : ""), fontString,
										String.valueOf(fontColorRed), String
												.valueOf(fontColorGreen),
										String.valueOf(fontColorBlue));

						IViewReference IRF[] = WorkView.workwindow
								.getActivePage().getViewReferences();
						for (int i = 0; i < IRF.length; i++) {
							IWorkbenchPart currentView = IRF[i].getPart(false);
							if (currentView instanceof QueryEditor) {
								QueryEditor qe = (QueryEditor) currentView;
								qe.itemQueryPlan.setEnabled(chkGetQueryPlan
										.getSelection());
							}
						}
						MainRegistry.saveMainRegistryToProperty();

						sShell.dispose();
					}
				});

		GridData gridData6 = new GridData();
		gridData6.widthHint = 75;
		btnCancel = new Button(cmpBtnArea, SWT.NONE);
		btnCancel.setLayoutData(gridData6);
		btnCancel.setText(Messages.getString("BUTTON.CANCEL"));
		btnCancel
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
	}

	/**
	 * This method initializes cmbCasPort
	 * 
	 */
	private void createCmbCasPort() {
		GridData gridData9 = new GridData();
		gridData9.widthHint = 150;
		cmbCasPort = new Combo(cmpConnValueArea, SWT.READ_ONLY);
		cmbCasPort.setLayoutData(gridData9);
		cmbCasPort.setToolTipText(Messages.getString("TOOLTIP.CASPORT"));
	}

	private void getQueryEditorOptionInMainRegistry() {
		chkAutoCommit.setSelection(MainRegistry.queryEditorOption.autocommit);
		chkGetQueryPlan
				.setSelection(MainRegistry.queryEditorOption.getqueryplan);
		spnRecordLimit
        .setSelection(MainRegistry.queryEditorOption.recordlimit > 0 ? MainRegistry.queryEditorOption.recordlimit
                : 5000);
		spnPageLimit.setSelection(MainRegistry.queryEditorOption.pagelimit);
		chkRecordLimit
				.setSelection(MainRegistry.queryEditorOption.recordlimit > 0);
		spnRecordLimit.setEnabled(chkRecordLimit.getSelection());
		chkGetOidInfo.setSelection(MainRegistry.queryEditorOption.oidinfo);

		CASItem casItem = null;
		int selectionItem = 0;
		int default_port = 30000 ;
		int admin_port = 30000 ;

		if (MainRegistry.getDefaultBroker(MainRegistry.UserID) != null ) {
			default_port = MainRegistry.getDefaultBroker(MainRegistry.UserID).broker_port ;
		} 

		if (MainRegistry.UserID.equals("admin")) {
			String old_port = (String)MainRegistry.UserPort.get(MainRegistry.UserID);
			if (old_port != null && !old_port.equals("")) {
				CASItem checkCasItem = MainRegistry.CASinfoFind(Integer.parseInt(old_port));
				if (checkCasItem == null) {
					CASItem ci = MainRegistry.getDefaultBroker(MainRegistry.UserID);					
					if(ci != null)
						admin_port = ci.broker_port;
				}
			} else {
				CASItem ci = MainRegistry.getDefaultBroker(MainRegistry.UserID);					
				if(ci != null)
					admin_port = ci.broker_port;				
			}
		}

		int selectNum = 0 ;
		boolean checkPort = false ;
		for (int i = 0; i < MainRegistry.CASinfo.size(); i++) {
			casItem = (CASItem) MainRegistry.CASinfo.get(i);
			if ( casItem.broker_port > 0 ) {
				cmbCasPort.add(casItem.broker_name
						+ "(" + Integer.toString(casItem.broker_port) + ")");
				selectNum++ ;
			}
			if(MainRegistry.UserPort.get(MainRegistry.UserID)==null||MainRegistry.UserPort.get(MainRegistry.UserID).toString().trim().equals(""))
			{
				if(casItem.broker_port == default_port) {
					selectionItem = ( selectNum - 1 );
					checkPort = true ;
				}
			}
			else if (casItem.broker_port == new Integer((String)MainRegistry.UserPort.get(MainRegistry.UserID))) {
				selectionItem = ( selectNum - 1 );
				checkPort = true ;
			} else {
				if (MainRegistry.UserID.equals("admin")) {
					if ( casItem.broker_port == admin_port ) 
						selectionItem = ( selectNum - 1 );
					checkPort = true ;
				}
			}
		}

		if (checkPort == true) {
			cmbCasPort.select(selectionItem);
		}	
		
		if(MainRegistry.UserID.equals("admin"))
		{
			cmbCasPort.setEnabled(true);
		}
		else
		{
			cmbCasPort.setEnabled(false);
		}		
		txtCharSet.setText(MainRegistry.queryEditorOption.charset);
		chkCharSet.setSelection(txtCharSet.getText().length() > 0);
		txtCharSet.setEnabled(chkCharSet.getSelection());
	}

	private boolean checkValid() {
		if (spnRecordLimit.getSelection() < 1) {
			CommonTool.ErrorBox(sShell, Messages
					.getString("MESSAGE.INVALIDRECORDLIMIT"));
			spnRecordLimit.setFocus();
			return false;
		}
		if (spnPageLimit.getSelection() < 1) {
			CommonTool.ErrorBox(sShell, Messages
					.getString("MESSAGE.INVALIDRECORDLIMIT"));
			spnPageLimit.setFocus();
			return false;
		}
		if (cmbCasPort.getText().indexOf("(stopped)") > 0) {
			CommonTool.ErrorBox(sShell, Messages
					.getString("MESSAGE.INVALIDCASPORT"));
			cmbCasPort.setFocus();
			return false;
		}
		if (chkCharSet.getSelection() && txtCharSet.getText().length() > 0) {
			try {
				byte[] b = { 0 };
				new String(b, txtCharSet.getText());
			} catch (UnsupportedEncodingException e) {
				CommonTool.ErrorBox(sShell, Messages
						.getString("MESSAGE.ILLEGALCHARSET")
						+ e.getLocalizedMessage());
				return false;
			}
		}
		return true;
	}

	private String getPort(String cmbItem) {
		String port = cmbCasPort.getText();
		port = port.substring(port.indexOf("(") + 1, port.indexOf(")"));
		return Integer.toString(CommonTool.atoi(port));
	}

	/**
	 * This method initializes groupFont
	 * 
	 */
	private void createGroupFont() {
		GridData gridData15 = new org.eclipse.swt.layout.GridData();
		gridData15.widthHint = 30;
		GridData gridData14 = new org.eclipse.swt.layout.GridData();
		gridData14.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData14.widthHint = 130;
		gridData14.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData13 = new org.eclipse.swt.layout.GridData();
		gridData13.grabExcessHorizontalSpace = true;
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData12.grabExcessHorizontalSpace = true;
		gridData12.widthHint = 80;
		gridData12.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData11.widthHint = 130;
		gridData11.grabExcessHorizontalSpace = true;
		gridData11.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData10 = new org.eclipse.swt.layout.GridData();
		gridData10.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData10.grabExcessHorizontalSpace = true;
		gridData10.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout3 = new GridLayout();
		gridLayout3.numColumns = 3;
		groupFont = new Group(sShell, SWT.NONE);
		groupFont.setLayout(gridLayout3);
		groupFont.setLayoutData(gridData10);
		labelFont = new Label(groupFont, SWT.NONE);
		labelFont.setText(Messages.getString("LABEL.FONT"));
		labelFont.setLayoutData(gridData13);
		textFont = new Text(groupFont, SWT.BORDER);
		textFont.setEditable(false);
		textFont.setLayoutData(gridData12);
		buttonSetFont = new Button(groupFont, SWT.NONE);
		buttonSetFont.setText(Messages.getString("BUTTON.CHANGE"));
		buttonSetFont.setLayoutData(gridData11);
		labelFontSize = new Label(groupFont, SWT.NONE);
		labelFontSize.setText(Messages.getString("TABLE.SIZE"));
		textFontSize = new Text(groupFont, SWT.BORDER);
		textFontSize.setEditable(false);
		textFontSize.setLayoutData(gridData15);
		buttonDefaultFont = new Button(groupFont, SWT.NONE);
		buttonDefaultFont.setText(Messages.getString("BUTTON.SETDEFAULTFONT"));
		buttonDefaultFont.setLayoutData(gridData14);
		buttonDefaultFont
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						fontString = "";
						fontColorRed = 0;
						fontColorGreen = 0;
						fontColorBlue = 0;
						textFont.setText("");
						textFontSize.setText("");
					}
				});
		buttonSetFont
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						FontDialog dlg = new FontDialog(Application.mainwindow
								.getShell());
						FontData fontdata = null;
						if (fontString != null && !fontString.equals("")) {
							fontdata = new FontData(fontString);
							FontData fontList[] = new FontData[1];
							fontList[0] = fontdata;
							dlg.setRGB(new RGB(fontColorRed, fontColorGreen,
									fontColorBlue));
							dlg.setFontList(fontList);
						}

						fontdata = dlg.open();

						if (fontdata != null) {
							fontString = fontdata.toString();
							textFont.setText(fontdata.getName());
							textFontSize.setText(String.valueOf(fontdata
									.getHeight()));
							fontColorRed = dlg.getRGB().red;
							fontColorBlue = dlg.getRGB().blue;
							fontColorGreen = dlg.getRGB().green;
						}
					}
				});
		fontString = MainRegistry.queryEditorOption.fontString;
		fontColorRed = MainRegistry.queryEditorOption.fontColorRed;
		fontColorBlue = MainRegistry.queryEditorOption.fontColorBlue;
		fontColorGreen = MainRegistry.queryEditorOption.fontColorGreen;
		if (fontString != null && !fontString.equals("")) {
			FontData fontData = new FontData(fontString);
			if (fontData != null) {
				textFont.setText(fontData.getName());
				textFontSize.setText(String.valueOf(fontData.getHeight()));
			}
		}
	}
}