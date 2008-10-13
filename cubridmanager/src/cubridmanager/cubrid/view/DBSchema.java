package cubridmanager.cubrid.view;

import java.util.ArrayList;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import cubridmanager.cubrid.SchemaInfo;
import cubridmanager.cubrid.DBAttribute;
import cubridmanager.cubrid.Constraint;
//import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
//import cubridmanager.ErrorPage;
import cubridmanager.MainConstants;
import cubridmanager.Messages;

import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;

public class DBSchema extends ViewPart {
	public static final String ID = "workview.DBSchema";
	// TODO Needs to be whatever is mentioned in plugin.xml
	public static final String SYS_SCHEMA = "SYS_SCHEMA";
	public static final String SYS_TABLE = "SYS_TABLE";
	public static final String SYS_VIEW = "SYS_VIEW";
	public static final String USER_SCHEMA = "USER_SCHEMA";
	public static final String USER_TABLE = "USER_TABLE";
	public static final String USER_VIEW = "USER_VIEW";
	public static final String SYS_OBJECT = "SYS_OBJECT";
	public static final String USER_OBJECT = "USER_OBJECT";
	public static String CurrentSelect = new String("");
	public static String CurrentObj = new String("");
	public static ArrayList schemainfo = null;
	private SchemaInfo objrec = null;
	private Composite top = null;
	private Table table = null;
	private Table table2 = null;
	private Table table3 = null;
	private Label label1 = null;
	private Label label2 = null;
	private Text txtViewSpec = null;

	public DBSchema() {
		super();
		if (CubridView.Current_db.length() <= 0)
			this.dispose();
		else {
			schemainfo = SchemaInfo.SchemaInfo_get(CubridView.Current_db);
			for (int i = 0, n = schemainfo.size(); i < n; i++) {
				if (((SchemaInfo) schemainfo.get(i)).name.equals(CurrentObj)) {
					objrec = (SchemaInfo) schemainfo.get(i);
					break;
				}
			}
			if (objrec == null)
				this.dispose();
		}
	}

	public void createPartControl(Composite parent) {
		// TODO Auto-generated method stub
		GridLayout gridLayout = new GridLayout();
		gridLayout.marginWidth = 0;
		gridLayout.marginHeight = 0;
		top = new Composite(parent, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		top.setLayout(gridLayout);

		getclassinfo();
		// try {
		if (objrec != null) {
			label1 = new Label(top, SWT.LEFT | SWT.WRAP);
			label1.setText(objrec.name);
			label1.setFont(new Font(top.getDisplay(), label1.getFont()
					.toString(), 14, SWT.BOLD));
			label1.setBackground(Display.getCurrent().getSystemColor(
					SWT.COLOR_WHITE));
			createTable();
			label1 = new Label(top, SWT.LEFT | SWT.WRAP);
			label1.setText(Messages.getString("LABEL.ATTRIBUTES1"));
			label1.setBackground(Display.getCurrent().getSystemColor(
					SWT.COLOR_WHITE));
			createTable2();

			label2 = new Label(top, SWT.LEFT | SWT.WRAP);
			label2.setBackground(Display.getCurrent().getSystemColor(
					SWT.COLOR_WHITE));
			if (objrec.virtual.equals("normal")) {
				label2.setText(Messages.getString("LABEL.INDICES"));
				createTable3();
			} else if (objrec.virtual.equals("view")) {
				label2.setText(Messages.getString("TITLE.ADD_QUERYDIALOG"));
				txtViewSpec = new Text(top, SWT.WRAP | SWT.BORDER);
				txtViewSpec.setLayoutData(new GridData(GridData.FILL_BOTH));
				txtViewSpec.setEditable(false);
				txtViewSpec.setBackground(Display.getCurrent().getSystemColor(
						SWT.COLOR_WHITE));
				for (int i = 0, n = objrec.querySpecs.size(); i < n; i++) {
					txtViewSpec.append((String) objrec.querySpecs.get(i));
					txtViewSpec.append(MainConstants.NEW_LINE);
				}
			}
		}
		// }
		// catch (Exception e) {
		// ErrorPage.setErrorPage(top);
		// }
	}

	public void setFocus() {
		// TODO Auto-generated method stub

	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		GridData gridData = new GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		table = new Table(top, SWT.FULL_SELECTION);
		table.setLayoutData(gridData);
		new TableColumn(table, SWT.LEFT);
		new TableColumn(table, SWT.LEFT);

		if (objrec == null)
			return;

		TableItem item;
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.SCHEMATYPE"));
		item.setText(1, objrec.type.equals("system") ? Messages
				.getString("TREE.SYSSCHEMA") : Messages
				.getString("TREE.USERSCHEMA"));
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.OWNER"));
		item.setText(1, objrec.schemaowner);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.SUPERCLASS"));
		String superstr = new String("");
		for (int si = 0, sn = objrec.superClasses.size(); si < sn; si++) {
			if (si > 0)
				superstr = superstr.concat(", ");
			superstr = superstr.concat((String) objrec.superClasses.get(si));
		}
		item.setText(1, superstr);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.VIRTUAL"));
		item.setText(1, objrec.virtual.equals("normal") ? Messages
				.getString("TREE.TABLE") : Messages.getString("TREE.VIEW"));
		setPartName(item.getText(1).concat(
				Messages.getString("STRING.INFORMATION")));

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(5, 80, true));
		tlayout.addColumnData(new ColumnWeightData(95, true));
		table.setLayout(tlayout);
	}

