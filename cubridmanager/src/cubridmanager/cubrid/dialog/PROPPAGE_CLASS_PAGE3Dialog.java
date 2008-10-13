package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.TabFolder;
import cubridmanager.cubrid.action.TablePropertyAction;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.SchemaInfo;
import cubridmanager.cubrid.*;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class PROPPAGE_CLASS_PAGE3Dialog extends Dialog {
	private Shell dlgShell = null;
	private Composite comparent = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Table LIST_CLASS_METHODS = null;
	private Button BUTTON_CLASS_METHOD_ADD = null;
	private Button BUTTON_CLASS_METHOD_DELETE = null;
	private Button BUTTON_CLASS_METHOD_EDIT = null;
	private Label label2 = null;
	private Table LIST_CLASS_METHODFILES = null;
	private Button BUTTON_CLASS_METHODFILE_ADD = null;
	private Button BUTTON_CLASS_METHODFILE_DELETE = null;
	public static SchemaInfo si = null;

	public PROPPAGE_CLASS_PAGE3Dialog(Shell parent) {
		super(parent);
	}

	public PROPPAGE_CLASS_PAGE3Dialog(Shell parent, int style) {
		super(parent, style);
	}

	public Composite SetTabPart(TabFolder parent) {
		comparent = parent;
		createComposite();
		setinfo();
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
		dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell
				.setText(Messages.getString("TITLE.PROPPAGE_CLASS_PAGE3DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.widthHint = 90;
		gridData8.grabExcessHorizontalSpace = false;
		gridData8.horizontalSpan = 1;
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.horizontalSpan = 3;
		gridData7.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData7.widthHint = 90;
		gridData7.grabExcessHorizontalSpace = true;
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalSpan = 4;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 90;
		gridData4.grabExcessHorizontalSpace = false;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalSpan = 1;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData3.widthHint = 90;
		gridData3.grabExcessHorizontalSpace = false;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData2.widthHint = 90;
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.horizontalSpan = 2;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 4;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
		sShell.setLayout(gridLayout);
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.METHODS"));
		label1.setLayoutData(gridData);
		createTable1();
		BUTTON_CLASS_METHOD_ADD = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_METHOD_ADD.setText(Messages.getString("BUTTON.ADD1"));
		BUTTON_CLASS_METHOD_ADD.setLayoutData(gridData2);
		BUTTON_CLASS_METHOD_ADD
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ADD_METHODDialog dlg = new ADD_METHODDialog(sShell
								.getShell());
						if (dlg.doModal()) {
							setinfo();
						}
					}
				});
		BUTTON_CLASS_METHOD_EDIT = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_METHOD_EDIT.setText(Messages.getString("BUTTON.EDIT"));
		BUTTON_CLASS_METHOD_EDIT.setLayoutData(gridData4);
		BUTTON_CLASS_METHOD_EDIT
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (LIST_CLASS_METHODS.getSelectionCount() < 1)
							return;
						EDIT_METHODDialog dlg = new EDIT_METHODDialog(sShell
								.getShell());
						if (dlg.doModal(LIST_CLASS_METHODS.getSelection()[0])) {
							setinfo();
						}
					}
				});
		BUTTON_CLASS_METHOD_DELETE = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_METHOD_DELETE.setText(Messages.getString("BUTTON.DELETE"));
		BUTTON_CLASS_METHOD_DELETE.setLayoutData(gridData3);
		BUTTON_CLASS_METHOD_DELETE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (LIST_CLASS_METHODS.getSelectionCount() < 1)
							return;
						String col0 = LIST_CLASS_METHODS.getSelection()[0]
								.getText(0);
						String col1 = LIST_CLASS_METHODS.getSelection()[0]
								.getText(1);
						if (CommonTool.WarnYesNo(sShell.getShell(), Messages
								.getString("WARNYESNO.DELETE")
								+ " " + col1) != SWT.YES)
							return;
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "classname:" + si.name + "\n";
						msg += "methodname:" + col1 + "\n";
						msg += "category:"
								+ ((col0.equals("Yes")) ? "class" : "instance");

						ClientSocket cs = new ClientSocket();
						if (!cs.SendBackGround(sShell.getShell(), msg,
								"dropmethod", Messages
										.getString("WAITING.DROPMETHOD"))) {
							CommonTool.ErrorBox(sShell.getShell(), cs.ErrorMsg);
							return;
						}
						setinfo();
					}
				});

		label2 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.METHODFILES"));
		label2.setLayoutData(gridData5);
		createTable2();
		BUTTON_CLASS_METHODFILE_ADD = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_METHODFILE_ADD.setText(Messages.getString("BUTTON.ADD1"));
		BUTTON_CLASS_METHODFILE_ADD.setLayoutData(gridData7);
		BUTTON_CLASS_METHODFILE_ADD
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ADD_METHODFILEDialog dlg = new ADD_METHODFILEDialog(
								sShell.getShell());
						if (dlg.doModal()) {
							setinfo();
						}
					}
				});
		BUTTON_CLASS_METHODFILE_DELETE = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_METHODFILE_DELETE.setText(Messages
				.getString("BUTTON.DELETE"));
		BUTTON_CLASS_METHODFILE_DELETE.setLayoutData(gridData8);
		BUTTON_CLASS_METHODFILE_DELETE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (LIST_CLASS_METHODFILES.getSelectionCount() < 1)
							return;
						String col0 = LIST_CLASS_METHODFILES.getSelection()[0]
								.getText(0);
						if (CommonTool.WarnYesNo(sShell.getShell(), Messages
								.getString("WARNYESNO.DELETE")
								+ " " + col0) != SWT.YES)
							return;
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "classname:" + si.name + "\n";
						msg += "methodfile:" + col0 + "\n";

						ClientSocket cs = new ClientSocket();
						if (!cs.SendBackGround(sShell.getShell(), msg,
								"dropmethodfile", Messages
										.getString("WAITING.DROPMETHODFILE"))) {
							CommonTool.ErrorBox(sShell.getShell(), cs.ErrorMsg);
							return;
						}
						setinfo();
					}
				});
		sShell.pack();
	}

	private void createTable1() {
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 4;
		gridData1.grabExcessVerticalSpace = true;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.heightHint = 200;
		gridData1.widthHint = 550;
		gridData1.grabExcessHorizontalSpace = true;
		LIST_CLASS_METHODS = new Table(sShell, SWT.FULL_SELECTION | SWT.SINGLE
				| SWT.BORDER);
		LIST_CLASS_METHODS.setLinesVisible(true);
		LIST_CLASS_METHODS.setLayoutData(gridData1);
		LIST_CLASS_METHODS.setHeaderVisible(true);

		TableColumn tblcol = new TableColumn(LIST_CLASS_METHODS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.ISCLASSMEMBER"));
		tblcol = new TableColumn(LIST_CLASS_METHODS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NAME"));
		tblcol = new TableColumn(LIST_CLASS_METHODS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.INHERIT"));
		tblcol = new TableColumn(LIST_CLASS_METHODS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.RETURNTYPE"));
		tblcol = new TableColumn(LIST_CLASS_METHODS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.ARGUMENTTYPE"));
		tblcol = new TableColumn(LIST_CLASS_METHODS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.FUNCTIONNAME"));
		LIST_CLASS_METHODS
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setBtnEnable();
					}
				});
	}

	private void createTable2() {
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.horizontalSpan = 4;
		gridData6.grabExcessVerticalSpace = true;
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData6.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData6.heightHint = 90;
		gridData6.widthHint = 550;
		gridData6.grabExcessHorizontalSpace = true;
		LIST_CLASS_METHODFILES = new Table(sShell, SWT.FULL_SELECTION
				| SWT.SINGLE | SWT.BORDER);
		LIST_CLASS_METHODFILES.setLinesVisible(true);
		LIST_CLASS_METHODFILES.setLayoutData(gridData6);
		LIST_CLASS_METHODFILES.setHeaderVisible(false);

		TableColumn tblcol = new TableColumn(LIST_CLASS_METHODFILES, SWT.LEFT);
		tblcol.setText("col1");
		LIST_CLASS_METHODFILES
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setBtnEnable();
					}
				});
	}

	private void setinfo() {
		si = TablePropertyAction.si;
		LIST_CLASS_METHODS.setToolTipText(Messages
				.getString("TOOLTIP.CLASSLISTMETHODS"));
		LIST_CLASS_METHODFILES.setToolTipText(Messages
				.getString("TOOLTIP.CLASSLISTMETHODFILES"));

		LIST_CLASS_METHODS.removeAll();
		for (int i = 0, n = si.classMethods.size(); i < n; i++) {
			DBMethod rec = (DBMethod) si.classMethods.get(i);
			TableItem item = new TableItem(LIST_CLASS_METHODS, SWT.NONE);
			item.setText(0, Messages.getString("RADIO.CLASS"));
			item.setText(1, rec.name);
			item.setText(2, rec.inherit);
			String ret_arg = CommonTool.ArrayToString(rec.arguments), ret = null, arg = null;
			int idx = ret_arg.indexOf(",");
			if (idx < 0) {
				ret = ret_arg;
				arg = "";
			} else {
				ret = ret_arg.substring(0, idx);
				arg = ret_arg.substring(idx + 1);
			}
			item.setText(3, ret);
			item.setText(4, arg);
			item.setText(5, rec.function);
		}
		for (int i = 0, n = si.methods.size(); i < n; i++) {
			DBMethod rec = (DBMethod) si.methods.get(i);
			TableItem item = new TableItem(LIST_CLASS_METHODS, SWT.NONE);
			item.setText(0, Messages.getString("RADIO.INSTANCE"));
			item.setText(1, rec.name);
			item.setText(2, rec.inherit);
			String ret_arg = CommonTool.ArrayToString(rec.arguments), ret = null, arg = null;
			int idx = ret_arg.indexOf(",");
			if (idx < 0) {
				ret = ret_arg;
				arg = "";
			} else {
				ret = ret_arg.substring(0, idx);
				arg = ret_arg.substring(idx + 1);
			}
			item.setText(3, ret);
			item.setText(4, arg);
			item.setText(5, rec.function);
		}

		LIST_CLASS_METHODFILES.removeAll();
		for (int i = 0, n = si.methodFiles.size(); i < n; i++) {
			String rec = (String) si.methodFiles.get(i);
			TableItem item = new TableItem(LIST_CLASS_METHODFILES, SWT.NONE);
			item.setText(0, rec);
		}

		if (si.type.equals("system")) {
			BUTTON_CLASS_METHOD_ADD.setEnabled(false);
			BUTTON_CLASS_METHOD_DELETE.setEnabled(false);
			BUTTON_CLASS_METHOD_EDIT.setEnabled(false);
			BUTTON_CLASS_METHODFILE_ADD.setEnabled(false);
			BUTTON_CLASS_METHODFILE_DELETE.setEnabled(false);
		}

		for (int i = 0, n = LIST_CLASS_METHODS.getColumnCount(); i < n; i++) {
			LIST_CLASS_METHODS.getColumn(i).pack();
		}

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(100, 30, false));
		LIST_CLASS_METHODFILES.setLayout(tlayout);

		setBtnEnable();
	}

	private void setBtnEnable() {
		if (si.type.equals("system"))
			return;

		if (LIST_CLASS_METHODS.getSelectionCount() > 0) {
			BUTTON_CLASS_METHOD_DELETE.setEnabled(true);
			BUTTON_CLASS_METHOD_EDIT.setEnabled(true);
		} else {
			BUTTON_CLASS_METHOD_DELETE.setEnabled(false);
			BUTTON_CLASS_METHOD_EDIT.setEnabled(false);
		}

		if (LIST_CLASS_METHODFILES.getSelectionCount() > 0) {
			BUTTON_CLASS_METHODFILE_DELETE.setEnabled(true);
		} else {
			BUTTON_CLASS_METHODFILE_DELETE.setEnabled(false);
		}
	}
}
