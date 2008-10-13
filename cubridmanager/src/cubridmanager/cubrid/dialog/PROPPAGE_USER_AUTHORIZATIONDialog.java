package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.cubrid.Authorizations;
import cubridmanager.cubrid.SchemaInfo;
import cubridmanager.cubrid.UserInfo;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.dialog.PROPPAGE_USER_GENERALDialog;
import java.util.ArrayList;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.CellEditor;
import org.eclipse.jface.viewers.ICellModifier;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.jface.viewers.ComboBoxCellEditor;
import org.eclipse.jface.viewers.Viewer;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.events.FocusEvent;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class PROPPAGE_USER_AUTHORIZATIONDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite comparent = null;
	private Composite sShell = null;
	private Table LIST_CURRENT_CLASSES = null;
	private Button BUTTON_USERINFO_ADDCLASS = null;
	private Button BUTTON_USERINFO_DELETECLASS = null;
	public Table LIST_AUTHORIZATIONS = null;
	public static final String[] PROPS = new String[15];
	public static final String[] ynstr = { "Y", "N" };

	public PROPPAGE_USER_AUTHORIZATIONDialog(Shell parent) {
		super(parent);
	}

	public PROPPAGE_USER_AUTHORIZATIONDialog(Shell parent, int style) {
		super(parent, style);
	}

	public Composite SetTabPart(TabFolder parent, boolean isDba) {
		comparent = parent;
		if (isDba) {
			sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
			sShell.setLayout(new GridLayout());
			Label lblItIsDda = new Label(sShell, SWT.WRAP);
			lblItIsDda.setText(Messages.getString("LABEL.ITISDBA"));
			lblItIsDda
					.setLayoutData(new GridData(GridData.GRAB_HORIZONTAL
							| GridData.GRAB_VERTICAL
							| GridData.HORIZONTAL_ALIGN_CENTER));

		} else
			createComposite();
		sShell.setParent(parent);
		return sShell;
	}

	public int doModal() {
		createSShell();
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		dlgShell = new Shell(comparent.getShell(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages
				.getString("TITLE.PROPPAGE_USER_AUTHORIZATIONDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
		sShell.setLayout(gridLayout);
		createTable1();

		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		// gridData2.widthHint = 100;
		// gridData2.grabExcessVerticalSpace = true;
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		BUTTON_USERINFO_ADDCLASS = new Button(sShell, SWT.NONE);
		BUTTON_USERINFO_ADDCLASS.setText(Messages.getString("BUTTON.ADDCLASS"));
		BUTTON_USERINFO_ADDCLASS.setLayoutData(gridData2);
		BUTTON_USERINFO_ADDCLASS
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						e.doit = false;

						int idx = LIST_CURRENT_CLASSES.getSelectionIndex();
						if (idx < 0)
							return;
						TableItem ti = LIST_CURRENT_CLASSES.getItem(idx);
						if (ti.getText(2).equals(
								PROPPAGE_USER_GENERALDialog.DBUser)) {
							CommonTool.ErrorBox(sShell.getShell(), Messages
									.getString("ERROR.CANNOTGRANTTOYOURSELF"));
							return;
						}
						String addclass = ti.getText(0);
						for (int i = 0, n = LIST_AUTHORIZATIONS.getItemCount(); i < n; i++) {
							if (LIST_AUTHORIZATIONS.getItem(i).getText(0)
									.equals(addclass)) {
								CommonTool
										.ErrorBox(
												sShell.getShell(),
												Messages
														.getString("ERROR.CLASSNAMEALREADYEXIST"));
								return;
							}
						}
						TableItem item = new TableItem(LIST_AUTHORIZATIONS,
								SWT.NONE);
						item.setText(0, addclass);
						item.setText(1, "Y");
						for (int i = 2; i < 15; i++)
							item.setText(i, "N");

						LIST_CURRENT_CLASSES.remove(idx);
						LIST_AUTHORIZATIONS.setFocus();
						LIST_AUTHORIZATIONS
								.setSelection(new TableItem[] { item });
						BUTTON_USERINFO_DELETECLASS.setEnabled(true);
						BUTTON_USERINFO_ADDCLASS.setEnabled(false);
					}
				});

		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.grabExcessHorizontalSpace = true;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		BUTTON_USERINFO_DELETECLASS = new Button(sShell, SWT.NONE);
		BUTTON_USERINFO_DELETECLASS.setText(Messages
				.getString("BUTTON.DELETECLASS"));
		BUTTON_USERINFO_DELETECLASS.setLayoutData(gridData3);
		BUTTON_USERINFO_DELETECLASS
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						e.doit = false;

						int idx = LIST_AUTHORIZATIONS.getSelectionIndex();
						if (idx < 0)
							return;

						String tableName = LIST_AUTHORIZATIONS.getItem(idx)
								.getText(0);
						ArrayList sinfo = SchemaInfo
								.SchemaInfo_get(CubridView.Current_db);
						for (int i = 0, n = sinfo.size(); i < n; i++) {
							SchemaInfo si = (SchemaInfo) sinfo.get(i);
							if (si.name.equals(tableName)) {
								if (si.isSystemClass()) {
									CommonTool
											.WarnBox(
													sShell.getShell(),
													Messages
															.getString("WARNING.CANNOTREVOKESYSTEMCLASS"));
									return;
								}
								TableItem item = new TableItem(
										LIST_CURRENT_CLASSES, SWT.NONE);
								item.setText(0, si.name);
								item
										.setText(
												1,
												si.type.equals("system") ? Messages
														.getString("TREE.SYSSCHEMA")
														: Messages
																.getString("TREE.USERSCHEMA"));
								item.setText(2, si.schemaowner);
								StringBuffer superClass = new StringBuffer("");
								for (int i2 = 0, n2 = si.superClasses.size(); i2 < n2; i2++) {
									if (superClass.length() < 1)
										superClass = superClass
												.append((String) si.superClasses
														.get(i2));
									else {
										superClass = superClass.append(", ");
										superClass = superClass
												.append((String) si.superClasses
														.get(i2));
									}
								}
								item.setText(3, superClass.toString());
								item
										.setText(
												4,
												si.virtual.equals("normal") ? Messages
														.getString("TREE.TABLE")
														: Messages
																.getString("TREE.VIEW"));
								LIST_CURRENT_CLASSES
										.setSelection(new TableItem[] { item });
								break;
							}
						}

						LIST_AUTHORIZATIONS.remove(idx);
						LIST_CURRENT_CLASSES.setFocus();
						BUTTON_USERINFO_DELETECLASS.setEnabled(false);
						BUTTON_USERINFO_ADDCLASS.setEnabled(true);
					}
				});
		createTable2();
		setinfo();
		sShell.pack();
	}

	private void createTable1() {
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.heightHint = 160;
		gridData.widthHint = 550;
		gridData.grabExcessHorizontalSpace = true;
		LIST_CURRENT_CLASSES = new Table(sShell, SWT.FULL_SELECTION
				| SWT.BORDER);
		LIST_CURRENT_CLASSES.setLinesVisible(true);
		LIST_CURRENT_CLASSES.setLayoutData(gridData);
		LIST_CURRENT_CLASSES.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 90, true));
		tlayout.addColumnData(new ColumnWeightData(50, 90, true));
		tlayout.addColumnData(new ColumnWeightData(50, 90, true));
		tlayout.addColumnData(new ColumnWeightData(50, 90, true));
		tlayout.addColumnData(new ColumnWeightData(50, 90, true));
		LIST_CURRENT_CLASSES.setLayout(tlayout);
		LIST_CURRENT_CLASSES
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (LIST_CURRENT_CLASSES.getSelectionIndex() > -1)
							BUTTON_USERINFO_ADDCLASS.setEnabled(true);
						else
							BUTTON_USERINFO_ADDCLASS.setEnabled(false);
						BUTTON_USERINFO_DELETECLASS.setEnabled(false);
					}
				});

		LIST_CURRENT_CLASSES
				.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusGained(FocusEvent e) {
						if (LIST_CURRENT_CLASSES.getSelectionIndex() > -1)
							BUTTON_USERINFO_ADDCLASS.setEnabled(true);
						else
							BUTTON_USERINFO_ADDCLASS.setEnabled(false);
						BUTTON_USERINFO_DELETECLASS.setEnabled(false);
					}
				});

		TableColumn tblcol = new TableColumn(LIST_CURRENT_CLASSES, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NAME"));
		tblcol = new TableColumn(LIST_CURRENT_CLASSES, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.SCHEMATYPE"));
		tblcol = new TableColumn(LIST_CURRENT_CLASSES, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.OWNER"));
		tblcol = new TableColumn(LIST_CURRENT_CLASSES, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.SUPERCLASS"));
		tblcol = new TableColumn(LIST_CURRENT_CLASSES, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.VIRTUAL"));
	}

	private void createTable2() {
		final TableViewer tv = new TableViewer(sShell, SWT.FULL_SELECTION
				| SWT.BORDER);
		LIST_AUTHORIZATIONS = tv.getTable();
		LIST_AUTHORIZATIONS.setBounds(new org.eclipse.swt.graphics.Rectangle(
				14, 228, 576, 160));
		LIST_AUTHORIZATIONS.setLinesVisible(true);
		LIST_AUTHORIZATIONS.setHeaderVisible(true);
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.heightHint = 160;
		gridData.widthHint = 550;
		gridData.grabExcessHorizontalSpace = true;
		LIST_AUTHORIZATIONS.setLayoutData(gridData);

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(20, 100, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 70, true));
		LIST_AUTHORIZATIONS.setLayout(tlayout);

		TableColumn tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.CLASS"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.SELECT"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.INSERT"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.UPDATE"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.DELETE"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.ALTER"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.INDEX1"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.EXECUTE"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTSELECT"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTINSERT"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTUPDATE"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTDELETE"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTALTER"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTINDEX"));
		tblColumn = new TableColumn(LIST_AUTHORIZATIONS, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTEXECUTE"));

		CellEditor[] editors = new CellEditor[15];
		editors[0] = null;
		for (int i = 0; i < 15; i++) {
			PROPS[i] = LIST_AUTHORIZATIONS.getColumn(i).getText();
			if (i > 0)
				editors[i] = new ComboBoxCellEditor(LIST_AUTHORIZATIONS, ynstr,
						SWT.READ_ONLY);
		}
		tv.setColumnProperties(PROPS);
		tv.setCellEditors(editors);
		tv.setCellModifier(new YnCellModifier(tv));

		LIST_AUTHORIZATIONS
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (LIST_AUTHORIZATIONS.getSelectionIndex() > -1)
							BUTTON_USERINFO_DELETECLASS.setEnabled(true);
						else
							BUTTON_USERINFO_DELETECLASS.setEnabled(false);
						BUTTON_USERINFO_ADDCLASS.setEnabled(false);
					}
				});

		LIST_AUTHORIZATIONS
				.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusGained(FocusEvent e) {
						if (LIST_AUTHORIZATIONS.getSelectionIndex() > -1)
							BUTTON_USERINFO_DELETECLASS.setEnabled(true);
						else
							BUTTON_USERINFO_DELETECLASS.setEnabled(false);
						BUTTON_USERINFO_ADDCLASS.setEnabled(false);
					}
				});
	}

	private void setinfo() {
		ArrayList sinfo = SchemaInfo.SchemaInfo_get(CubridView.Current_db);
		for (int i = 0, n = sinfo.size(); i < n; i++) {
			SchemaInfo si = (SchemaInfo) sinfo.get(i);
			if (!si.type.equals("user"))
				continue;
			TableItem item = new TableItem(LIST_CURRENT_CLASSES, SWT.NONE);
			item.setText(0, si.name);
			item.setText(1, si.type.equals("system") ? Messages
					.getString("TREE.SYSSCHEMA") : Messages
					.getString("TREE.USERSCHEMA"));
			item.setText(2, si.schemaowner);
			String superstr = new String("");
			for (int i2 = 0, n2 = si.superClasses.size(); i2 < n2; i2++) {
				if (superstr.length() < 1)
					superstr = superstr
							.concat((String) si.superClasses.get(i2));
				else
					superstr = superstr.concat(", "
							+ (String) si.superClasses.get(i2));
			}
			item.setText(3, superstr);
			item.setText(4, si.virtual.equals("normal") ? Messages
					.getString("TREE.TABLE") : Messages.getString("TREE.VIEW"));
		}

		if (PROPPAGE_USER_GENERALDialog.DBUser == null
				|| PROPPAGE_USER_GENERALDialog.DBUser.length() <= 0)
			return;
		ArrayList userinfo = UserInfo.UserInfo_get(CubridView.Current_db);
		UserInfo ui = UserInfo.UserInfo_find(userinfo,
				PROPPAGE_USER_GENERALDialog.DBUser);

		TableItem item;

		ArrayList alreadyGrantTable = new ArrayList();
		for (int i = 0, n = ui.authorizations.size(); i < n; i++) {
			Authorizations auth = (Authorizations) ui.authorizations.get(i);
			alreadyGrantTable.add(auth.className);
			item = new TableItem(LIST_AUTHORIZATIONS, SWT.NONE);
			item.setText(0, auth.className);
			item.setText(1, CommonTool.BooleanYN(auth.selectPriv));
			item.setText(2, CommonTool.BooleanYN(auth.insertPriv));
			item.setText(3, CommonTool.BooleanYN(auth.updatePriv));
			item.setText(4, CommonTool.BooleanYN(auth.deletePriv));
			item.setText(5, CommonTool.BooleanYN(auth.alterPriv));
			item.setText(6, CommonTool.BooleanYN(auth.indexPriv));
			item.setText(7, CommonTool.BooleanYN(auth.executePriv));
			item.setText(8, CommonTool.BooleanYN(auth.grantSelectPriv));
			item.setText(9, CommonTool.BooleanYN(auth.grantInsertPriv));
			item.setText(10, CommonTool.BooleanYN(auth.grantUpdatePriv));
			item.setText(11, CommonTool.BooleanYN(auth.grantDeletePriv));
			item.setText(12, CommonTool.BooleanYN(auth.grantAlterPriv));
			item.setText(13, CommonTool.BooleanYN(auth.grantIndexPriv));
			item.setText(14, CommonTool.BooleanYN(auth.grantExecutePriv));
		}

		for (int i = LIST_CURRENT_CLASSES.getItemCount() - 1, n = -1; i > n; i--) {
			for (int j = 0, m = alreadyGrantTable.size(); j < m; j++) {
				if (LIST_CURRENT_CLASSES.getItem(i).getText(0).equals(
						alreadyGrantTable.get(j))) {
					LIST_CURRENT_CLASSES.remove(i);
					alreadyGrantTable.remove(j);
					alreadyGrantTable.trimToSize();
					break;
				}
			}
		}

		for (int i = 0, n = LIST_CURRENT_CLASSES.getColumnCount(); i < n; i++) {
			LIST_CURRENT_CLASSES.getColumn(i).pack();
		}
		for (int i = 0, n = LIST_AUTHORIZATIONS.getColumnCount(); i < n; i++) {
			LIST_AUTHORIZATIONS.getColumn(i).pack();
		}

		BUTTON_USERINFO_ADDCLASS.setEnabled(false);
		BUTTON_USERINFO_DELETECLASS.setEnabled(false);
	}
}