	private void createTable2() {
		GridData gridData1 = new GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.grabExcessVerticalSpace = true;
		table2 = new Table(top, SWT.BORDER | SWT.FULL_SELECTION);
		table2.setHeaderVisible(true);
		table2.setLayoutData(gridData1);
		table2.setLinesVisible(true);

		TableColumn tblColumn = new TableColumn(table2, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.NAME"));
		tblColumn = new TableColumn(table2, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.DOMAIN"));
		tblColumn = new TableColumn(table2, SWT.CENTER);
		tblColumn.setText(Messages.getString("TABLE.ISINDEXED"));
		tblColumn.setResizable(false);
		tblColumn = new TableColumn(table2, SWT.CENTER);
		tblColumn.setText("NOT NULL");
		tblColumn = new TableColumn(table2, SWT.CENTER);
		tblColumn.setText("SHARED");
		tblColumn = new TableColumn(table2, SWT.CENTER);
		tblColumn.setText("UNIQUE");
		tblColumn = new TableColumn(table2, SWT.LEFT);
		tblColumn.setText("DEFAULT");
		tblColumn = new TableColumn(table2, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.INHERITANCE"));

		if (objrec == null)
			return;

		boolean color = false;
		TableItem item;
		for (int i = 0, n = objrec.classAttributes.size(); i < n; i++) {
			DBAttribute da = (DBAttribute) objrec.classAttributes.get(i);
			item = new TableItem(table2, SWT.NONE);
			item.setText(0, da.name);
			item.setText(1, da.type);
			item.setText(2, CommonTool.BooleanO(da.isIndexed));
			item.setText(3, CommonTool.BooleanO(da.isNotNull));
			item.setText(4, CommonTool.BooleanO(da.isShared));
			item.setText(5, CommonTool.BooleanO(da.isUnique));
			item.setText(6, da.defaultval);
			item.setText(7, objrec.name.equals(da.inherit) ? "" : da.inherit);
			if (color = !color)
				item.setBackground(MainConstants.colorOddLine);
			else
				item.setBackground(MainConstants.colorEvenLine);
		}
		for (int i = 0, n = objrec.attributes.size(); i < n; i++) {
			DBAttribute da = (DBAttribute) objrec.attributes.get(i);
			item = new TableItem(table2, SWT.NONE);
			item.setText(0, da.name);
			item.setText(1, da.type);
			item.setText(2, CommonTool.BooleanO(da.isIndexed));
			item.setText(3, CommonTool.BooleanO(da.isNotNull));
			item.setText(4, CommonTool.BooleanO(da.isShared));
			item.setText(5, CommonTool.BooleanO(da.isUnique));
			item.setText(6, da.defaultval);
			item.setText(7, objrec.name.equals(da.inherit) ? "" : da.inherit);
			if (color = !color)
				item.setBackground(MainConstants.colorOddLine);
			else
				item.setBackground(MainConstants.colorEvenLine);
		}

		table2.getColumn(1).pack();

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(20, true));
		tlayout.addColumnData(new ColumnWeightData(20, true));
		tlayout.addColumnData(new ColumnWeightData(0, 0, false));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(15, true));
		tlayout.addColumnData(new ColumnWeightData(15, true));
		table2.setLayout(tlayout);
	}

	private void createTable3() {
		GridData gridData2 = new GridData(GridData.FILL_BOTH);
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		table3 = new Table(top, SWT.BORDER | SWT.FULL_SELECTION);
		table3.setHeaderVisible(true);
		table3.setLayoutData(gridData2);
		table3.setLinesVisible(true);

		TableColumn tblColumn = new TableColumn(table3, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.NAME"));
		tblColumn = new TableColumn(table3, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.INDEXTYPE"));
		tblColumn = new TableColumn(table3, SWT.LEFT);
		tblColumn.setText(Messages.getString("LABEL.ATTRIBUTES1"));
		tblColumn = new TableColumn(table3, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.RULE"));

		if (objrec == null)
			return;

		boolean color = false;
		TableItem item;
		for (int i = 0, n = objrec.constraints.size(); i < n; i++) {
			Constraint cr = (Constraint) objrec.constraints.get(i);
			if (!cr.type.equals("NOT NULL")) {
				item = new TableItem(table3, SWT.NONE);
				item.setText(0, cr.name);
				item.setText(1, cr.type);
				item.setText(2, CommonTool.ArrayToString(cr.attributes));
				item.setText(3, CommonTool.ArrayToString(cr.rule));
				if (color = !color)
					item.setBackground(MainConstants.colorOddLine);
				else
					item.setBackground(MainConstants.colorEvenLine);
			}
		}

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(20, true));
		tlayout.addColumnData(new ColumnWeightData(20, true));
		tlayout.addColumnData(new ColumnWeightData(20, true));
		tlayout.addColumnData(new ColumnWeightData(60, true));
		table3.setLayout(tlayout);
	}

	private void getclassinfo() {
		try {
			ClientSocket cs = new ClientSocket();
			if (!cs.SendClientMessage(top.getShell(), "dbname:"
					+ CubridView.Current_db + "\nclassname:" + objrec.name,
					"class")) {
				CommonTool.ErrorBox(top.getShell(), cs.ErrorMsg);
				objrec = null;
			}
		} catch (Exception e) {
			schemainfo = SchemaInfo.SchemaInfo_get(CubridView.Current_db);
			for (int i = 0, n = schemainfo.size(); i < n; i++) {
				if (((SchemaInfo) schemainfo.get(i)).name.equals(CurrentObj)) {
					objrec = (SchemaInfo) schemainfo.get(i);
					break;
				}
			}
		}
	}
}
