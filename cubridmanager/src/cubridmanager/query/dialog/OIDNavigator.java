package cubridmanager.query.dialog;

import java.sql.Connection;
import java.sql.ResultSetMetaData;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Vector;

import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Tree;
import org.eclipse.swt.widgets.TreeItem;

import cubrid.sql.CUBRIDOID;
import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDResultSet;
import cubrid.sql.CUBRIDOID;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;

public class OIDNavigator extends Dialog {
	private static final String NEW_LINE = System.getProperty("line.separator");
	private static final String STR_NULL = cubridmanager.query.view.QueryEditor.STR_NULL;
	private Shell shlON = null; // @jve:decl-index=0:visual-constraint="10,10"
	private Composite cmpBottom;
	private Tree treeObject = null;
	private Label lblFindOID = null;
	private Text txtInput = null;
	private Button btnSearch = null;
	private Button btnClose = null;
	private Connection conn;

	private OIDNavigator(Shell parent) {
		super(parent);
	}

	public OIDNavigator(Shell parent, Connection conn) {
		this(parent);

		this.conn = conn;
	}

	public void doModal() {
		createShlON();
		CommonTool.centerShell(shlON);
		shlON.open();
		txtInput.setFocus();

		Display display = shlON.getDisplay();
		while (!shlON.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		if (MainRegistry.isProtegoBuild())
			((CUBRIDConnection) conn).Logout();
	}

	public void doModal(String strOid) {
		createShlON();
		CommonTool.centerShell(shlON);
		shlON.open();
		txtInput.setFocus();

		if (strOid.equals("NULL")) {
			shlON.close();
			return;
		}

		txtInput.setText(strOid);
		try {
			printOID(strOid);
		} catch (SQLException e) {
			CommonTool.ErrorBox(shlON, e.getErrorCode() + NEW_LINE
					+ e.getMessage());
		}

		Display display = shlON.getDisplay();
		while (!shlON.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		if (MainRegistry.isProtegoBuild())
			((CUBRIDConnection) conn).Logout();
	}

	/**
	 * This method initializes shlON
	 */
	private void createShlON() {
		shlON = new Shell(getParent(), SWT.SHELL_TRIM | SWT.APPLICATION_MODAL);
		shlON.setText(Messages.getString("QEDIT.OIDNAVIGATOR"));
		shlON.setLayout(new GridLayout());

		createTreeObject();
		createCmpBottom();

		shlON.setSize(new org.eclipse.swt.graphics.Point(380, 250));
		CommonTool.centerShell(shlON);
		shlON.addControlListener(new org.eclipse.swt.events.ControlAdapter() {
			public void controlResized(org.eclipse.swt.events.ControlEvent e) {
				adjustWindows();
			}
		});
	}

	/**
	 * This method initializes treeObject
	 * 
	 */
	private void createTreeObject() {
		GridData gridData = new GridData();
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		treeObject = new Tree(shlON, SWT.BORDER);
		treeObject.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		treeObject.setLayoutData(gridData);
	}

	/**
	 * This method initializes cmpBottom
	 * 
	 */
	private void createCmpBottom() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		gridLayout.marginWidth = 0;
		gridLayout.horizontalSpacing = 0;

		GridData gridData = new GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		cmpBottom = new Composite(shlON, SWT.NONE);
		cmpBottom.setLayoutData(gridData);
		cmpBottom.setLayout(gridLayout);

		gridData = new GridData();
		gridData.minimumWidth = 60;
		gridData.horizontalIndent = 10;
		lblFindOID = new Label(cmpBottom, SWT.CENTER);
		lblFindOID.setText(Messages.getString("QEDIT.OIDVALUE"));
		lblFindOID.setLayoutData(gridData);

		gridData = new GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalIndent = 10;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		txtInput = new Text(cmpBottom, SWT.BORDER);
		txtInput.setLayoutData(gridData);

		gridData = new GridData();
		gridData.widthHint = 66;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		btnSearch = new Button(cmpBottom, SWT.NONE);
		// TODO: image
		btnSearch.setText(Messages.getString("QEDIT.FIND"));
		btnSearch
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (txtInput.getText().trim().equals("")) {
							CommonTool.ErrorBox(shlON, Messages
									.getString("ERROR.EMPTYOID"));
							return;
						}

						for (int i = 0, n = treeObject.getItemCount(); i < n; i++) {
							if (treeObject.getItem(i).getText().equals(
									txtInput.getText().trim())) {
								CommonTool.ErrorBox(shlON, Messages
										.getString("ERROR.OIDALEADYEXIST"));
								return;
							}
						}

						try {
							printOID(txtInput.getText());
						} catch (SQLException e1) {
							CommonTool.ErrorBox(shlON, e1.getErrorCode()
									+ System.getProperty("line.separator")
									+ e1.getMessage());
							CommonTool.debugPrint(e1);
						}
					}
				});
		btnSearch.setLayoutData(gridData);

		gridData = new GridData();
		gridData.horizontalIndent = 10;
		gridData.widthHint = 66;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		btnClose = new Button(cmpBottom, SWT.NONE);
		// TODO: image
		btnClose.setText(Messages.getString("QEDIT.CLOSE"));
		btnClose.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						shlON.dispose();
					}
				});
		btnClose.setLayoutData(gridData);
		;
	}

	private void adjustWindows() {
		if (shlON.getSize().x < 360)
			shlON.setSize(360, shlON.getSize().y);
	}

	private void searchOID(String strOid, TreeItem parent, Vector oidVector)
			throws SQLException {
		CUBRIDOID oid = null;
		String tblName = null;
		String[] column;
		String[] type_name;
		String[] value;
		String[] oidSet = null;
		boolean[] isOid = null;

		int cntColumn;

		try {
			oid = CUBRIDOID.getNewInstance((CUBRIDConnection) conn, strOid);
			tblName = oid.getTableName();
		} catch (IllegalArgumentException e) {
			CommonTool.ErrorBox(shlON, Messages.getString("QEDIT.INVALIDOID"));
			parent.dispose();
			txtInput.setFocus();
			return;
		}

		if (tblName == null) {
			CommonTool.ErrorBox(shlON, Messages.getString("QEDIT.INVALIDOID"));
			parent.dispose();
			txtInput.setFocus();
			return;
		}

		parent.setText(strOid);

		TreeItem item = new TreeItem(parent, SWT.NONE);
		item.setText(Messages.getString("QEDIT.TABLE") + " "
				+ Messages.getString("QEDIT.NAME") + ": " + tblName);

		String sql = "select * from " + tblName + " where rownum = 1";
		Statement stmt;
		CUBRIDResultSet rs;
		ResultSetMetaData rsmt;

		stmt = conn.createStatement();
		rs = (CUBRIDResultSet) stmt.executeQuery(sql);
		conn.getMetaData();
		rsmt = rs.getMetaData();
		cntColumn = rsmt.getColumnCount();
		column = new String[cntColumn];
		type_name = new String[cntColumn];
		value = new String[cntColumn];

		for (int i = 0; i < cntColumn; i++) {
			column[i] = rsmt.getColumnName(i + 1);
			type_name[i] = rsmt.getColumnTypeName(i + 1);
		}

		rs.close();

		rs = (CUBRIDResultSet) oid.getValues(column);

		while (rs.next()) {
			for (int i = 0; i < column.length; i++) {
				if (rs.getObject(column[i]) != null) {
					if (type_name[i].equals("SET")
							|| type_name[i].equals("MULTISET")
							|| type_name[i].equals("SEQUENCE")) {
						Object[] set = (Object[]) rs.getCollection(column[i]);
						oidSet = new String[set.length];
						isOid = new boolean[set.length];
						value[i] = "{";
						if (set.length > 0) {
							for (int j = 0; j < set.length; j++) {
								if (set[j] instanceof CUBRIDOID) {
									value[i] += ((CUBRIDOID) set[j])
											.getOidString();
									oidSet[j] = ((CUBRIDOID) set[j])
											.getOidString();
									isOid[j] = true;
								} else {
									value[i] += set[j];
									oidSet[j] = null;
									isOid[j] = false;
								}

								if (i < set.length - 1)
									value[i] += ", ";
							}
						}
						value[i] += "}";
					} else
						value[i] = rs.getString(column[i]);
				} else
					value[i] = STR_NULL;
			}
		}
		rs.close();

		for (int i = 0; i < value.length; i++) {
			if (type_name[i].equals("CLASS") && !value[i].equals(STR_NULL)) {
				item = new TreeItem(parent, SWT.NONE);
				item.setText(column[i] + ": " + value[i]);
				if (!oidVector.contains(value[i])) {
					oidVector.add(value[i]);
					searchOID(value[i], new TreeItem(item, SWT.NONE), oidVector);
				} else
					(new TreeItem(item, SWT.NONE)).setText(Messages
							.getString("QEDIT.ALREADYOID"));
			} else if (type_name[i].equals("SET")
					|| type_name[i].equals("MULTISET")
					|| type_name[i].equals("SEQUENCE")) {
				item = new TreeItem(parent, SWT.NONE);
				item.setText(column[i] + ": " + value[i]);
				if (isOid != null) {
					for (int j = 0; j < oidSet.length; j++) {
						if (isOid[j])
							searchOID(oidSet[i], new TreeItem(item, SWT.NONE),
									oidVector);
					}
				}
			} else
				(new TreeItem(parent, SWT.NONE)).setText(column[i] + ": "
						+ value[i]);
		}
		oidVector.remove(strOid);
	}

	private void printOID(String oid) throws SQLException {
		TreeItem root = new TreeItem(treeObject, SWT.NONE);

		searchOID(oid, root, new Vector());
		if (root.isDisposed() == false)
			root.setExpanded(true);
	}
}
