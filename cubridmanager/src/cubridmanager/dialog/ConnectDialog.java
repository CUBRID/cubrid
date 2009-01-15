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

import java.net.InetAddress;
import java.util.Properties;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import cubridmanager.ApplicationWorkbenchWindowAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.CubridException;
import cubridmanager.HostmonSocket;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.OnlyQueryEditor;
import cubridmanager.TextFocusAdapter;
import cubridmanager.VerifyDigitListener;
import cubridmanager.WaitingMsgBox;
import cubridmanager.cas.CASItem;
import cubridmanager.diag.DiagSiteDiagData;
import cubridmanager.ProtegoReadCert;

public class ConnectDialog extends Dialog {
	public static String Cmd_site = null;
	public static String Cmd_pass = null;
	String Desc = null;
	String Addr = null;
	String Port = null;
	String ID = null;
	String Pass = null;
	private Properties prop = new Properties();
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="9,13"
	private Composite cmpTableArea = null;
	private Table table = null;
	private Button NEWSITE = null;
	private Button SITESAVE = null;
	private Button SITEDELETE = null;
	private Button CONNECT = null;
	private Button CANCEL = null;
	private CLabel cLabel = null;
	private CLabel cLabel1 = null;
	private CLabel cLabel2 = null;
	private CLabel cLabel3 = null;
	private CLabel cLabel4 = null;
	private Text textDesc = null;
	private Text textAddr = null;
	private Text textPort = null;
	private Text textID = null;
	private Text textPassword = null;
	private Composite group = null;
	private Composite cmpLeftBtnArea = null;
	private Composite cmpRightBtnArea = null;
	private Composite cmtTxtArea = null;

	public ConnectDialog(Shell parent) {
		super(parent);
	}

