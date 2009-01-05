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

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CCombo;
import org.eclipse.swt.custom.TableEditor;
import org.eclipse.swt.events.FocusAdapter;
import org.eclipse.swt.events.FocusEvent;
import org.eclipse.swt.events.MouseAdapter;
import org.eclipse.swt.events.MouseEvent;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.TraverseEvent;
import org.eclipse.swt.events.TraverseListener;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.CubridManagerUserInfo;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cas.CASItem;
import cubridmanager.cubrid.DBUserInfo;

import org.eclipse.swt.layout.GridData;

public class UserManagementDialog extends Dialog {

	private Shell sShell = null;
	private Table tblUsers = null;
	private Composite cmpBtnArea = null;
	private Button btnAdd = null;
	private Button btnDelete = null;
	private Button btnConfirm = null;
	private Button btnCancel = null;
	private Button btnSave = null;
	private ArrayList listUserTable = new ArrayList();
	private int beforeSelectionIndex = -1;
	private int currentSelectionIndex = -1;
	private boolean btnSaveEnable = false;

	public UserManagementDialog(Shell parent) {
		super(parent);
	}

	public UserManagementDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(sShell);
		sShell.open();

		saveCurrentUserTable();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		sShell = new Shell(getParent(), SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setText(Messages.getString("TITLE.USERADMIN"));
		sShell.setLayout(new GridLayout());
		createTable();
		createBtnArea();
		sShell.pack();
	}

	private void createTable() {
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.widthHint = 350;
		gridData.heightHint = 150;
		tblUsers = new Table(sShell, SWT.FULL_SELECTION | SWT.SINGLE
				| SWT.BORDER);
		tblUsers.setLayoutData(gridData);
		tblUsers.setLinesVisible(true);
		tblUsers.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(30, 30, true));
		tlayout.addColumnData(new ColumnWeightData(30, 30, true));
		tlayout.addColumnData(new ColumnWeightData(20, 30, true));
		tlayout.addColumnData(new ColumnWeightData(20, 30, true));
		tlayout.addColumnData(new ColumnWeightData(20, 30, true));
		tblUsers.setLayout(tlayout);

		TableColumn tblCol = new TableColumn(tblUsers, SWT.LEFT);
		tblCol.setText(Messages.getString("LBL.USERID"));
		tblCol = new TableColumn(tblUsers, SWT.LEFT | SWT.PASSWORD);
		tblCol.setText(Messages.getString("LBL.USERPASSWORD"));
		tblCol = new TableColumn(tblUsers, SWT.LEFT);
		tblCol.setText(Messages.getString("TABLE.DBAAUTH"));
		tblCol = new TableColumn(tblUsers, SWT.LEFT);
		tblCol.setText(Messages.getString("TABLE.CASAUTH"));
		tblCol = new TableColumn(tblUsers, SWT.LEFT);
		tblCol.setText(Messages.getString("TABLE.BROKERPORT"));

		final TableEditor editor = new TableEditor(tblUsers);
		editor.horizontalAlignment = SWT.LEFT;
		editor.grabHorizontal = true;

