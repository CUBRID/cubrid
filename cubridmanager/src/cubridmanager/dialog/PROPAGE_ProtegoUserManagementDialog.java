package cubridmanager.dialog;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
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

import cubrid.upa.UpaClient;
import cubrid.upa.UpaException;
import cubrid.upa.UpaUserInfo;
import cubridmanager.Application;
import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.action.ProtegoUserManagementAction;

import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;

public class PROPAGE_ProtegoUserManagementDialog extends Dialog {

	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="36,9"
	private Composite comparent = null;
	private Composite compositeList = null;
	private Table tableAuthInfo = null;
	private ArrayList userAuthItemList = new ArrayList();
	private Object[] objArray = null;
	private boolean sortApidDesc = false;
	private boolean sortDnDesc = false;
	private boolean sortDbnameDesc = false;
	private boolean sortDbuserDesc = false;
	private boolean sortExpTimeDesc = false;
	private boolean sortRegTimeDesc = false;
	private boolean sortManagerDnDesc = false;

	private static PROPAGE_ProtegoUserManagementDialog dlg = null;

	public UpaUserInfo[] userInfo = null;

	String a = new String();

	public PROPAGE_ProtegoUserManagementDialog(Shell parentShell) {
		super(parentShell);
		dlg = this;
	}

	public PROPAGE_ProtegoUserManagementDialog(IShellProvider parentShell) {
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
		/* this function is for visual-editor */
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
		// compositeList = new Composite(sShell, SWT.NONE);
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
		TableColumn AP_ID = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn User_DN = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn DBName = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn DBUser = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn Regist_time = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn Expire_time = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn Manager_DN = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn Note = new TableColumn(tableAuthInfo, SWT.LEFT);

		_internal.setText("");
		_internal.setResizable(false);
		_internal.setWidth(0);

		AP_ID.setText(Messages.getString("TABLE.APID"));
		AP_ID.setWidth(50);
		AP_ID
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = userAuthItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[1];
								String t2 = (String) ((String[]) o2)[1];
								if (sortApidDesc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);

								return ret_val;
							}
						});

						setTableInfo();
						restoreTableSelection(rowString);
						sortApidDesc = !sortApidDesc;
					}

					public void widgetDefaultSelected(
							org.eclipse.swt.events.SelectionEvent e) {
					}
				});
		User_DN.setText(Messages.getString("TABLE.USERDN"));
		User_DN.setWidth(100);
		User_DN
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = userAuthItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[2];
								String t2 = (String) ((String[]) o2)[2];
								if (sortDnDesc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);

								return ret_val;
							}
						});

						setTableInfo();
						restoreTableSelection(rowString);
						sortDnDesc = !sortDnDesc;
					}

					public void widgetDefaultSelected(
							org.eclipse.swt.events.SelectionEvent e) {
					}
				});
		DBName.setText(Messages.getString("TABLE.DATABASE"));
		DBName.setWidth(30);
		DBName
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = userAuthItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[3];
								String t2 = (String) ((String[]) o2)[3];
								if (sortDbnameDesc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);

								return ret_val;
							}
						});

						setTableInfo();
						restoreTableSelection(rowString);
						sortDbnameDesc = !sortDbnameDesc;
					}

					public void widgetDefaultSelected(
							org.eclipse.swt.events.SelectionEvent e) {
					}
				});
		DBUser.setText(Messages.getString("TABLE.DATABASEUSER"));
		DBUser.setWidth(30);
		DBUser
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = userAuthItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[4];
								String t2 = (String) ((String[]) o2)[4];
								if (sortDbuserDesc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);

								return ret_val;
							}
						});

						setTableInfo();
						restoreTableSelection(rowString);
						sortDbuserDesc = !sortDbuserDesc;
					}

					public void widgetDefaultSelected(
							org.eclipse.swt.events.SelectionEvent e) {
					}
				});
		Regist_time.setText(Messages.getString("TABLE.REGTIME"));
		Regist_time.setWidth(50);
		Regist_time
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = userAuthItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[4];
								String t2 = (String) ((String[]) o2)[4];
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
		Expire_time.setText(Messages.getString("TABLE.EXPIRETIME"));
		Expire_time.setWidth(50);
		Expire_time
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = userAuthItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[5];
								String t2 = (String) ((String[]) o2)[5];
								if (sortExpTimeDesc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);

								return ret_val;
							}
						});
						setTableInfo();
						restoreTableSelection(rowString);
						sortExpTimeDesc = !sortExpTimeDesc;
					}

					public void widgetDefaultSelected(
							org.eclipse.swt.events.SelectionEvent e) {
					}
				});
		Manager_DN.setText(Messages.getString("TABLE.MANAGERDN"));
		Manager_DN.setWidth(100);
		Manager_DN
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = userAuthItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[6];
								String t2 = (String) ((String[]) o2)[6];
								if (sortManagerDnDesc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);

								return ret_val;
							}
						});
						setTableInfo();
						restoreTableSelection(rowString);
						sortManagerDnDesc = !sortManagerDnDesc;
					}

					public void widgetDefaultSelected(
							org.eclipse.swt.events.SelectionEvent e) {
					}
				});
		Note.setText(Messages.getString("TABLE.NOTE"));
		Note.setWidth(200);
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
				ApplicationActionBarAdvisor.protegoUserAddAction
						.setEnabled(true);
				ApplicationActionBarAdvisor.protegoUserDeleteAction
						.setEnabled(enable);
				// UserChangeAction is not supported
				// ApplicationActionBarAdvisor.protegoUserChangeAction.setEnabled(enable);

				MenuManager addMgr = new MenuManager(Messages
						.getString("MENU.ADD"));
				addMgr.add(ApplicationActionBarAdvisor.protegoUserAddAction);
				addMgr.add(ApplicationActionBarAdvisor.protegoUserBulkAddAction);
				manager.add(addMgr);
				manager.add(ApplicationActionBarAdvisor.protegoUserDeleteAction);
				// manager.add(ApplicationActionBarAdvisor.protegoUserChangeAction);
				manager.add(new Separator());
				manager.add(ApplicationActionBarAdvisor.protegoUserSaveAsFileAction);
				manager.add(new Separator());
				manager.add(ApplicationActionBarAdvisor.protegoUserManagementRefreshAction);
			}
		};

		menuMgr.addMenuListener(menuListener);
		Menu menu = menuMgr.createContextMenu(tableAuthInfo);
		MenuItem newContextMenuItem = new MenuItem(menu, SWT.NONE);
		newContextMenuItem.setText("context.item"); //$NON-NLS-1$

		tableAuthInfo.setMenu(menu);
	}

	public void refreshListAndMenu() {
		userAuthItemList.clear();
		getUserInfo();
		if (userAuthItemList != null) {
			objArray = userAuthItemList.toArray();
			setTableInfo();
		}
		setContextMenu();
	}

	public void setTableInfo() {
		if (userAuthItemList == null || objArray == null)
			return;

		tableAuthInfo.removeAll();

		for (int i = 0; i < objArray.length; i++) {
			TableItem item = new TableItem(tableAuthInfo, SWT.NONE);
			String[] stringArray = (String[]) objArray[i];
			for (int j = 0; j < 9; j++)
				item.setText(j, stringArray[j]);
		}
	}

	public void getUserInfo() {
		UpaUserInfo usrInfo = new UpaUserInfo();
		if (!ProtegoUserManagementAction.dlg.isLoggedIn) {
			return;
		}

		try {
			userInfo = UpaClient.admUserInfo(
					ProtegoUserManagementAction.dlg.upaKey, usrInfo);
		} catch (UpaException ee) {
			CommonTool.ErrorBox(comparent.getShell(), Messages
					.getString("ERROR.GETUSERAUTHINFO"));
			return;
		}

		if (userInfo != null) {
			for (int i = 0; i < userInfo.length; i++) {
				String[] item = new String[9];
				item[0] = String.valueOf(i);
				item[1] = userInfo[i].getApid();
				item[2] = userInfo[i].getUuserdn();
				item[3] = userInfo[i].getDbname();
				item[4] = userInfo[i].getDbuser();
				item[5] = userInfo[i].getRegdate();
				item[6] = userInfo[i].getExpdate();
				item[7] = userInfo[i].getRegdn();
				item[8] = userInfo[i].getNote();

				userAuthItemList.add(item);
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

	static public void refresh() {
		dlg.refreshListAndMenu();
	}

	public static ArrayList getSelectedUserInfo() {
		TableItem[] items = dlg.tableAuthInfo.getSelection();
		ArrayList usrinfo = new ArrayList();
		for (int i = 0; i < items.length; i++) {
			String apId = items[i].getText(1);
			String dn = items[i].getText(2);
			String dbName = items[i].getText(3);
			usrinfo.add(new UpaUserInfo(apId, dn, dbName, "", "", "", "", ""));
		}
		return usrinfo;
	}

	public static void saveAsFile() {
		boolean isFailed = false;
		FileDialog fDlg = new FileDialog(Application.mainwindow.getShell(),
				SWT.SAVE | SWT.APPLICATION_MODAL);
		fDlg.setFilterExtensions(new String[] { "*.txt", "*.*" });
		fDlg.setFilterNames(new String[] { "Text file", "All files" });
		File curdir = new File(".");
		try {
			fDlg.setFilterPath(curdir.getCanonicalPath());
		} catch (Exception ee) {
			fDlg.setFilterPath(".");
		}

		String fileName = fDlg.open();
		if (fileName != null) {
			File targetFile = new File(fileName);
			FileWriter writer = null;
			BufferedWriter bufWriter = null;
			try {
				Table userTable = dlg.tableAuthInfo;
				writer = new FileWriter(targetFile);
				bufWriter = new BufferedWriter(writer);

				/*
				 * User registeration format : User-DN AP-ID DB-NAME DB-USER Expire-Date
				 */
				String ExpDate = new String();
				for (int i = 0; i < userTable.getItemCount(); i++) {
					bufWriter.write(userTable.getItem(i).getText(2)); // user dn
					bufWriter.write("\t");
					bufWriter.write(userTable.getItem(i).getText(1)); // ap id
					bufWriter.write("\t");
					bufWriter.write(userTable.getItem(i).getText(3)); // db name
					bufWriter.write("\t");
					bufWriter.write(userTable.getItem(i).getText(4)); // db user
					bufWriter.write("\t");
					ExpDate = userTable.getItem(i).getText(6); // Exp Date
					bufWriter.write(ExpDate.substring(0, 10));
					bufWriter.write("\n");
				}
			} catch (Exception ee) {
				CommonTool.ErrorBox(ee.getMessage());
				isFailed = true;
			}

			try {
				bufWriter.close();
				writer.close();
			} catch (Exception ee) {
			}

			if (!isFailed) {
				CommonTool.MsgBox(dlg.comparent.getShell(), Messages
						.getString("BUTTON.SAVETOFILE"), Messages
						.getString("TEXT.SUCCESSFULLYSAVEDTOFILE"));
			}
		}
	}
}
