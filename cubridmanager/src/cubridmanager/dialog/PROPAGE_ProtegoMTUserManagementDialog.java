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

import cubrid.upa.UpaClient;
import cubrid.upa.UpaException;
import cubrid.upa.UpaMtInfo;
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

public class PROPAGE_ProtegoMTUserManagementDialog extends Dialog {

	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="36,9"
	private Composite compositeList = null;
	private Composite comparent = null;
	private Table tableAuthInfo = null;
	private ArrayList mtItemList = new ArrayList();
	private Object[] objArray = null;
	private boolean sortDnDesc = false;
	private boolean sortMtIdDesc = false;
	private boolean sortApIdDesc = false;
	private boolean sortDbNameDesc = false;
	private boolean sortDbUserDesc = false;
	private boolean sortRegTimeDesc = false;
	private boolean sortExpTimeDesc = false;
	private static PROPAGE_ProtegoMTUserManagementDialog dlg = null;

	private UpaMtInfo[] mtInfo = null;

	public PROPAGE_ProtegoMTUserManagementDialog(Shell parentShell) {
		super(parentShell);
		dlg = this;
	}

	public PROPAGE_ProtegoMTUserManagementDialog(IShellProvider parentShell) {
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
		tableAuthInfo = new Table(compositeList, SWT.FULL_SELECTION);
		tableAuthInfo.setHeaderVisible(true);
		tableAuthInfo.setLayoutData(gridData);
		tableAuthInfo.setLinesVisible(true);

		TableColumn _internal = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn userDn = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn mtId = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn apId = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn registTime = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn expireTime = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn dbName = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn dbUser = new TableColumn(tableAuthInfo, SWT.LEFT);
		TableColumn note = new TableColumn(tableAuthInfo, SWT.LEFT);

		_internal.setText("");
		_internal.setResizable(false);
		_internal.setWidth(0);

		userDn.setText(Messages.getString("TABLE.USERDN"));
		userDn.setWidth(50);
		userDn
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = mtItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[1];
								String t2 = (String) ((String[]) o2)[1];
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

		mtId.setText(Messages.getString("TABLE.MAINTENECEID"));
		mtId.setWidth(50);
		mtId
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = mtItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[2];
								String t2 = (String) ((String[]) o2)[2];
								if (sortMtIdDesc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);

								return ret_val;
							}
						});

						setTableInfo();
						restoreTableSelection(rowString);
						sortMtIdDesc = !sortMtIdDesc;
					}

					public void widgetDefaultSelected(
							org.eclipse.swt.events.SelectionEvent e) {
					}
				});

		/*
		 * apId.setText(Messages.getString("TABLE.APID")); apId.setWidth(50);
		 * apId.addSelectionListener(new
		 * org.eclipse.swt.events.SelectionListener() { public void
		 * widgetSelected(org.eclipse.swt.events.SelectionEvent e) { String
		 * rowString = getTableSelection();
		 * objArray = mtItemList.toArray();
		 * Arrays.sort(objArray, new Comparator(){ public int compare(Object o1,
		 * Object o2) { int ret_val;
		 * 
		 * String t1 = (String)((String[])o1)[3]; String t2 =
		 * (String)((String[])o2)[3]; if (sortApIdDesc) ret_val =
		 * t1.compareTo(t2); else ret_val = t2.compareTo(t1);
		 * 
		 * return ret_val; } });
		 * 
		 * setTableInfo(); restoreTableSelection(rowString); sortApIdDesc =
		 * !sortApIdDesc; } public void
		 * widgetDefaultSelected(org.eclipse.swt.events.SelectionEvent e) {} });
		 * 
		 */

		registTime.setText(Messages.getString("TABLE.REGTIME"));
		registTime.setWidth(150);
		registTime
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = mtItemList.toArray();
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

		expireTime.setText(Messages.getString("TABLE.EXPIRETIME"));
		expireTime.setWidth(150);
		expireTime
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = mtItemList.toArray();
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

		dbName.setText(Messages.getString("TABLE.DATABASE"));
		dbName.setWidth(30);
		dbName
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = mtItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[6];
								String t2 = (String) ((String[]) o2)[6];
								if (sortDbNameDesc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);

								return ret_val;
							}
						});

						setTableInfo();
						restoreTableSelection(rowString);
						sortDbNameDesc = !sortDbNameDesc;
					}

					public void widgetDefaultSelected(
							org.eclipse.swt.events.SelectionEvent e) {
					}
				});

		dbUser.setText(Messages.getString("TABLE.DATABASEUSER"));
		dbUser.setWidth(30);
		dbUser
				.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String rowString = getTableSelection();

						// Convert to array and using "array.sort" function
						objArray = mtItemList.toArray();
						Arrays.sort(objArray, new Comparator() {
							public int compare(Object o1, Object o2) {
								int ret_val;

								String t1 = (String) ((String[]) o1)[7];
								String t2 = (String) ((String[]) o2)[7];
								if (sortDbUserDesc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);

								return ret_val;
							}
						});

						setTableInfo();
						restoreTableSelection(rowString);
						sortDbUserDesc = !sortDbUserDesc;
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
				ApplicationActionBarAdvisor.protegoMTUserAddAction
						.setEnabled(true);
				// MT user delete & change action is not supported
				// ApplicationActionBarAdvisor.protegoMTUserDeleteAction.setEnabled(enable);
				// ApplicationActionBarAdvisor.protegoMTUserChangeAction.setEnabled(enable);

				manager.add(ApplicationActionBarAdvisor.protegoMTUserAddAction);
				// manager.add(ApplicationActionBarAdvisor.protegoMTUserDeleteAction);
				// manager.add(ApplicationActionBarAdvisor.protegoMTUserChangeAction);
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
		mtItemList.clear();
		getMtInfo();
		if (mtItemList != null) {
			objArray = mtItemList.toArray();
			setTableInfo();
		}
		setContextMenu();
	}

	public void setTableInfo() {
		if (mtItemList == null || objArray == null)
			return;

		tableAuthInfo.removeAll();

		for (int i = 0; i < objArray.length; i++) {
			TableItem item = new TableItem(tableAuthInfo, SWT.NONE);
			String[] stringArray = (String[]) objArray[i];
			for (int j = 0; j < 9; j++)
				item.setText(j, stringArray[j]);
		}
	}

	public void getMtInfo() {
		if (!ProtegoUserManagementAction.dlg.isLoggedIn) {
			return;
		}

		try {
			mtInfo = UpaClient.admMtInfo(
					ProtegoUserManagementAction.dlg.upaKey, true,
					new UpaMtInfo());
		} catch (UpaException ee) {
			CommonTool.ErrorBox(comparent.getShell(), "Cannot get auth transfer information");
			return;
		}

		if (mtInfo != null) {
			for (int i = 0; i < mtInfo.length; i++) {
				String[] item = new String[9];
				item[0] = String.valueOf(i);
				item[1] = mtInfo[i].getDn();
				item[2] = mtInfo[i].getId();

				item[3] = "reg date";// mtInfo[i].getRegdate();
				item[4] = "exp date";// mtInfo[i].getExpdate();
				item[5] = mtInfo[i].getDbname();
				item[6] = mtInfo[i].getName();
				item[7] = mtInfo[i].getNote();

				mtItemList.add(item);
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
}
