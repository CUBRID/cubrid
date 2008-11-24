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
import java.util.Arrays;
import java.util.Comparator;

import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.action.Separator;
import org.eclipse.jface.dialogs.Dialog;
import org.eclipse.jface.window.IShellProvider;
import org.eclipse.swt.widgets.Shell;

import cubrid.upa.UpaAppInfo;
import cubrid.upa.UpaClient;
import cubrid.upa.UpaException;
import cubrid.upa.UpaUserInfo;
import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.action.ProtegoUserManagementAction;

import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;

public class PROPAGE_ProtegoAPIDManagementDialog extends Dialog {
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="36,9"
	private Composite comparent = null;
	private Composite compositeList = null;
	private Table tableAuthInfo = null;
	private ArrayList apIdItemList = new ArrayList();
	private Object[] objArray = null;
	private boolean sortRegTimeDesc = false;
	private boolean sortApIdDesc = false;
	private boolean sortTypeDesc = false;
	public static PROPAGE_ProtegoAPIDManagementDialog dlg = null;
	private UpaAppInfo[] appInfo = null;

	public PROPAGE_ProtegoAPIDManagementDialog(Shell parentShell) {
		super(parentShell);
		dlg = this;
	}

	public PROPAGE_ProtegoAPIDManagementDialog(IShellProvider parentShell) {
		super(parentShell);
		dlg = this;
	}

	public Composite SetTabPart(TabFolder parent) {
		comparent = parent;
		createCompositeList();
		compositeList.setParent(parent);
		return compositeList;
	}