	public ConnectDialog(Shell parent, int style) {
		super(parent, style);
		// TODO Auto-generated constructor stub
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(sShell);

		String stringLastSelIndex = prop
				.getProperty(MainConstants.propLastSelectionItem);

		int lastSelection = CommonTool.atoi(stringLastSelIndex);
		table.setSelection(lastSelection);
		setTextArea();

		if (Cmd_site != null) { 
			TableItem row;
			ID = null;
			for (int i = 0, n = table.getItemCount(); i < n; i++) {
				row = table.getItem(i);
				Desc = row.getText(0);
				if (Desc.equals(Cmd_site)) {
					Addr = row.getText(1);
					Port = row.getText(2);
					ID = row.getText(3);
					Pass = Cmd_pass;
					break;
				}
			}
			Cmd_site = null;
			Cmd_pass = null;
			if (ID == null) {
				CommonTool.ErrorBox(Messages.getString("ERROR.NOCONNECTSITE"));
			} else {
				ConnectProcess();
			}
			sShell.dispose();
			return 0;
		}

		sShell.setDefaultButton(CONNECT);
		sShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		gridLayout.horizontalSpacing = 10;
		gridLayout.marginHeight = 5;
		gridLayout.marginWidth = 10;
		gridLayout.verticalSpacing = 0;
		sShell = new Shell(super.getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		sShell.setText(Messages.getString("TITLE.SITE_MANAGER")); 
		sShell.setMaximized(false);
		sShell.setLayout(gridLayout);

		createCmpTableArea();
		createCmpTextArea();

		sShell.pack();
		setTextArea();
	}

	private void createCmpTableArea() {
		GridLayout layoutTableArea = new GridLayout();
		layoutTableArea.horizontalSpacing = 0;
		layoutTableArea.marginWidth = 0;
		layoutTableArea.verticalSpacing = 0;
		layoutTableArea.marginHeight = 0;
		GridData gridTableArea = new GridData(GridData.FILL_BOTH);
		cmpTableArea = new Composite(sShell, SWT.NONE);
		cmpTableArea.setLayout(layoutTableArea);
		cmpTableArea.setLayoutData(gridTableArea);

		createTable();
		createCmpLeftBtnArea();
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.widthHint = 120;
		gridData.heightHint = 150;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		table = new Table(cmpTableArea, SWT.BORDER | SWT.FULL_SELECTION);
		table.setHeaderVisible(true);
		table.setLayoutData(gridData);
		table.setLinesVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(100, 100, false));
		tlayout.addColumnData(new ColumnWeightData(0, 100, false));
		tlayout.addColumnData(new ColumnWeightData(0, 100, false));
		tlayout.addColumnData(new ColumnWeightData(0, 100, false));
		table.setLayout(tlayout);

		TableColumn descColumn = new TableColumn(table, SWT.LEFT);
		descColumn.setResizable(false);
		descColumn.setText(Messages.getString("TABLE.SITENAME")); 
		TableColumn addrColumn = new TableColumn(table, SWT.LEFT);
		addrColumn.setText(Messages.getString("TABLE.ADDR")); 
		TableColumn portColumn = new TableColumn(table, SWT.LEFT);
		portColumn.setText(Messages.getString("TABLE.PORT")); 
		TableColumn idColumn = new TableColumn(table, SWT.LEFT);
		idColumn.setText(Messages.getString("TABLE.USERID")); 

		if (!CommonTool.LoadProperties(prop)) {
			CommonTool.SetDefaultParameter();
			CommonTool.LoadProperties(prop);
		}
		int hostcnt;
		String paracnt = prop.getProperty(MainConstants.SYSPARA_HOSTCNT);
		if (paracnt == null)
			hostcnt = 0;
		else
			hostcnt = Integer.parseInt(paracnt);

		for (int i = 0; i < hostcnt; i++) {
			String para = prop.getProperty(MainConstants.SYSPARA_HOSTBASE + i);
			if (para == null)
				continue;
			TableItem item = new TableItem(table, SWT.NONE);
			item.setText(0, para);
			para = prop.getProperty(MainConstants.SYSPARA_HOSTBASE + i
					+ MainConstants.SYSPARA_HOSTADDR);
			if (para != null) {
				item.setText(1, para);
			}
			para = prop.getProperty(MainConstants.SYSPARA_HOSTBASE + i
					+ MainConstants.SYSPARA_HOSTPORT);
			if (para != null) {
				item.setText(2, para);
			}
			para = prop.getProperty(MainConstants.SYSPARA_HOSTBASE + i
					+ MainConstants.SYSPARA_HOSTID);
			if (para != null) {
				if (!MainRegistry.isCertLogin())
					item.setText(3, para);
			}
		}
		for (int i = 0, n = table.getColumnCount(); i < n; i++) {
			table.getColumn(i).pack();
		}

		table
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setTextArea();
					}
				});
		table.addMouseListener(new org.eclipse.swt.events.MouseAdapter() {
			public void mouseDoubleClick(org.eclipse.swt.events.MouseEvent e) {
				Point pt = new Point(e.x, e.y);
				TableItem item = table.getItem(pt);
				if (item != null) {
					goconnect();
				}
			}
		});
	}

	/**
	 * This method initializes cmpLeftBtnArea
	 * 
	 */
	private void createCmpLeftBtnArea() {
		GridData gridData1 = new GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData1.grabExcessVerticalSpace = true;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 3;
		gridLayout1.marginWidth = 0;
		gridLayout1.marginHeight = 5;
		gridLayout1.horizontalSpacing = 5;
		gridLayout1.verticalSpacing = 5;
		cmpLeftBtnArea = new Composite(cmpTableArea, SWT.NONE);
		cmpLeftBtnArea.setLayout(gridLayout1);
		cmpLeftBtnArea.setLayoutData(gridData1);

		NEWSITE = new Button(cmpLeftBtnArea, SWT.NONE);
		NEWSITE.setText(Messages.getString("BUTTON.NEWSITE")); 
		NEWSITE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ClearFields();
					}
				});
		NEWSITE.setLayoutData(new GridData(GridData.GRAB_HORIZONTAL
				| GridData.GRAB_VERTICAL | GridData.VERTICAL_ALIGN_END));

		SITESAVE = new Button(cmpLeftBtnArea, SWT.NONE);
		SITESAVE.setText(Messages.getString("BUTTON.SAVE")); 
		SITESAVE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						SaveHost();
					}
				});
		SITESAVE.setLayoutData(new GridData(GridData.GRAB_HORIZONTAL));

		SITEDELETE = new Button(cmpLeftBtnArea, SWT.NONE);
		SITEDELETE.setText(Messages.getString("BUTTON.DELETE")); 
		SITEDELETE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DeleteHost();
					}
				});
		SITEDELETE.setLayoutData(new GridData(GridData.GRAB_HORIZONTAL));
	}

	private void createCmpTextArea() {
		group = new Composite(sShell, SWT.NONE);
		GridLayout grpgrid = new GridLayout();
		grpgrid.horizontalSpacing = 0;
		grpgrid.marginHeight = 0;
		grpgrid.marginWidth = 0;
		grpgrid.verticalSpacing = 0;
		group.setLayout(grpgrid);
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		group.setLayoutData(gridData);

		createGrpTxtArea();
		createCmpRightBtnArea();
	}

	/**
	 * This method initializes grpTxtArea
	 * 
	 */
	private void createGrpTxtArea() {
		GridLayout gridLayout3 = new GridLayout();
		gridLayout3.numColumns = 2;
		gridLayout3.marginWidth = 5;
		GridData gridData4 = new GridData();
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData4.grabExcessVerticalSpace = true;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		cmtTxtArea = new Composite(group, SWT.BORDER | SWT.FLAT);
		cmtTxtArea.setLayoutData(gridData4);
		cmtTxtArea.setLayout(gridLayout3);

		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 100;
		gridData2.grabExcessVerticalSpace = true;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		cLabel = new CLabel(cmtTxtArea, SWT.NONE);
		cLabel.setText(Messages.getString("LBL.SITENAME")); 
		textDesc = new Text(cmtTxtArea, SWT.BORDER);
		textDesc.setTextLimit(255);
		textDesc.addFocusListener(new TextFocusAdapter());
		textDesc.setLayoutData(gridData2);

		cLabel1 = new CLabel(cmtTxtArea, SWT.NONE);
		cLabel1.setText(Messages.getString("LBL.ADDR")); 
		textAddr = new Text(cmtTxtArea, SWT.BORDER);
		textAddr.setTextLimit(255);
		textAddr.addFocusListener(new TextFocusAdapter());
		textAddr.setLayoutData(gridData2);

		cLabel2 = new CLabel(cmtTxtArea, SWT.NONE);
		cLabel2.setText(Messages.getString("LBL.PORT")); 
		textPort = new Text(cmtTxtArea, SWT.BORDER);
		textPort.setTextLimit(5);
		textPort.addListener(SWT.Verify, new VerifyDigitListener());
		textPort.addFocusListener(new TextFocusAdapter());
		textPort.setLayoutData(gridData2);

		cLabel3 = new CLabel(cmtTxtArea, SWT.NONE);
		cLabel3.setText(Messages.getString("LBL.USERID")); 
		textID = new Text(cmtTxtArea, SWT.BORDER);
		textID.setTextLimit(255);
		textID.addFocusListener(new TextFocusAdapter());
		textID.setLayoutData(gridData2);

		cLabel4 = new CLabel(cmtTxtArea, SWT.NONE);
		cLabel4.setText(Messages.getString("LBL.USERPASSWORD")); 
		textPassword = new Text(cmtTxtArea, SWT.BORDER | SWT.PASSWORD);
		textPassword.setTextLimit(255);
		textPassword.addFocusListener(new TextFocusAdapter());
		textPassword.setLayoutData(gridData2);

		if (MainRegistry.isCertLogin()) {
			if (MainRegistry.isCertificateLogin) {
				/* disable id, password box */
				textID.setEnabled(false);
				textPassword.setEnabled(false);
			}
		}
	}

	private void createCmpRightBtnArea() {
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 2;
		gridLayout2.horizontalSpacing = 5;
		gridLayout2.marginHeight = 5;
		gridLayout2.marginWidth = 0;
		gridLayout2.verticalSpacing = 5;
		cmpRightBtnArea = new Composite(group, SWT.NONE);
		cmpRightBtnArea.setLayout(gridLayout2);
		GridData gridData3 = new GridData();
		gridData3.widthHint = 100;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData3.grabExcessHorizontalSpace = true;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		cmpRightBtnArea.setLayoutData(gridData3);

		GridData gridData5 = new GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData5.grabExcessHorizontalSpace = true;
		CONNECT = new Button(cmpRightBtnArea, SWT.NONE);
		CONNECT.setText(Messages.getString("BUTTON.CONNECT")); 
		CONNECT.setLayoutData(gridData5);
		CONNECT
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						goconnect();
						if (MainRegistry.IsConnected) {
							CASItem casitem = MainRegistry
									.CASinfo_find("query_editor");
							if (casitem != null)
								MainRegistry.queryEditorOption.casport = casitem.broker_port;
						}
					}
				});

		GridData gridData6 = new GridData();
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		CANCEL = new Button(cmpRightBtnArea, SWT.NONE);
		CANCEL.setText(Messages.getString("BUTTON.CANCEL")); 
		CANCEL.setLayoutData(gridData6);
		CANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
	}

	private void setTextArea() {
		int selIndex = table.getSelectionIndex();
		if (selIndex < 0)
			return;

		TableItem row = table.getItem(selIndex);
		textDesc.setText(row.getText(0));
		textAddr.setText(row.getText(1));
		textPort.setText(row.getText(2));
		if (!MainRegistry.isCertLogin()) {
			textID.setText(row.getText(3));
			textPassword.setText("");
		}
	}

	private void SetSysparaAll() {
		TableItem row;
		String Desc, Addr, Port, ID;
		int selIndex = table.getSelectionIndex();
		if (selIndex >= 0)
			prop.setProperty(MainConstants.propLastSelectionItem, Integer
					.toString(selIndex));
		else
			prop.setProperty(MainConstants.propLastSelectionItem, "0");

		prop.setProperty(MainConstants.SYSPARA_HOSTCNT, Integer.toString(table
				.getItemCount()));
		for (int i = 0, n = table.getItemCount(); i < n; i++) {
			row = table.getItem(i);
			Desc = row.getText(0);
			Addr = row.getText(1);
			Port = row.getText(2);
			ID = row.getText(3);
			if (Desc.length() < 1)
				Desc = "";
			if (Addr.length() < 1)
				Addr = "";
			if (Port.length() < 1)
				Port = "";
			if (ID.length() < 1)
				ID = "";
			prop.setProperty(MainConstants.SYSPARA_HOSTBASE + i, Desc);
			prop.setProperty(MainConstants.SYSPARA_HOSTBASE + i
					+ MainConstants.SYSPARA_HOSTADDR, Addr);
			prop.setProperty(MainConstants.SYSPARA_HOSTBASE + i
					+ MainConstants.SYSPARA_HOSTPORT, Port);
			prop.setProperty(MainConstants.SYSPARA_HOSTBASE + i
					+ MainConstants.SYSPARA_HOSTID, ID);
		}

		if (MainRegistry.isProtegoBuild()) {
			if (MainRegistry.isCertificateLogin) {
				prop.setProperty(MainConstants.protegoLoginType,
						MainConstants.protegoLoginTypeCert);
			} else {
				prop.setProperty(MainConstants.protegoLoginType,
						MainConstants.protegoLoginTypeMtId);
			}
		}
		CommonTool.SaveProperties(prop);
	}

	private boolean SaveHost() {
		String Desc = textDesc.getText();
		String Addr = textAddr.getText();
		String Port = textPort.getText();
		String ID = textID.getText();

		if (!CheckFields(Desc, Addr, Port, ID))
			return false;
		Desc.trim();
		Addr.trim();
		Port.trim();
		ID.trim();
		// double item check
		for (int i = 0, n = table.getItemCount(); i < n; i++) {
			if (table.getItem(i).getText(0).equals(Desc)) {
				TableItem row = table.getItem(i);
				row.setText(0, Desc);
				row.setText(1, Addr);
				row.setText(2, Port);
				row.setText(3, ID);
				SetSysparaAll();
				return true;
			}
		}
		// add to table
		TableItem item = new TableItem(table, SWT.NONE);
		item.setText(0, Desc);
		item.setText(1, Addr);
		item.setText(2, Port);
		item.setText(3, ID);

		SetSysparaAll();
		return true;
	}

	private void DeleteHost() {
		String Desc = textDesc.getText();

		if (Desc == null || Desc.trim().length() < 1) {
			CommonTool.MsgBox(sShell, SWT.ICON_WARNING | SWT.YES, Messages
					.getString("MSG.ERROR"), 
					Messages.getString("MSG.SELECTSITE")); 
			return;
		}
		Desc.trim();
		// item search
		for (int i = 0, n = table.getItemCount(); i < n; i++) {
			if (table.getItem(i).getText(0).equals(Desc)) {
				table.remove(i);
				ClearFields();
				SetSysparaAll();
				return;
			}
		}
		CommonTool.MsgBox(sShell, SWT.ICON_WARNING | SWT.YES, Messages
				.getString("MSG.ERROR"), 
				Messages.getString("MSG.SITENOTFOUND")); 
	}

	private void ClearFields() {
		textDesc.setText(""); 
		textAddr.setText(""); 
		textPort.setText("8001"); 
		textID.setText("admin"); 
		textPassword.setText(""); 
		textDesc.setFocus();
	}

	private boolean CheckFields(String Desc, String Addr, String Port, String ID) {
		if (Desc == null || Desc.trim().length() < 1) {
			CommonTool.MsgBox(sShell, SWT.ICON_WARNING | SWT.YES, Messages
					.getString("MSG.ERROR"), 
					Messages.getString("MSG.INPUT_SITENAME")); 
			textDesc.setFocus();
			return false;
		}
		if (Addr == null || Addr.trim().length() < 1) {
			CommonTool.MsgBox(sShell, SWT.ICON_WARNING | SWT.YES, Messages
					.getString("MSG.ERROR"), 
					Messages.getString("MSG.INPUT_ADDR")); 
			textAddr.setFocus();
			return false;
		}
		if (Port == null || Port.trim().length() < 1) {
			CommonTool.MsgBox(sShell, SWT.ICON_WARNING | SWT.YES, Messages
					.getString("MSG.ERROR"), 
					Messages.getString("MSG.INPUT_PORT")); 
			textPort.setFocus();
			return false;
		} else {
			int portnum = 0;
			try {
				portnum = Integer.parseInt(Port);
			} catch (NumberFormatException e) {
				portnum = 0;
			}
			if (portnum < 1 || portnum > 65535) {
				CommonTool.MsgBox(sShell, SWT.ICON_WARNING | SWT.YES, Messages
						.getString("MSG.ERROR"), 
						Messages.getString("MSG.INVALID_PORT")); 
				textPort.setFocus();
				return false;
			}
		}
		if (MainRegistry.isCertLogin())
			return true;

		if (ID == null || ID.trim().length() < 1) {
			CommonTool.MsgBox(sShell, SWT.ICON_WARNING | SWT.YES, Messages
					.getString("MSG.ERROR"), 
					Messages.getString("MSG.INPUT_ID")); 
			textID.setFocus();
			return false;
		}
		return true;
	}

	private boolean ConnectProcess() {
		String msg = new String();
		MainRegistry.HostAddr = new String(Addr);
		MainRegistry.HostDesc = new String(Desc);
		MainRegistry.HostPort = Integer.parseInt(Port);
		MainRegistry.UserID = new String(ID);

		MainRegistry.soc = new HostmonSocket();
		String version = MainConstants.MANAGER_VERSION;
		if (OnlyQueryEditor.connectOldServer)
			version = "3.0.0";

		msg = "id:" + MainRegistry.UserID + "\n" + "password:";
		if (MainRegistry.isProtegoBuild()) {
			msg += MainRegistry.UserSignedData + "\n";
		} else {
			msg += Pass + "\n";
		}

		msg += "clientver:" + version + "\n\n";
		if (MainRegistry.soc.ConnectHost(MainRegistry.HostAddr,
				MainRegistry.HostPort, msg)) {
			WaitingMsgBox dlg = new WaitingMsgBox(sShell);
			dlg.run(Messages.getString("MSG.CONNECTTING"), 20);
			if (dlg.is_timeout) {
				MainRegistry.soc.stoploop();
			}
			if (HostmonSocket.errmsg != null)
				CommonTool.ErrorBox(sShell, HostmonSocket.errmsg);
		} else {
			CommonTool.ErrorBox(sShell, CubridException
					.getErrorMessage(CubridException.ER_CONNECT));
		}
		if(MainRegistry.IsConnected && MainRegistry.UserID.equals("admin") && MainRegistry.UserPassword.equals("admin"))	
		{
			ChangePasswordDialog dialog = new ChangePasswordDialog(sShell,"admin");
			String password = dialog.doModal();
			if(!password.equals("admin"))
			{
				ClientSocket cs = new ClientSocket();
				String setPasswdMsg = new String();
				setPasswdMsg = "targetid:" + "admin" + "\n";
				setPasswdMsg += "newpassword:" + password.toString();
				if (!cs.SendBackGround(sShell, setPasswdMsg, "setdbmtpasswd",
						Messages.getString("MENU.USERADMIN"))) {
					return false;
				}
			}
			else
			{
				MainRegistry.IsConnected = false;
				for (int i = 0; i < MainRegistry.diagSiteDiagDataList.size(); i++) {
					DiagSiteDiagData diagSiteData = (DiagSiteDiagData) MainRegistry.diagSiteDiagDataList
							.get(i);
					if (diagSiteData.site_name.equals(MainRegistry.HostDesc))
						MainRegistry.diagSiteDiagDataList.remove(i);
				}

				// Environment initial
				if (MainRegistry.soc != null)
					MainRegistry.soc.stoploop();
				MainRegistry.IsConnected = false;
				MainRegistry.HostAddr = null;
				MainRegistry.HostPort = 0;
				MainRegistry.UserID = null;
				MainRegistry.HostJSPort = 0;
				MainRegistry.DiagAuth = MainConstants.AUTH_NONE;
				MainRegistry.CASAuth = MainConstants.AUTH_NONE;
				MainRegistry.Authinfo.clear();
				MainRegistry.IsSecurityManager = false;
				MainRegistry.NaviDraw_CUBRID = false;
				MainRegistry.NaviDraw_CAS = false;
				MainRegistry.NaviDraw_DIAG = false;
				MainRegistry.IsDBAAuth = false;
				MainRegistry.IsCASStart = false;
				MainRegistry.IsCASinfoReady = false;
				MainRegistry.CASinfo.clear();
			}

		}
		if (!MainRegistry.IsConnected)
			return false;
		ApplicationWorkbenchWindowAdvisor.myconfigurer.setTitle(Messages
				.getString("TITLE.CUBRIDMANAGER")
				+ " - " + Desc);

		MainRegistry.NaviDraw_CUBRID = false;
		MainRegistry.NaviDraw_CAS = false;
		MainRegistry.NaviDraw_DIAG = false;

		MainRegistry.DiagAuth = MainConstants.AUTH_DBA;
		MainRegistry.IsSecurityManager = false;

		// send getdiaginfo message
		DiagSiteDiagData diagSiteData = new DiagSiteDiagData();
		diagSiteData.site_name = MainRegistry.HostDesc;
		MainRegistry.diagSiteDiagDataList.add(diagSiteData);
		MainRegistry.SetCurrentSiteName(diagSiteData.site_name);

		// Query Editor option setting
		MainRegistry.setQueryEditorOption(prop
				.getProperty(MainConstants.queryEditorOptionAucoCommit), prop
				.getProperty(MainConstants.queryEditorOptionGetQueryPlan), prop
				.getProperty(MainConstants.queryEditorOptionRecordLimit), prop
				.getProperty(MainConstants.queryEditorOptionPageLimit),prop
				.getProperty(MainConstants.queryEditorOptionGetOidInfo), prop
				.getProperty(MainConstants.queryEditorOptionCasPort), prop
				.getProperty(MainConstants.queryEditorOptionCharSet), prop
				.getProperty(MainConstants.queryEditorOptionFontString), prop
				.getProperty(MainConstants.queryEditorOptionFontColorRed), prop
				.getProperty(MainConstants.queryEditorOptionFontColorGreen),
				prop.getProperty(MainConstants.queryEditorOptionFontColorBlue));
		/*
		 * ClientSocket cs = new ClientSocket(); Shell psh = sShell; if
		 * (!cs.SendClientMessage(psh, "", "getdiaginfo")) {
		 * CommonTool.ErrorBox(psh,
		 * Messages.getString("ERROR.ERRORSERVERDIAGINFO") + "(" + cs.ErrorMsg +
		 * ")"); }
		 */
		return true;
	}

	private void goconnect() {
		// connect,login,environment set process
		Desc = textDesc.getText().trim();
		Addr = textAddr.getText().trim();
		Port = textPort.getText().trim();

		if (MainRegistry.isCertLogin()) {
			try {
				String[] ret = null;

				ProtegoReadCert reader = new ProtegoReadCert();
				ret = reader.protegoSelectCert();
				if (ret == null) {
					return;
				}

				ID = ret[0]; /* User DN string */
				Pass = ret[1]; /* Signed Data */

				MainRegistry.UserSignedData = new String(Pass);
			} catch (Exception e) {
				System.out.println(e.getMessage());
			}
		} else {
			ID = textID.getText().trim();
			if (MainRegistry.isMTLogin()) {
				Pass = textPassword.getText().trim();
				MainRegistry.UserSignedData = new String(mt_id_passwd_make(ID,
						textPassword.getText().trim()));
			} else {
				Pass = textPassword.getText().trim();
			}
		}

		if (!SaveHost())
			return;

		if (MainRegistry.IsConnected == true) {
			// connected session check
			if (isAleadyConnected(Addr, Port, ID)) {
				CommonTool.ErrorBox(sShell, Messages
						.getString("MSG.ALREADYCONNECTED"));
				return;
			}
			try {
				Runtime.getRuntime()
						.exec(
								"cubridmanager -nosplash CMDS \"" + Desc
										+ "\" " + Pass);
			} catch (Exception ex) {
				CommonTool.debugPrint(ex);
			}
			sShell.dispose();

			return;
		}

		if (ConnectProcess())
			sShell.dispose();
	}

	private boolean isAleadyConnected(String Addr, String Port, String ID) {
		if ((MainRegistry.HostPort != Integer.parseInt(Port))
				|| (!MainRegistry.UserID.equals(ID)))
			return false;

		String currentHostAddr = MainRegistry.HostAddr;
		if (Addr.equals(currentHostAddr))
			return true;

		// 1. Addr is IP, currentHostAddr is hostname
		try {
			InetAddress addr = InetAddress.getByName(currentHostAddr);
			if (addr.getHostAddress().equals(Addr))
				return true;
		} catch (Exception e) {
		}

		// 2. Addr is hostname, currentHostAddr is IP
		try {
			InetAddress addr = InetAddress.getByName(Addr);
			if (addr.getHostAddress().equals(currentHostAddr))
				return true;
		} catch (Exception e) {
		}

		return false;
	}

	private String mt_id_passwd_make(String id, String passwd) {
		String out_buf;
		out_buf = "1" + id + ":" + passwd;
		return out_buf;
	}
}