class YnCellModifier implements ICellModifier {
	Table tbl = null;

	public YnCellModifier(Viewer viewer) {
		tbl = ((TableViewer) viewer).getTable();
	}

	public boolean canModify(Object element, String property) {
		if (property == null)
			return false;
		if (PROPPAGE_USER_AUTHORIZATIONDialog.PROPS[0].equals(property))
			return false;

		if (tbl.getSelectionCount() > 0) {
			TableItem[] items = tbl.getSelection();

			ArrayList sinfo = SchemaInfo.SchemaInfo_get(CubridView.Current_db);
			for (int i = 0, n = sinfo.size(); i < n; i++) {
				SchemaInfo si = (SchemaInfo) sinfo.get(i);
				if (si.name.equals(items[0].getText(0))) {
					return si.isSystemClass() ? false : true;
				}
			}
		}

		return true;
	}

	public Object getValue(Object element, String property) {
		TableItem[] tis = tbl.getSelection();
		for (int i = 1; i < 15; i++) {
			if (PROPPAGE_USER_AUTHORIZATIONDialog.PROPS[i].equals(property)) {
				return (tis[0].getText(i).equals("Y")) ? new Integer(0)
						: new Integer(1);
			}
		}
		return Boolean.valueOf(false);
	}

	public void modify(Object element, String property, Object value) {
		String yn = (((Integer) value).intValue() == 0) ? "Y" : "N";
		for (int i = 1; i < 15; i++) {
			if (PROPPAGE_USER_AUTHORIZATIONDialog.PROPS[i].equals(property)) {
				((TableItem) element).setText(i, yn);
				((TableItem) element).getParent().redraw();
			}
		}
	}
}