	/*
	 * public boolean doModal() { createSShell(); sShell.open();
	 * 
	 * Display display = sShell.getDisplay(); while (!sShell.isDisposed()) { if
	 * (!display.readAndDispatch()) display.sleep(); }
	 * 
	 * return true; }
	 */
	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		sShell = new Shell();
		sShell.setLayout(new FillLayout());
		createCompositeList();
		// sShell.setSize(new org.eclipse.swt.graphics.Point(526,322));
	}

	/**
	 * This method initializes compositeList
	 * 
	 */
	private void createCompositeList() {
		compositeList = new Composite(comparent, SWT.NONE);
		// compositeList = new Composite(sShell, SWT.NONE); /* for visual-editor
		// */
		compositeList.setLayout(new GridLayout());
		createTableAuthInfo();
	}

	/**
	 * This method initializes tableAuthInfo
	 * 
	 */
	private void createTableAuthInfo() {
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.heightHint = 250;
		gridData.widthHint = 800;
		tableAuthInfo = new Table(compositeList, SWT.FULL_SELECTION | SWT.MULTI);
		tableAuthInfo.setHeaderVisible(true);
		tableAuthInfo.setLayoutData(gridData);
		tableAuthInfo.setLinesVisible(true);

		TableColumn _internal = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn apId = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn type = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn registTime = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn note = new TableColumn(tableAuthInfo, SWT.LEFT);

		_internal.setText("");
		_internal.setResizable(false);
		_internal.setWidth(0);

		apId.setText(Messages.getString("TABLE.APID"));
		apId.setWidth(60);
		apId.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = apIdItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[1];
								String t2 = (String) ((String[]) o2)[1];
								if (sortApIdDesc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);

								return ret_val;
							}
						});

						setTableInfo();
						restoreTableSelection(rowString);
						sortApIdDesc = !sortApIdDesc;
					}

					public void widgetDefaultSelected(
							org.eclipse.swt.events.SelectionEvent e) {
					}
				});

		type.setText(Messages.getString("TABLE.TYPE"));
		type.setWidth(60);
		type
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = apIdItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[2];
								String t2 = (String) ((String[]) o2)[2];
								if (sortTypeDesc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);

								return ret_val;
							}
						});

						setTableInfo();
						restoreTableSelection(rowString);
						sortTypeDesc = !sortTypeDesc;
					}

					public void widgetDefaultSelected(
							org.eclipse.swt.events.SelectionEvent e) {
					}
				});

		registTime.setText(Messages.getString("TABLE.REGTIME"));
		registTime.setWidth(150);
		registTime
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = apIdItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[3];
								String t2 = (String) ((String[]) o2)[3];
								if (sortRegTimeDesc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);

								return ret_val;
							}
						});

						setTableInfo();
						restoreTableSelection(rowString);
						sortRegTimeDesc = !sortRegTimeDesc;
					}

					public void widgetDefaultSelected(
							org.eclipse.swt.events.SelectionEvent e) {
					}
				});

		note.setText(Messages.getString("TABLE.NOTE"));
		note.setWidth(150);

		refreshListAndMenu();
	}

	public void setContextMenu() {
		MenuManager menuMgr = new MenuManager("PopupMenu", "contextMenu");
		menuMgr.setRemoveAllWhenShown(true);
		IMenuListener menuListener;

		menuListener = new IMenuListener() {
			public void menuAboutToShow(IMenuManager manager) {
				boolean enable = (tableAuthInfo.getSelectionIndex() == -1) ? false
						: true;
				ApplicationActionBarAdvisor.protegoAPIDAddAction
						.setEnabled(true);
				ApplicationActionBarAdvisor.protegoAPIDDeleteAction
						.setEnabled(enable);
				// APID change action is not supported
				// ApplicationActionBarAdvisor.protegoAPIDChangeAction.setEnabled(enable);
				// not supported
				ApplicationActionBarAdvisor.protegoUserManagementRefreshAction
						.setEnabled(true);

				manager.add(ApplicationActionBarAdvisor.protegoAPIDAddAction);
				manager
						.add(ApplicationActionBarAdvisor.protegoAPIDDeleteAction);
				// manager.add(ApplicationActionBarAdvisor.protegoAPIDChangeAction);
				manager.add(new Separator());
				manager
						.add(ApplicationActionBarAdvisor.protegoUserManagementRefreshAction);
			}
		};

		menuMgr.addMenuListener(menuListener);
		Menu menu = menuMgr.createContextMenu(tableAuthInfo);
		MenuItem newContextMenuItem = new MenuItem(menu, SWT.NONE);
		newContextMenuItem.setText("context.item"); //$NON-NLS-1$

		tableAuthInfo.setMenu(menu);
	}

	public void refreshListAndMenu() {
		apIdItemList.clear();
		getapIdInfo();
		if (apIdItemList != null) {
			objArray = apIdItemList.toArray();
			setTableInfo();
		}
		setContextMenu();
	}

	public void setTableInfo() {
		if (apIdItemList == null || objArray == null)
			return;

		tableAuthInfo.removeAll();

		for (int i = 0; i < objArray.length; i++) {
			TableItem item = new TableItem(tableAuthInfo, SWT.NONE);
			String[] stringArray = (String[]) objArray[i];
			for (int j = 0; j < 5; j++)
				item.setText(j, stringArray[j]);
		}
	}

	public void getapIdInfo() {
		try {
			appInfo = UpaClient.admAppCmd(
					ProtegoUserManagementAction.dlg.upaKey,
					UpaClient.UPA_USER_APID_INFO, new UpaUserInfo());
		} catch (UpaException ee) {
			CommonTool.ErrorBox(comparent.getShell(), Messages
					.getString("ERROR.GETAPIDINFO"));
			return;
		}

		if (appInfo != null) {
			for (int i = 0; i < appInfo.length; i++) {
				String[] item = new String[5];
				item[0] = String.valueOf(i);
				item[1] = appInfo[i].getApid();
				item[2] = (appInfo[i].getAptype() == (byte) 0) ? "system"
						: "user";
				item[3] = appInfo[i].getRegdate();
				item[4] = appInfo[i].getNot();

				apIdItemList.add(item);
			}
		}
	}

	public String getTableSelection() {
		String rowString = null;
		if (tableAuthInfo.getSelectionCount() > 0) {
			TableItem item = tableAuthInfo.getSelection()[0];
			if (item != null)
				rowString = new String(item.getText(0));
		}

		return rowString;
	}

	public void restoreTableSelection(String rowString) {
		if (rowString == null)
			return;

		if (!rowString.equals("")) {
			for (int i = 0; i < tableAuthInfo.getItemCount(); i++) {
				if (tableAuthInfo.getItem(i).getText(0).equals(rowString)) {
					tableAuthInfo.setSelection(i);
					break;
				}
			}
		}
	}

	public static void refresh() {
		dlg.refreshListAndMenu();
	}

	public static Object[] getAPIDList() {
		return dlg.objArray;
	}

	public static ArrayList getSelectedUserInfo() {
		TableItem[] items = dlg.tableAuthInfo.getSelection();
		ArrayList usrinfo = new ArrayList();
		for (int i = 0; i < items.length; i++) {
			String apId = new String(items[i].getText(1));
			if (apId.equals("cubridmanager") || apId.equals("anonymous")) {
				CommonTool.ErrorBox(dlg.comparent.getShell(), Messages
						.getString("ERROR.REMOVESYSTEMDEFINEDAPID"));
				return null;
			}
			usrinfo.add(new UpaUserInfo(items[i].getText(1), "", "", "", "",
					"", "", ""));
		}
		return usrinfo;
	}
}
