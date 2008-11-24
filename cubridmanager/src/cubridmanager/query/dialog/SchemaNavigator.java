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

package cubridmanager.query.dialog;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;

import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.custom.SashForm;
import org.eclipse.swt.dnd.DND;
import org.eclipse.swt.dnd.DragSource;
import org.eclipse.swt.dnd.DragSourceAdapter;
import org.eclipse.swt.dnd.DragSourceEvent;
import org.eclipse.swt.dnd.TextTransfer;
import org.eclipse.swt.dnd.Transfer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Tree;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.TreeItem;

import cubrid.jdbc.driver.CUBRIDResultSet;
import cubridmanager.CommonTool;
import cubridmanager.Messages;

import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Text;

public class SchemaNavigator {

	private static final String NEW_LINE = System.getProperty("line.separator");

	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="10,10"

	private SashForm splitSN = null;

	private Tree treeSN = null;

	private Button btnClose = null;

	private Rectangle rctShell = null;

	private Connection conn;

	private Text txtInfo = null;

	public SchemaNavigator(Connection conn) throws SQLException {
		this.conn = conn;
		createSShell();
		sShell.open();
	}

	/**
	 * This method initializes sShell
	 * 
	 * @throws SQLException
	 */
	private void createSShell() throws SQLException {
		sShell = new Shell();
		sShell.setText(Messages.getString("QEDIT.SCHEMANAVIGATOR"));
		createFrmSplit();
		sShell.setSize(new org.eclipse.swt.graphics.Point(400, 350));
		sShell.addControlListener(new org.eclipse.swt.events.ControlAdapter() {
			public void controlResized(org.eclipse.swt.events.ControlEvent e) {
				adjustWindows();
			}
		});

		// TODO: image
		btnClose = new Button(sShell, SWT.NONE);
		btnClose.setBounds(new org.eclipse.swt.graphics.Rectangle(320, 300, 70,
				22));
		btnClose.setText(Messages.getString("QEDIT.CLOSE"));
		btnClose
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.close();
						sShell = null;
					}
				});
	}

	/**
	 * This method initializes frmSplit
	 * 
	 * @throws SQLException
	 * 
	 */
	private void createFrmSplit() throws SQLException {
		splitSN = new SashForm(sShell, SWT.NONE);
		splitSN.SASH_WIDTH = 6;
		splitSN
				.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 0, 390,
						295));
		createTreeSN();
		txtInfo = new Text(splitSN, SWT.BORDER | SWT.MULTI | SWT.H_SCROLL
				| SWT.V_SCROLL | SWT.WRAP);
		txtInfo.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		txtInfo.setEditable(false);
		splitSN.setWeights(new int[] { 45, 55 });
	}

	/**
	 * This method initializes treeSN
	 * 
	 * @throws SQLException
	 * 
	 */
	private void createTreeSN() throws SQLException {
		treeSN = new Tree(splitSN, SWT.NONE);
		treeSN.addMouseListener(new org.eclipse.swt.events.MouseAdapter() {
			public void mouseUp(org.eclipse.swt.events.MouseEvent e) {
				try {
					makeRight();
				} catch (SQLException e1) {
					CommonTool.ErrorBox(sShell, e1.getErrorCode() + NEW_LINE
							+ e1.getMessage());
					CommonTool.debugPrint(e1);
				}
			}
		});

		// Create the drag source on the tree
		DragSource ds = new DragSource(treeSN, DND.DROP_MOVE);
		ds.setTransfer(new Transfer[] { TextTransfer.getInstance() });
		ds.addDragListener(new DragSourceAdapter() {
			public void dragSetData(DragSourceEvent event) {
				if (treeSN.getSelection()[0].getData() != null)
					// Set the data to be the first selected item's text
					event.data = treeSN.getSelection()[0].getText();
				else
					event.data = " ";
			}
		});

		TreeItem table = new TreeItem(treeSN, SWT.NONE);
		table.setText(Messages.getString("QEDIT.TABLE"));

		TreeItem userTable = new TreeItem(table, SWT.NONE);
		userTable.setText(Messages.getString("QEDIT.USER"));

		TreeItem systemTable = new TreeItem(table, SWT.NONE);
		systemTable.setText(Messages.getString("QEDIT.SYSTEM"));

		TreeItem view = new TreeItem(treeSN, SWT.NONE);
		view.setText(Messages.getString("QEDIT.VIEW"));

		TreeItem userView = new TreeItem(view, SWT.NONE);
		userView.setText(Messages.getString("QEDIT.USER"));

		TreeItem systemView = new TreeItem(view, SWT.NONE);
		systemView.setText(Messages.getString("QEDIT.SYSTEM"));

		/*
		 * TreeItem index = new TreeItem(treeSN, SWT.NONE);
		 * index.setText(Messages.getString("QEDIT.INDEX"));
		 * 
		 * TreeItem trigger = new TreeItem(treeSN, SWT.NONE);
		 * trigger.setText(Messages.getString("QEDIT.TRIGGER"));
		 */

		makeLeftTable(systemTable, "CLASS", "YES");
		makeLeftTable(userTable, "CLASS", "NO");
		makeLeftTable(systemView, "VCLASS", "YES");
		makeLeftTable(userView, "VCLASS", "NO");
		/*
		 * makeLeftIndex(index); makeLeftTrigger(trigger);
		 */
	}

	private void adjustWindows() {
		try {
			rctShell = sShell.getBounds();
			splitSN.setBounds(0, 0, rctShell.width - 10, rctShell.height - 55);
			btnClose.setBounds(rctShell.width - 80, rctShell.height - 50, 70,
					22);
		} catch (Exception e) {
			CommonTool.debugPrint(e);
		}
	}

	private void makeLeftTable(TreeItem parent, String class_type,
			String is_system_class) throws SQLException {
		String sql = "select class_name, owner_name from db_class ";
		sql += "where class_type = '" + class_type + "' ";
		sql += "and is_system_class = '" + is_system_class + "'";

		Statement stmt;
		ResultSet rs;

		stmt = conn.createStatement();
		rs = stmt.executeQuery(sql);

		while (rs.next()) {
			TreeItem item = new TreeItem(parent, SWT.NONE);
			item.setText(rs.getString("class_name"));
			item.setData(class_type + rs.getString("owner_name"));
		}

		rs.close();
	}

	/*
	 * private void makeLeftIndex(TreeItem parent) throws SQLException { String
	 * sql = "select class_name from db_index group by class_name";
	 * 
	 * Statement stmt; ResultSet rs;
	 * 
	 * stmt = conn.createStatement(); rs = stmt.executeQuery(sql);
	 * 
	 * while (rs.next()) (new TreeItem(parent,
	 * SWT.NONE)).setText(rs.getString("class_name"));
	 * 
	 * rs.close();
	 * 
	 * sql = "select index_name from db_index where class_name ="; for (int i =
	 * 0; i < parent.getItemCount(); i++) { TreeItem item = parent.getItem(i);
	 * String class_name = "'" + item.getText() + "'"; rs =
	 * stmt.executeQuery(sql + class_name); while (rs.next()) { TreeItem itemIdx =
	 * new TreeItem(item, SWT.NONE); itemIdx.setText(rs.getString(1));
	 * itemIdx.setData("index"); } }
	 * 
	 * rs.close(); }
	 * 
	 * private void makeLeftTrigger(TreeItem parent) throws SQLException {
	 * String sql = "select trigger_name from db_trig";
	 * 
	 * Statement stmt; ResultSet rs;
	 * 
	 * stmt = conn.createStatement(); rs = stmt.executeQuery(sql);
	 * 
	 * while (rs.next()) { TreeItem item = new TreeItem(parent, SWT.NONE);
	 * item.setText(rs.getString(1)); item.setData("trigger"); }
	 * 
	 * rs.close(); }
	 */
	private void makeRight() throws SQLException {
		TreeItem item = treeSN.getSelection()[0];

		if (item.getData() == null)
			return;

		txtInfo.setText("");

		String type = item.getData().toString();
		String name = item.getText();

		if (type.startsWith("CLASS"))
			showTable(name, type.substring(5));
		else if (type.startsWith("VCLASS"))
			showView(name, type.substring(6));
		// else if (type.startsWith("index"))
		// showIndex(name);
	}

	synchronized private void showTable(String name, String owner)
			throws SQLException {
		String sql;
		Statement stmt = conn.createStatement(
				ResultSet.TYPE_SCROLL_INSENSITIVE, ResultSet.CONCUR_READ_ONLY);
		CUBRIDResultSet rs;

		txtInfo.append(Messages.getString("QEDIT.NAME") + ": " + name);
		txtInfo.append(NEW_LINE);
		txtInfo.append(Messages.getString("QEDIT.OWNER") + ": " + owner);
		txtInfo.append(NEW_LINE);

		sql = "select super_class_name from db_direct_super_class where class_name = '"
				+ name + "'";
		rs = (CUBRIDResultSet) stmt.executeQuery(sql);
		while (rs.next()) {
			if (rs.isFirst())
				txtInfo.append(Messages.getString("QEDIT.SUPERCLASS") + ": ");
			txtInfo.append(rs.getString(1));
			if (!rs.isLast())
				txtInfo.append(", ");
			else
				txtInfo.append(NEW_LINE);
		}

		txtInfo.append(NEW_LINE);
		txtInfo.append(Messages.getString("QEDIT.COLUMN") + ": ");
		txtInfo.append(NEW_LINE);

		sql = "select attr_name || ' ' || "
				+ "lower(case "
				+ "when data_type = 'OBJECT' then nvl(x.domain_class_name, x.data_type) "
				+ "when data_type = 'NUMERIC' then x.data_type || '(' || x.prec || ',' || x.scale || ')' "
				+ "when data_type = 'CHAR' or data_type = 'NCHAR' or data_type = 'BIT' "
				+ "or data_type = 'STRING' or data_type = 'VARNCHAR' or data_type = 'VARBIT' "
				+ "then 'bit varying' || '(' || x.prec || ')' "
				+ "else x.data_type "
				+ "end) , "
				+ "list(select all lower(decode(y.data_type, "
				+ "'OBJECT', nvl(y.domain_class_name, y.data_type), "
				+ "'NUMERIC', y.data_type || '(' || y.prec || ',' || y.scale || ')', "
				+ "'CHAR', y.data_type || '(' || y.prec || ')', "
				+ "'NCHAR', y.data_type || '(' || y.prec || ')', "
				+ "'BIT', y.data_type || '(' || y.prec || ')', "
				+ "'STRING' , 'varchar' || '(' || y.prec || ')', "
				+ "'VARNCHAR' , 'nchar varying' || '(' || y.prec || ')', "
				+ "'VARBIT' , 'bit varying' || '(' || y.prec || ')', "
				+ "y.data_type)) "
				+ "from db_attr_setdomain_elm y "
				+ "where y.class_name = x.class_name "
				+ "and y.attr_name = x.attr_name "
				+ ") , "
				+ "nvl2(default_value, ' default ' || default_value, '') || decode(is_nullable, 'YES', ' not null', '') "
				+ "from db_attribute x " + "where class_name = '" + name + "' "
				+ "order by def_order";

		rs = (CUBRIDResultSet) stmt.executeQuery(sql);

		while (rs.next()) {
			String attr1 = rs.getString(1);
			String attr2[] = (String[]) rs.getCollection(2);
			String attr3 = rs.getString(3);

			txtInfo.append("  ");
			txtInfo.append(attr1);
			if (attr2.length > 0) {
				txtInfo.append("(");
				txtInfo.append(attr2[0]);
				for (int i = 1; i < attr2.length; i++) {
					txtInfo.append(", ");
					txtInfo.append(attr2[i]);
				}
				txtInfo.append(")");
			}

			txtInfo.append(attr3);
			if (!rs.isLast())
				txtInfo.append(",");
			txtInfo.append(NEW_LINE);
		}

		rs.close();

		getMethod(name, "INSTANCE");
		getMethod(name, "CLASS");

		sql = "select path_name || nvl2(from_class_name, ' (from ' || from_class_name || ')', '')"
				+ "from db_meth_file where class_name = '" + name + "'";
		rs = (CUBRIDResultSet) stmt.executeQuery(sql);

		while (rs.next()) {
			if (rs.isFirst()) {
				txtInfo.append(NEW_LINE);
				txtInfo.append(Messages.getString("QEDIT.METHFILE") + ": ");
				txtInfo.append(NEW_LINE);
			}
			String meth_file = rs.getString(1);

			txtInfo.append("  " + meth_file);
			txtInfo.append(NEW_LINE);
		}
		rs.close();

		sql = "select decode(x.is_reverse, 'YES', 'REVERSE ', '') || "
				+ "decode(x.is_unique, 'YES', 'UNIQUE ', '') || "
				+ "'INDEX ' || x.index_name || ' ON ' || x.class_name, "
				+ "list(select y.key_attr_name from db_index_key y "
				+ "where y.index_name = x.index_name "
				+ "and y.class_name = x.class_name " + "order by y.key_order) "
				+ "from db_index x " + "where x.class_name = '" + name + "' ";
		rs = (CUBRIDResultSet) stmt.executeQuery(sql);

		while (rs.next()) {
			if (rs.isFirst()) {
				txtInfo.append(NEW_LINE);
				txtInfo.append(Messages.getString("QEDIT.INDEX") + ": ");
				txtInfo.append(NEW_LINE);
			}
			String idx1 = rs.getString(1);
			String idx2[] = (String[]) rs.getCollection(2);

			txtInfo.append("  ");
			txtInfo.append(idx1);
			txtInfo.append("(");
			if (idx2.length > 0) {
				txtInfo.append(idx2[0]);
				for (int i = 1; i < idx2.length; i++) {
					txtInfo.append(", ");
					txtInfo.append(idx2[i]);
				}
			}
			txtInfo.append(")");
			txtInfo.append(NEW_LINE);
		}
		rs.close();
	}

	synchronized private void showView(String name, String owner)
			throws SQLException {
		showTable(name, owner);
		String sql = "select vclass_def from db_vclass where vclass_name = '"
				+ name + "'";
		Statement stmt = conn.createStatement();
		ResultSet rs = stmt.executeQuery(sql);

		txtInfo.append(NEW_LINE);
		txtInfo.append(Messages.getString("QEDIT.VIEWSPEC") + ": ");
		txtInfo.append(NEW_LINE);

		while (rs.next()) {
			txtInfo.append("  " + rs.getString(1));
		}
	}

	private void getMethod(String class_name, String meth_type)
			throws SQLException {
		String sql;
		Statement stmt = conn.createStatement(
				ResultSet.TYPE_SCROLL_INSENSITIVE, ResultSet.CONCUR_READ_ONLY);
		CUBRIDResultSet rs;

		sql = "select meth_name, "
				+ "list(select parm "
				+ "from (select all lower(decode(y.data_type, "
				+ "'OBJECT', nvl(y.domain_class_name, y.data_type), "
				+ "'NUMERIC', y.data_type || '(' || y.prec || ',' || y.scale || ')', "
				+ "'CHAR', y.data_type || '(' || y.prec || ')', "
				+ "'NCHAR', y.data_type || '(' || y.prec || ')', "
				+ "'BIT', y.data_type || '(' || y.prec || ')', "
				+ "'STRING' , 'varchar' || '(' || y.prec || ')', "
				+ "'VARNCHAR' , 'nchar varying' || '(' || y.prec || ')', "
				+ "'VARBIT' , 'bit varying' || '(' || y.prec || ')', "
				+ "y.data_type)) "
				+ "from db_meth_arg y "
				+ "where y.meth_name = x.meth_name "
				+ "and y.class_name = x.class_name "
				+ "order by y.index_of "
				+ ") z(parm)"
				+ "), "
				+ "' function ' || func_name || nvl2(from_class_name, ' (from ' || from_class_name || ')', '') "
				+ "from db_method x " + "where x.class_name = '" + class_name
				+ "'";

		rs = (CUBRIDResultSet) stmt.executeQuery(sql + " and meth_type = '"
				+ meth_type + "'");

		while (rs.next()) {
			if (rs.isFirst()) {
				txtInfo.append(NEW_LINE);
				txtInfo.append(meth_type + " "
						+ Messages.getString("QEDIT.METHOD") + ": ");
				txtInfo.append(NEW_LINE);
			}
			String meth1 = rs.getString(1);
			String meth2[] = (String[]) rs.getCollection(2);
			String meth3 = rs.getString(3);

			txtInfo.append("  " + meth1 + "(");
			if (meth2.length > 0) {
				for (int i = 1; i < meth2.length; i++) {
					txtInfo.append(meth2[i]);
					if (i != meth2.length - 1)
						txtInfo.append(", ");
				}
			}
			txtInfo.append(") ");
			if (meth2.length > 0)
				txtInfo.append(meth2[0]);

			txtInfo.append(meth3);
			txtInfo.append(NEW_LINE);
		}
		rs.close();
	}
}
