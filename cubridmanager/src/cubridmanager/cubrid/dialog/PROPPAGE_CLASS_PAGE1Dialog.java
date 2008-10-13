package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.*;
import cubridmanager.cubrid.action.TablePropertyAction;
import cubridmanager.cubrid.view.CubridView;

import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class PROPPAGE_CLASS_PAGE1Dialog extends Dialog {
	private Shell dlgShell = null;
	private Composite comparent = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Text EDIT_CLASS_NAME = null;
	private Group group1 = null;
	private Label label2 = null;
	private Text EDIT_CLASS_TYPE = null;
	private Label label3 = null;
	private Text txtClassOwner = null;
	private Button BUTTON_CLASS_OWNER = null;
	private Label label4 = null;
	private Text EDIT_CLASS_VIRTUALCLASS = null;
	private Label label5 = null;
	private Table LIST_CLASS_ATTRIBUTES = null;
	private Button BUTTON_CLASS_ATTRIBUTE_ADD = null;
	private Button BUTTON_CLASS_ATTRIBUTE_DELETE = null;
	private Button BUTTON_CLASS_ATTRIBUTE_EDIT = null;
	private Label label6 = null;
	private Table LIST_CLASS_CONSTRAINTS = null;
	private Button BUTTON_CLASS_CONSTRAINT_ADD = null;
	private Button BUTTON_CLASS_CONSTRAINT_DELETE = null;
	// public static TableItem CurrentLIST_CLASS_ATTRIBUTES=null;
	// public static TableItem CurrentLIST_CLASS_CONSTRAINTS=null;
	public static SchemaInfo si = null;
	private Label label7 = null;
	private Label label8 = null;

	public PROPPAGE_CLASS_PAGE1Dialog(Shell parent) {
		super(parent);
	}

	public PROPPAGE_CLASS_PAGE1Dialog(Shell parent, int style) {
		super(parent, style);
	}

	public Composite SetTabPart(TabFolder parent) {
		comparent = parent;
		createSShell();
		sShell.getShell().addShellListener(
				new org.eclipse.swt.events.ShellAdapter() {
					public void shellActivated(
							org.eclipse.swt.events.ShellEvent e) {
						setinfo();
					}
				});
		sShell.setParent(parent);
		return sShell;
	}

	public int doModal() {
		createDlgShell();
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createDlgShell() {
		dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell
				.setText(Messages.getString("TITLE.PROPPAGE_CLASS_PAGE1DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createSShell();
	}

	private void createSShell() {
		GridData gridData55 = new org.eclipse.swt.layout.GridData();
		gridData55.widthHint = 90;
		GridData gridData54 = new org.eclipse.swt.layout.GridData();
		gridData54.widthHint = 90;
		GridData gridData53 = new org.eclipse.swt.layout.GridData();
		gridData53.horizontalSpan = 4;
		gridData53.grabExcessHorizontalSpace = true;
		GridData gridData52 = new org.eclipse.swt.layout.GridData();
		gridData52.horizontalSpan = 6;
		GridData gridData51 = new org.eclipse.swt.layout.GridData();
		gridData51.widthHint = 90;
		GridData gridData50 = new org.eclipse.swt.layout.GridData();
		gridData50.widthHint = 90;
		GridData gridData49 = new org.eclipse.swt.layout.GridData();
		gridData49.widthHint = 90;
		GridData gridData48 = new org.eclipse.swt.layout.GridData();
		gridData48.horizontalSpan = 3;
		gridData48.grabExcessHorizontalSpace = true;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 6;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
		sShell.setLayout(gridLayout);

		createGroup1();

		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 2;
		label5 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.ATTRIBUTES"));
		label5.setLayoutData(gridData2);

		createLIST_CLASS_ATTRIBUTES();

		label7 = new Label(sShell, SWT.NONE);
		label7.setLayoutData(gridData48);
		BUTTON_CLASS_ATTRIBUTE_ADD = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_ATTRIBUTE_ADD.setText(Messages.getString("BUTTON.ADD1"));
		BUTTON_CLASS_ATTRIBUTE_ADD.setLayoutData(gridData49);
		BUTTON_CLASS_ATTRIBUTE_ADD
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						EditAttributeDialog dlg = new EditAttributeDialog(
								sShell.getShell());
						dlg.setText(Messages
								.getString("TITLE.ADD_ATTRIBUTEDIALOG"));
						if (dlg.doModal()) {
							setinfo();
						}
					}
				});
		BUTTON_CLASS_ATTRIBUTE_EDIT = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_ATTRIBUTE_EDIT.setText(Messages.getString("BUTTON.EDIT"));
		BUTTON_CLASS_ATTRIBUTE_EDIT.setLayoutData(gridData51);
		BUTTON_CLASS_ATTRIBUTE_EDIT
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (LIST_CLASS_ATTRIBUTES.getSelectionCount() < 1)
							return;
						EditAttributeDialog dlg = new EditAttributeDialog(
								sShell.getShell());
						dlg.setText(Messages
								.getString("TITLE.EDIT_ATTRIBUTEDIALOG"));
						if (dlg
								.doModal(LIST_CLASS_ATTRIBUTES.getSelection()[0])) {
							setinfo();
						}
					}
				});
		BUTTON_CLASS_ATTRIBUTE_DELETE = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_ATTRIBUTE_DELETE.setText(Messages
				.getString("BUTTON.DELETE"));
		BUTTON_CLASS_ATTRIBUTE_DELETE.setLayoutData(gridData50);
		BUTTON_CLASS_ATTRIBUTE_DELETE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (LIST_CLASS_ATTRIBUTES.getSelectionCount() < 1)
							return;
						String colClassAttribute = LIST_CLASS_ATTRIBUTES
								.getSelection()[0].getText(8);
						String colAttributeName = LIST_CLASS_ATTRIBUTES
								.getSelection()[0].getText(0);
						if (CommonTool.WarnYesNo(sShell.getShell(), Messages
								.getString("WARNYESNO.DELETE")
								+ " " + colAttributeName) != SWT.YES)
							return;
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "classname:" + si.name + "\n";
						msg += "attributename:" + colAttributeName + "\n";
						msg += "category:";
						if (colClassAttribute.length() > 0)
							msg += "class";
						else
							msg += "instance";

						ClientSocket cs = new ClientSocket();
						if (!cs.SendBackGround(sShell.getShell(), msg,
								"dropattribute", Messages
										.getString("WAITING.DROPATTR"))) {
							CommonTool.ErrorBox(sShell.getShell(), cs.ErrorMsg);
							return;
						}
						setinfo();
					}
				});

		label6 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.INDICES"));
		label6.setLayoutData(gridData52);
		createLIST_CLASS_CONSTRAINTS();
		label8 = new Label(sShell, SWT.NONE);
		label8.setLayoutData(gridData53);
		BUTTON_CLASS_CONSTRAINT_ADD = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_CONSTRAINT_ADD.setText(Messages.getString("BUTTON.ADD1"));
		BUTTON_CLASS_CONSTRAINT_ADD.setLayoutData(gridData54);
		BUTTON_CLASS_CONSTRAINT_ADD
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ADD_CONSTRAINTDialog dlg = new ADD_CONSTRAINTDialog(
								sShell.getShell());
						if (dlg.doModal()) {
							setinfo();
						}
					}
				});
		BUTTON_CLASS_CONSTRAINT_DELETE = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_CONSTRAINT_DELETE.setText(Messages
				.getString("BUTTON.DELETE"));
		BUTTON_CLASS_CONSTRAINT_DELETE.setLayoutData(gridData55);
		BUTTON_CLASS_CONSTRAINT_DELETE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (LIST_CLASS_CONSTRAINTS.getSelectionCount() < 1)
							return;
						TableItem item = LIST_CLASS_CONSTRAINTS.getSelection()[0];
						String col0 = item.getText(0);
						String col1 = item.getText(1);
						String col2 = item.getText(2);
						String col3 = item.getText(3);
						if (CommonTool.WarnYesNo(sShell.getShell(), Messages
								.getString("WARNYESNO.DELETE")
								+ " " + col0) != SWT.YES)
							return;
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "classname:" + si.name + "\n";
						msg += "type:" + col1 + "\n";
						msg += "name:" + col0 + "\n";
						String[] attribute = null;
						String category = "";
						if (col2.length() > 0) {
							attribute = col2.split(", ");
							category = "class";
						} else {
							attribute = col3.split(", ");
							category = "instance";
						}
						msg += "attributecount:" + attribute.length + "\n";
						for (int i = 0; i < attribute.length; i++)
							msg += "attribute:" + attribute[i] + "\n";
						msg += "category:" + category + "\n";
						ClientSocket cs = new ClientSocket();

						if (!cs.SendBackGround(sShell.getShell(), msg,
								"dropconstraint", Messages
										.getString("WAITING.DROPCONSTRAINT"))) {
							CommonTool.ErrorBox(sShell.getShell(), cs.ErrorMsg);
							return;
						}
						setinfo();
					}
				});
		sShell.pack();
	}

	private void createGroup1() {
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 6;
		gridData1.grabExcessHorizontalSpace = true;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.grabExcessVerticalSpace = true;
		GridLayout gridLayout56 = new GridLayout();
		gridLayout56.numColumns = 3;
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayoutData(gridData1);
		group1.setLayout(gridLayout56);

		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.grabExcessHorizontalSpace = true;
		gridData.widthHint = 80;
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.NAME"));
		EDIT_CLASS_NAME = new Text(group1, SWT.NONE);
		EDIT_CLASS_NAME.setEditable(false);
		EDIT_CLASS_NAME.setLayoutData(gridData);

		GridData gridData57 = new org.eclipse.swt.layout.GridData();
		gridData57.widthHint = 80;
		gridData57.horizontalSpan = 2;
		gridData57.grabExcessHorizontalSpace = true;
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.SCHEMATYPE"));
		EDIT_CLASS_TYPE = new Text(group1, SWT.NONE);
		EDIT_CLASS_TYPE.setEditable(false);
		EDIT_CLASS_TYPE.setLayoutData(gridData57);

		GridData gridData58 = new org.eclipse.swt.layout.GridData();
		gridData58.widthHint = 80;
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.OWNER"));
		txtClassOwner = new Text(group1, SWT.NONE);
		txtClassOwner.setEditable(false);
		txtClassOwner.setLayoutData(gridData58);
		GridData gridBUTTON_CLASS_OWNER = new GridData();
		gridBUTTON_CLASS_OWNER.grabExcessHorizontalSpace = true;
		gridBUTTON_CLASS_OWNER.horizontalAlignment = GridData.BEGINNING;
		BUTTON_CLASS_OWNER = new Button(group1, SWT.NONE);
		BUTTON_CLASS_OWNER.setText(Messages.getString("BUTTON.CHANGE"));
		BUTTON_CLASS_OWNER.setLayoutData(gridBUTTON_CLASS_OWNER);
		BUTTON_CLASS_OWNER
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						CHANGE_OWNERDialog dlg = new CHANGE_OWNERDialog(sShell
								.getShell());
						if (dlg.doModal()) {
							setinfo();
						}
					}
				});

		GridData gridData59 = new org.eclipse.swt.layout.GridData();
		gridData59.horizontalSpan = 2;
		gridData59.grabExcessHorizontalSpace = true;
		gridData59.widthHint = 80;
		label4 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.TYPE"));
		EDIT_CLASS_VIRTUALCLASS = new Text(group1, SWT.NONE);
		EDIT_CLASS_VIRTUALCLASS.setEditable(false);
		EDIT_CLASS_VIRTUALCLASS.setLayoutData(gridData59);
	}

	private void createLIST_CLASS_ATTRIBUTES() {
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalSpan = 6;
		gridData3.grabExcessVerticalSpace = true;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.heightHint = 160;
		gridData3.widthHint = 550;
		gridData3.grabExcessHorizontalSpace = true;
		LIST_CLASS_ATTRIBUTES = new Table(sShell, SWT.FULL_SELECTION
				| SWT.SINGLE | SWT.BORDER);
		LIST_CLASS_ATTRIBUTES.setLinesVisible(true);
		LIST_CLASS_ATTRIBUTES.setLayoutData(gridData3);
		LIST_CLASS_ATTRIBUTES.setHeaderVisible(true);

		TableColumn tblcol;
		tblcol = new TableColumn(LIST_CLASS_ATTRIBUTES, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NAME"));
		tblcol = new TableColumn(LIST_CLASS_ATTRIBUTES, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.DOMAIN"));
		tblcol = new TableColumn(LIST_CLASS_ATTRIBUTES, SWT.CENTER);
		tblcol.setText(Messages.getString("TABLE.ISINDEXED"));
		tblcol.setWidth(0);
		tblcol.setResizable(false);
		tblcol = new TableColumn(LIST_CLASS_ATTRIBUTES, SWT.CENTER);
		tblcol.setText("NOT NULL");
		tblcol = new TableColumn(LIST_CLASS_ATTRIBUTES, SWT.CENTER);
		tblcol.setText("SHARED");
		tblcol = new TableColumn(LIST_CLASS_ATTRIBUTES, SWT.CENTER);
		tblcol.setText("UNIQUE");
		tblcol = new TableColumn(LIST_CLASS_ATTRIBUTES, SWT.LEFT);
		tblcol.setText("DEFAULT");
		tblcol = new TableColumn(LIST_CLASS_ATTRIBUTES, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.INHERITANCE"));
		tblcol = new TableColumn(LIST_CLASS_ATTRIBUTES, SWT.CENTER);
		tblcol.setText(Messages.getString("TABLE.ISCLASS").concat(
				Messages.getString("TABLE.ATTRIBUTE")));

		LIST_CLASS_ATTRIBUTES.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setBtnEnable();
			}
		});
	}

	private void createLIST_CLASS_CONSTRAINTS() {
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalSpan = 6;
		gridData4.grabExcessVerticalSpace = true;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData4.heightHint = 80;
		gridData4.widthHint = 550;
		gridData4.grabExcessHorizontalSpace = true;
		LIST_CLASS_CONSTRAINTS = new Table(sShell, SWT.FULL_SELECTION
				| SWT.SINGLE | SWT.BORDER);
		LIST_CLASS_CONSTRAINTS.setLinesVisible(true);
		LIST_CLASS_CONSTRAINTS.setLayoutData(gridData4);
		LIST_CLASS_CONSTRAINTS.setHeaderVisible(true);

		TableColumn tblcol = new TableColumn(LIST_CLASS_CONSTRAINTS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NAME"));
		tblcol = new TableColumn(LIST_CLASS_CONSTRAINTS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.INDEXTYPE"));
		tblcol = new TableColumn(LIST_CLASS_CONSTRAINTS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.CLASSATTRIBUTES"));
		tblcol.setWidth(0);
		tblcol.setResizable(false);
		tblcol = new TableColumn(LIST_CLASS_CONSTRAINTS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.ATTRIBUTE"));
		tblcol = new TableColumn(LIST_CLASS_CONSTRAINTS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.RULE"));
		LIST_CLASS_CONSTRAINTS.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setBtnEnable();
			}
		});
	}

	private void setinfo() {
		si = TablePropertyAction.si;
		EDIT_CLASS_NAME.setToolTipText(Messages
				.getString("TOOLTIP.CLASSEDITCLASSNAME"));
		EDIT_CLASS_TYPE.setToolTipText(Messages
				.getString("TOOLTIP.CLASSEDITTYPE"));
		BUTTON_CLASS_OWNER.setToolTipText(Messages
				.getString("TOOLTIP.CLASSEDITOWNER"));
		EDIT_CLASS_VIRTUALCLASS.setToolTipText(Messages
				.getString("TOOLTIP.CLASSEDITVIRTUALCLASS"));
		LIST_CLASS_ATTRIBUTES.setToolTipText(Messages
				.getString("TOOLTIP.CLASSLISTATTRIBUTES"));
		LIST_CLASS_CONSTRAINTS.setToolTipText(Messages
				.getString("TOOLTIP.CLASSLISTCONSTRAINTS"));

		EDIT_CLASS_NAME.setText(si.name);
		EDIT_CLASS_TYPE.setText(si.type.equals("system") ? Messages
				.getString("TREE.SYSSCHEMA") : Messages
				.getString("TREE.USERSCHEMA"));
		txtClassOwner.setText(si.schemaowner);
		EDIT_CLASS_VIRTUALCLASS.setText(si.virtual.equals("normal") ? Messages
				.getString("TREE.TABLE") : Messages.getString("TREE.VIEW"));
		LIST_CLASS_ATTRIBUTES.removeAll();

		BUTTON_CLASS_OWNER.setEnabled(MainRegistry
				.Authinfo_find(CubridView.Current_db).isDBAGroup);

		// class attributes
		for (int i = 0, n = si.classAttributes.size(); i < n; i++) {
			DBAttribute da = (DBAttribute) si.classAttributes.get(i);
			TableItem item = new TableItem(LIST_CLASS_ATTRIBUTES, SWT.NONE);
			item.setText(0, da.name);
			item.setText(1, da.type);
			item.setText(2, CommonTool.BooleanO(da.isIndexed));
			item.setText(3, CommonTool.BooleanO(da.isNotNull));
			item.setText(4, CommonTool.BooleanO(da.isShared));
			item.setText(5, CommonTool.BooleanO(da.isUnique));
			item.setText(6, da.defaultval);
			item.setText(7, si.name.equals(da.inherit) ? "" : da.inherit);
			item.setText(8, CommonTool.BooleanO(true));
		}
		for (int i = 0, n = si.attributes.size(); i < n; i++) {
			DBAttribute da = (DBAttribute) si.attributes.get(i);
			TableItem item = new TableItem(LIST_CLASS_ATTRIBUTES, SWT.NONE);
			item.setText(0, da.name);
			item.setText(1, da.type);
			item.setText(2, CommonTool.BooleanO(da.isIndexed));
			item.setText(3, CommonTool.BooleanO(da.isNotNull));
			item.setText(4, CommonTool.BooleanO(da.isShared));
			item.setText(5, CommonTool.BooleanO(da.isUnique));
			item.setText(6, da.defaultval);
			item.setText(7, si.name.equals(da.inherit) ? "" : da.inherit);
			item.setText(8, CommonTool.BooleanO(false));
		}

		// constraints
		LIST_CLASS_CONSTRAINTS.removeAll();
		for (int i = 0, n = si.constraints.size(); i < n; i++) {
			Constraint rec = (Constraint) si.constraints.get(i);
			if (!rec.type.equals("NOT NULL")) {
				TableItem item = new TableItem(LIST_CLASS_CONSTRAINTS, SWT.NONE);
				item.setText(0, rec.name);
				item.setText(1, rec.type);
				item.setText(2, CommonTool.ArrayToString(rec.classAttributes));
				item.setText(3, CommonTool.ArrayToString(rec.attributes));
				item.setText(4, CommonTool.ArrayToString(rec.rule));
			}
		}

		// system class
		if (si.type.equals("system")) {
			BUTTON_CLASS_ATTRIBUTE_ADD.setEnabled(false);
			BUTTON_CLASS_ATTRIBUTE_DELETE.setEnabled(false);
			BUTTON_CLASS_ATTRIBUTE_EDIT.setEnabled(false);
			BUTTON_CLASS_CONSTRAINT_ADD.setEnabled(false);
			BUTTON_CLASS_CONSTRAINT_DELETE.setEnabled(false);
			BUTTON_CLASS_OWNER.setEnabled(false);
		}

		if (si.virtual.equals("view")) {
			BUTTON_CLASS_CONSTRAINT_ADD.setEnabled(false);
			BUTTON_CLASS_CONSTRAINT_DELETE.setEnabled(false);
		}

		for (int i = 0, n = LIST_CLASS_ATTRIBUTES.getColumnCount(); i < n; i++) {
			if (i != 2)
				LIST_CLASS_ATTRIBUTES.getColumn(i).pack();
		}

		LIST_CLASS_CONSTRAINTS.getColumn(0).pack();
		LIST_CLASS_CONSTRAINTS.getColumn(1).pack();
		LIST_CLASS_CONSTRAINTS.getColumn(3).pack();
		LIST_CLASS_CONSTRAINTS.getColumn(4).pack();

		setBtnEnable();
	}

	private void setBtnEnable() {
		if (LIST_CLASS_ATTRIBUTES.getSelectionCount() > 0) {
			BUTTON_CLASS_ATTRIBUTE_EDIT.setEnabled(true);
			BUTTON_CLASS_ATTRIBUTE_DELETE.setEnabled(true);
		} else {
			BUTTON_CLASS_ATTRIBUTE_EDIT.setEnabled(false);
			BUTTON_CLASS_ATTRIBUTE_DELETE.setEnabled(false);
		}

		if (LIST_CLASS_CONSTRAINTS.getSelectionCount() > 0)
			BUTTON_CLASS_CONSTRAINT_DELETE.setEnabled(true);
		else
			BUTTON_CLASS_CONSTRAINT_DELETE.setEnabled(false);
	}
}