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

package cubridmanager.cubrid.view;

import java.util.ArrayList;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.part.ViewPart;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.ITreeObjectChangedListener;
import cubridmanager.MainConstants;
import cubridmanager.Messages;
import cubridmanager.cubrid.Constraint;
import cubridmanager.cubrid.DBAttribute;
import cubridmanager.cubrid.SchemaInfo;

public class DBSchema extends ViewPart implements ITreeObjectChangedListener {
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
			Label label = new Label(top, SWT.LEFT | SWT.WRAP);
			label.setText(Messages.getString("LABEL.ATTRIBUTES1"));
			label.setBackground(Display.getCurrent().getSystemColor(
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
				createTextViewSpec();
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

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(5, 80, true));
		tlayout.addColumnData(new ColumnWeightData(95, true));
		table.setLayout(tlayout);
		fillTable();
	}

	private void fillTable() {
		if (objrec == null || table == null || table.isDisposed())
			return;
		table.removeAll();
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
		fillTable2();
	}

	private void fillTable2() {
		if (objrec == null || table2 == null || table2.isDisposed())
			return;
		table2.removeAll();
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

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(20, true));
		tlayout.addColumnData(new ColumnWeightData(20, true));
		tlayout.addColumnData(new ColumnWeightData(20, true));
		tlayout.addColumnData(new ColumnWeightData(60, true));
		table3.setLayout(tlayout);
		fillTable3();
	}

	private void fillTable3() {
		if (objrec == null || table3 == null || table3.isDisposed())
			return;
		table3.removeAll();
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

	}

	private void createTextViewSpec() {
		txtViewSpec = new Text(top, SWT.WRAP | SWT.BORDER);
		txtViewSpec.setLayoutData(new GridData(GridData.FILL_BOTH));
		txtViewSpec.setEditable(false);
		txtViewSpec.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		fillTextViewSpec();
	}

	private void fillTextViewSpec() {
		if (txtViewSpec == null || txtViewSpec.isDisposed())
			return;
		txtViewSpec.setText("");
		for (int i = 0, n = objrec.querySpecs.size(); i < n; i++) {
			txtViewSpec.append((String) objrec.querySpecs.get(i));
			txtViewSpec.append(MainConstants.NEW_LINE);
		}
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

	public void refresh() {
		if (CubridView.Current_db.length() <= 0)
			return;
		else {
			schemainfo = SchemaInfo.SchemaInfo_get(CubridView.Current_db);
			for (int i = 0, n = schemainfo.size(); i < n; i++) {
				if (((SchemaInfo) schemainfo.get(i)).name.equals(CurrentObj)) {
					objrec = (SchemaInfo) schemainfo.get(i);
					break;
				}
			}
			if (objrec == null)
				return;
		}
		getclassinfo();
		if (label1 != null && !label1.isDisposed())
			label1.setText(objrec.name);
		fillTable();
		fillTable2();
		if (objrec.virtual.equals("normal")) {
			if (label2 != null && !label2.isDisposed())
				label2.setText(Messages.getString("LABEL.INDICES"));
			if (table3 == null || table3.isDisposed()) {
				if (txtViewSpec != null && !txtViewSpec.isDisposed()) {
					txtViewSpec.dispose();
					txtViewSpec = null;
				}
				createTable3();
			} else {
				fillTable3();
			}
		} else if (objrec.virtual.equals("view")) {
			if (label2 != null && !label2.isDisposed())
				label2.setText(Messages.getString("TITLE.ADD_QUERYDIALOG"));
			if (txtViewSpec == null || txtViewSpec.isDisposed()) {
				if (table3 != null && !table3.isDisposed()) {
					table3.dispose();
					table3 = null;
				}
				createTextViewSpec();
			} else {
				fillTextViewSpec();
			}
		}
		top.layout(true);
	}
}