		tblUsers.addMouseListener(new MouseAdapter() {
			public void mouseDown(MouseEvent event) {
				Control old = editor.getEditor();
				if (old != null)
					old.dispose();

				Point pt = new Point(event.x, event.y);

				final TableItem item = tblUsers.getItem(pt);

				beforeSelectionIndex = currentSelectionIndex;
				currentSelectionIndex = tblUsers.getSelectionIndex();
				if (beforeSelectionIndex < 0
						|| beforeSelectionIndex != currentSelectionIndex)
					return;

				if (item != null) {
					int column = -1;
					for (int i = 0, n = tblUsers.getColumnCount(); i < n; i++) {
						Rectangle rect = item.getBounds(i);
						if (rect.contains(pt)) {
							column = i;
							break;
						}
					}

					// id field. admin cannot changed
					if (column == 0) {
						if (item.getText(0).equals("admin"))
							return;

						final Text text = new Text(tblUsers, SWT.NONE);
						text.setForeground(item.getForeground());

						text.setText((String) item.getText(column));
						text.setForeground(item.getForeground());
						text.selectAll();
						text.setFocus();

						editor.minimumWidth = text.getBounds().width;

						editor.setEditor(text, item, column);

						text.addTraverseListener(new TraverseListener() {
							public void keyTraversed(TraverseEvent e) {
								switch (e.detail) {
								case SWT.TRAVERSE_ESCAPE:
									e.doit = false;
									text.dispose();
									break;
								case SWT.TRAVERSE_ARROW_NEXT:
								case SWT.TRAVERSE_ARROW_PREVIOUS:
								case SWT.TRAVERSE_PAGE_NEXT:
								case SWT.TRAVERSE_PAGE_PREVIOUS:
								case SWT.TRAVERSE_TAB_NEXT:
								case SWT.TRAVERSE_TAB_PREVIOUS:
								case SWT.TRAVERSE_RETURN:
								default:
									String preValue = (String) item.getText(0);
									item.setText(0, text.getText());
									if (!preValue.equals(item.getText(0)))
										btnSave.setEnabled(true);
									text.dispose();
									break;
								}
							}
						});

						text.addFocusListener(new FocusAdapter() {
							public void focusLost(FocusEvent e) {
								String preValue = (String) item.getText(0);
								item.setText(0, text.getText());
								if (!preValue.equals(item.getText(0)))
									btnSave.setEnabled(true);
								text.dispose();
							}
						});
					}
					// password field
					else if (column == 1) {
						String prePasswd = (String) item.getData("passwd");
						UserManagementPasswordConfirmDialog dlg = new UserManagementPasswordConfirmDialog(
								sShell, prePasswd);
						String newPasswd = dlg.doModal();
						if (newPasswd.length() > 0) {
							item.setData("passwd", newPasswd);
							item.setText(1, "********");
							btnSave.setEnabled(true);
						}
					}
					// db creat auth field, cas auth field
					else if (column == 2 || column == 3) {
						if (column == 2 && item.getText(0).equals("admin"))
							return;

						final CCombo combo = new CCombo(tblUsers, SWT.READ_ONLY);
						combo.add("admin");
						if (column == 3)
							combo.add("monitor");
						combo.add("none");

						combo.select(combo.indexOf(item.getText(column)));

						editor.minimumWidth = combo.computeSize(SWT.DEFAULT,
								SWT.DEFAULT).x;
						tblUsers.getColumn(column)
								.setWidth(editor.minimumWidth);

						combo.setFocus();
						editor.setEditor(combo, item, column);

						final int col = column;
						combo.addSelectionListener(new SelectionAdapter() {
							public void widgetSelected(SelectionEvent event) {
								String preValue = item.getText(col);
								item.setText(col, combo.getText());
								if (!preValue.equals(item.getText(col)))
									btnSave.setEnabled(true);

								combo.dispose();
							}
						});
					}
					
					else if (column == 4) {

						final CCombo combo = new CCombo(tblUsers, SWT.READ_ONLY);
//						Hashtable userPort = MainRegistry.UserPort;
						for(int i=0;i<MainRegistry.CASinfo.size();i++)
						{
							CASItem casItem = (CASItem)MainRegistry.CASinfo.get(i);
							combo.add(new Integer(casItem.broker_port).toString());
						}
//						Enumeration enu = userPort.keys();
//						while(enu.hasMoreElements())
//						{
//							String user=(String)enu.nextElement();
//							combo.add((String)userPort.get(user));
//						}
						combo.select(combo.indexOf(item.getText(column)));

						editor.minimumWidth = combo.computeSize(SWT.DEFAULT,
								SWT.DEFAULT).x;
						tblUsers.getColumn(column)
								.setWidth(editor.minimumWidth);

						combo.setFocus();
						editor.setEditor(combo, item, column);

						final int col = column;
						combo.addSelectionListener(new SelectionAdapter() {
							public void widgetSelected(SelectionEvent event) {
								String preValue = item.getText(col);
								item.setText(col, combo.getText());
								if (!preValue.equals(item.getText(col)))
									btnSave.setEnabled(true);

								combo.dispose();
							}
						});
					}
				}
			}
		});
		tblUsers.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (!tblUsers.getSelection()[0].getText(0).equals(
								"admin"))
							btnDelete.setEnabled(true);
						else
							btnDelete.setEnabled(false);
					}
				});
		createItems();

	}

	private void createBtnArea() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 6;
		gridLayout.makeColumnsEqualWidth = false;
		GridData gridData1 = new GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.grabExcessHorizontalSpace = true;
		cmpBtnArea = new Composite(sShell, SWT.NONE);
		cmpBtnArea.setLayoutData(gridData1);
		cmpBtnArea.setLayout(gridLayout);

		GridData gridData2 = new GridData();
		gridData2.widthHint = 60;
		btnAdd = new Button(cmpBtnArea, SWT.NONE);
		btnAdd.setLayoutData(gridData2);
		btnAdd.setText(Messages.getString("BUTTON.ADD"));

		btnAdd.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						final TableItem item = new TableItem(tblUsers, SWT.NONE);
						item.setData("passwd", new String(""));
						item.setText(2, "none");
						item.setText(3, "none");
						item.setData("dbUserInfo", null);

						TableEditor editor = new TableEditor(tblUsers);
						editor.horizontalAlignment = SWT.LEFT;
						editor.grabHorizontal = true;

						final Text text = new Text(tblUsers, SWT.NONE);
						text.setForeground(item.getForeground());

						text.setText(Messages.getString("LBL.USERID"));
						text.setForeground(item.getForeground());
						text.selectAll();
						text.setFocus();

						editor.minimumWidth = text.getBounds().width;

						editor.setEditor(text, item, 0);

						text.addTraverseListener(new TraverseListener() {
							public void keyTraversed(TraverseEvent e) {
								switch (e.detail) {
								case SWT.TRAVERSE_ESCAPE:
									e.doit = false;
									text.dispose();
									item.dispose();
									break;
								case SWT.TRAVERSE_ARROW_NEXT:
								case SWT.TRAVERSE_ARROW_PREVIOUS:
								case SWT.TRAVERSE_PAGE_NEXT:
								case SWT.TRAVERSE_PAGE_PREVIOUS:
								case SWT.TRAVERSE_TAB_NEXT:
								case SWT.TRAVERSE_TAB_PREVIOUS:
								case SWT.TRAVERSE_RETURN:
								default:
									String preValue = (String) item.getText(0);
									item.setText(0, text.getText());
									if (!preValue.equals(item.getText(0)))
										btnSave.setEnabled(true);
									text.dispose();
									btnSave.setEnabled(true);
									break;
								}
							}
						});

						text.addFocusListener(new FocusAdapter() {
							public void focusLost(FocusEvent e) {
								String preValue = (String) item.getText(0);
								item.setText(0, text.getText());
								if (!preValue.equals(item.getText(0)))
									btnSave.setEnabled(true);
								text.dispose();
								btnSave.setEnabled(true);
							}
						});
					}
				});
		GridData gridData3 = new GridData();
		gridData3.widthHint = 60;
		gridData3.grabExcessHorizontalSpace = false;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		btnDelete = new Button(cmpBtnArea, SWT.NONE);
		btnDelete.setLayoutData(gridData3);
		btnDelete.setText(Messages.getString("BUTTON.DELETE"));
		btnDelete.setEnabled(false);
		btnDelete.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CommonTool.WarnYesNo(sShell, Messages
								.getString("WARNYESNO.DELETE")) == SWT.NO)
							return;
						tblUsers.remove(tblUsers.getSelectionIndex());
						btnSave.setEnabled(true);
					}
				});

		GridData dummyGridData = new GridData();
		dummyGridData.grabExcessHorizontalSpace = true;
		Label dummy = new Label(cmpBtnArea, SWT.NONE);
		dummy.setLayoutData(dummyGridData);

		GridData gridData4 = new GridData();
		gridData4.widthHint = 60;
		gridData4.grabExcessHorizontalSpace = false;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		btnConfirm = new Button(cmpBtnArea, SWT.NONE);
		btnConfirm.setLayoutData(gridData2);
		btnConfirm.setText(Messages.getString("BUTTON.OK"));
		btnConfirm.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (!checkAdaptation())
							return;
						if (hasChanged()) {
							if (!sendMessage())
								CommonTool.ErrorBox(sShell, Messages
										.getString("ERROR.FAILCMUSERMODIFY"));
						}
						sShell.dispose();
					}
				});

		GridData gridData5 = new GridData();
		gridData5.widthHint = 60;
		btnCancel = new Button(cmpBtnArea, SWT.NONE);
		btnCancel.setLayoutData(gridData3);
		btnCancel.setText(Messages.getString("BUTTON.CANCEL"));
		btnCancel.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});

		GridData gridData6 = new GridData();
		gridData6.widthHint = 60;
		btnSave = new Button(cmpBtnArea, SWT.NONE);
		btnSave.setLayoutData(gridData4);
		btnSave.setText(Messages.getString("BUTTON.APPLY"));
		btnSave.setEnabled(btnSaveEnable);
		btnSave.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (!checkAdaptation())
							return;
						if (hasChanged()) {
							if (!sendMessage())
								CommonTool.ErrorBox(sShell, Messages
										.getString("ERROR.FAILCMUSERMODIFY"));
							saveCurrentUserTable();
						}
						btnSave.setEnabled(false);
					}
				});
	}

	private void createItems() {
		String cmUser = MainRegistry.UserID;
		String cmPassword = MainRegistry.UserPassword;
		String dbaAuth = (cmUser.equals("admin") ? cmUser
				: dbaAuthToString(MainRegistry.IsDBAAuth));
		String casAuth = casAuthToString(MainRegistry.CASAuth);
		TableItem item = new TableItem(tblUsers, SWT.NONE);
		item.setText(0, cmUser);
		item.setText(1, "********");
		item.setData("passwd", cmPassword);
		item.setText(2, dbaAuth);
		item.setText(3, casAuth);
		String port = "30000";
		if(MainRegistry.UserPort.get(cmUser)==null||MainRegistry.UserPort.get(cmUser).toString().trim().equals(""))
		{
			btnSaveEnable = true;
			CASItem casItem = (CASItem) MainRegistry.CASinfo.get(0);
			if(casItem != null)
				port = new Integer(casItem.broker_port).toString();
		}	
		else
		{
			port = MainRegistry.UserPort.get(cmUser).toString();
			btnSaveEnable = false;
		}
			

		item.setText(4,port);
		item.setData("dbUserInfo", MainRegistry.listDBUserInfo);

		for (int i = 0; i < MainRegistry.listOtherCMUserInfo.size(); i++) {
			CubridManagerUserInfo cmui = (CubridManagerUserInfo) MainRegistry.listOtherCMUserInfo
					.get(i);
			cmUser = cmui.cmUser;
			cmPassword = cmui.cmPassword;
			casAuth = casAuthToString(cmui.CASAuth);
			dbaAuth = dbaAuthToString(cmui.IsDBAAuth);

			item = new TableItem(tblUsers, SWT.NONE);
			item.setText(0, cmUser);
			item.setText(1, "********");
			item.setData("passwd", cmPassword);
			item.setText(2, dbaAuth);
			item.setText(3, casAuth);
			String port1 = "30000";
			if(MainRegistry.UserPort.get(cmUser)==null||MainRegistry.UserPort.get(cmUser).toString().trim().equals(""))
			{
				btnSaveEnable = true;
				CASItem casItem = (CASItem) MainRegistry.CASinfo.get(0);
				if(casItem != null)
					port1 = new Integer(casItem.broker_port).toString();
			}	
			else
			{
				port1 = MainRegistry.UserPort.get(cmUser).toString();
				btnSaveEnable = false;
			}
			item.setText(4,port1);
			item.setData("dbUserInfo", cmui.listDBUserInfo.clone());
		}
	}

	private String casAuthToString(byte casAuth) {
		switch (casAuth) {
		case MainConstants.AUTH_DBA:
			return new String("admin");
		case MainConstants.AUTH_NONDBA:
			return new String("monitor");
		default:
			return new String("none");
		}
	}

	private String dbaAuthToString(boolean isDBA) {
		return isDBA ? new String("admin") : new String("none");
	}

	private void saveCurrentUserTable() {
		if (listUserTable != null)
			listUserTable.clear();
		TableItem item = null;
		for (int i = 0; i < tblUsers.getItemCount(); i++) {
			item = tblUsers.getItem(i);

			listUserTable.add(new StructUserTable(item.getText(0), item
					.getData("passwd").toString(), item.getText(2), item
					.getText(3),item.getText(4)));
		}
	}

	private boolean checkAdaptation() {
		TableItem item1, item2 = null;
		for (int i = 0; i < tblUsers.getItemCount(); i++) {
			item1 = tblUsers.getItem(i);
			if (item1.getText(0).length() == 0
					|| item1.getData("passwd").toString().length() == 0) {
				CommonTool.WarnBox(sShell, Messages
						.getString("WARNING.EMPTYSTRING1")
						+ (i + 1) + Messages.getString("WARNING.EMPTYSTRING2"));
				return false;
			}
		}

		for (int i = 0; i < tblUsers.getItemCount(); i++) {
			item1 = tblUsers.getItem(i);
			for (int j = i + 1; j < tblUsers.getItemCount(); j++) {
				item2 = tblUsers.getItem(j);
				if (item1.getText(0).equals(item2.getText(0))) {
					CommonTool.WarnBox(sShell, Messages
							.getString("WARNING.DUPLICATION1")
							+ item2.getText(0)
							+ Messages.getString("WARNING.DUPLICATION2"));
					return false;
				}
			}
		}

		return true;
	}

	private boolean hasChanged() {
		if (tblUsers.getItemCount() != listUserTable.size())
			return true;

		TableItem currItem = null;
		StructUserTable beforeItem = null;
		for (int i = 0; i < listUserTable.size(); i++) {
			currItem = tblUsers.getItem(i);
			beforeItem = (StructUserTable) listUserTable.get(i);
			if (!currItem.getText(0).equals(beforeItem.id))
				return true;
			if (!((String) currItem.getData("passwd")).equals(beforeItem.pw))
				return true;
			if (!currItem.getText(2).equals(beforeItem.dba))
				return true;
			if (!currItem.getText(3).equals(beforeItem.cas))
				return true;
			if (!currItem.getText(4).equals(beforeItem.port))
				return true;
		}
		return false;
	}

	/**
	 * Delete all user in Manager Server and add from tblUsers's information
	 */
	private boolean sendMessage() {
		ClientSocket cs = new ClientSocket();
		String msg = "";

		// delete entire user
		for (int i = 0; i < MainRegistry.listOtherCMUserInfo.size(); i++) {
			msg = "targetid:"
					+ ((CubridManagerUserInfo) (MainRegistry.listOtherCMUserInfo
							.get(i))).cmUser + "\n";
			if (!cs.SendBackGround(sShell, msg, "deletedbmtuser", Messages
					.getString("MENU.USERADMIN")))
				return false;
		}

		// add user from tblUsers information
		TableItem item = null;
		String cmd = new String();
		for (int i = 0; i < tblUsers.getItemCount(); i++) {
			item = tblUsers.getItem(i);
			msg = "targetid:" + item.getText(0) + "\n";
			if (!item.getText(0).equals("admin")) {
				cmd = "adddbmtuser";
				msg += "password:" + item.getData("passwd") + "\n";
				if (item.getData("dbUserInfo") != null) {
					DBUserInfo ui = null;
					for (int j = 0; j < ((ArrayList) item.getData("dbUserInfo"))
							.size(); j++) {
						ui = (DBUserInfo) ((ArrayList) item
								.getData("dbUserInfo")).get(j);
						msg += "open:dbauth\n";
						msg += "dbname:" + ui.dbname + "\n";
						msg += "dbid:" + ui.dbuser + "\n";
						msg += "dbpassword:" + ui.dbpassword + "\n";
						msg += "close:dbauth\n";
					}
				}
			} else
				cmd = "updatedbmtuser";
			String port = "30000";
			if(item.getText(4)==null&&item.getText(4).trim().equals(""))
			{
				CASItem casItem = (CASItem) MainRegistry.CASinfo.get(0);
				if(casItem != null)
					port = new Integer(casItem.broker_port).toString();
			}
			else
			{
				port = item.getText(4);
			}
			msg += "casauth:" + item.getText(3) +","+port+ "\n";
			msg += "dbcreate:" + item.getText(2) + "\n";

			if (!cs.SendBackGround(sShell, msg, cmd, Messages
					.getString("MENU.USERADMIN")))
				return false;

			/* save dbmtuser passwd */
			String setPasswdMsg = new String();
			setPasswdMsg = "targetid:" + item.getText(0) + "\n";
			setPasswdMsg += "newpassword:" + item.getData("passwd").toString()
					+ "\n";
			if (!cs.SendBackGround(sShell, setPasswdMsg, "setdbmtpasswd",
					Messages.getString("MENU.USERADMIN"))) {
				return false;
			}
		}

		if (!cs.SendClientMessage(sShell, "", "getdbmtuserinfo"))
			return false;

		tblUsers.removeAll();
		createItems();

		if (!cs.SendClientMessage(sShell, "", "startinfo"))
			return false;

		return true;
	}
}

class StructUserTable {
	public String id = new String();

	public String pw = new String();

	public String dba;

	public String cas;
	
	public String port;

	StructUserTable(String id, String pw, String dba, String cas,String port) {
		this.id = id;
		this.pw = pw;
		this.dba = dba;
		this.cas = cas;
		this.port = port;
	}
}
