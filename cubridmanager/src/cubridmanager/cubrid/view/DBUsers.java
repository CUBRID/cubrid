package cubridmanager.cubrid.view;

import java.util.ArrayList;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;

import cubridmanager.cubrid.*;
import cubridmanager.MainConstants;
import cubridmanager.Messages;
import cubridmanager.CommonTool;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.layout.GridData;

public class DBUsers extends ViewPart {

	// TODO Needs to be whatever is mentioned in plugin.xml
	public static final String ID = "workview.DBUsers";	
	public static final String RESERVED = "DBUsers.reserved"; 	
	public static final String USERS = "DBUsers.users";	
	private Composite top = null;
	private Table table = null;
	public static String Current_select = new String("");
	public static ArrayList userinfo = null;
	private UserInfo objrec = null;
	private CLabel lblUser = null;
	private Table tblUserOwnClass;

	public DBUsers() {
		super();
		if (CubridView.Current_db.length() <= 0)
			this.dispose();
		else {
			userinfo = UserInfo.UserInfo_get(CubridView.Current_db);
			// for (int i=0,n=userinfo.size(); i<n; i++) {
			// if (((UserInfo)userinfo.get(i)).userName.equals(Current_select)){
			// objrec=(UserInfo)userinfo.get(i);
			// break;
			// }
			// }
			objrec = UserInfo.UserInfo_find(userinfo, Current_select);
			if (objrec == null)
				this.dispose();
		}
	}

	public void createPartControl(Composite parent) {
		GridLayout gridLayout = new GridLayout();
		gridLayout.marginHeight = 5;
		gridLayout.marginWidth = 5;
		GridData gridData = new GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		top = new Composite(parent, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		top.setLayout(gridLayout);
		lblUser = new CLabel(top, SWT.NONE);
		lblUser.setFont(new Font(top.getDisplay(),
				lblUser.getFont().toString(), 14, SWT.BOLD));
		lblUser.setBackground(top.getBackground());
		lblUser.setLayoutData(gridData);
		lblUser.setText(Current_select);

		StringBuffer sb = new StringBuffer();
		for (int i = 0, n = objrec.groupNames.size(); i < n; i++) {
			if (i > 0)
				sb.append(", ");
			sb.append(objrec.groupNames.get(i));
		}
		lblUser = new CLabel(top, SWT.NONE);
		lblUser.setBackground(top.getBackground());
		lblUser.setText(Messages.getString("LABEL.GROUPLIST")
				+ (sb.length() < 1 ? Messages.getString("LABEL.NOTEXIST") : sb
						.toString()));

		sb = new StringBuffer();
		for (int i = 0, n = objrec.members.size(); i < n; i++) {
			if (i > 0)
				sb.append(", ");
			sb.append(((UserInfo) objrec.members.get(i)).userName);
		}
		lblUser = new CLabel(top, SWT.NONE);
		lblUser.setBackground(top.getBackground());
		lblUser.setText(Messages.getString("LABEL.MEMBERLIST")
				+ (sb.length() < 1 ? Messages.getString("LABEL.NOTEXIST") : sb
						.toString()));

		GridData gridData1 = new GridData(GridData.FILL_HORIZONTAL);
		gridData1.heightHint = 4;

		lblUser = new CLabel(top, SWT.SHADOW_IN);
		lblUser.setLayoutData(gridData1);
		// lblUser.setBackground(new Color(Display.getDefault(), 220, 220, 220));

		lblUser = new CLabel(top, SWT.NONE);
		lblUser.setBackground(top.getBackground());
		lblUser.setText(Messages.getString("LABEL.OWNCLASS"));
		createTblUserOwnClass();

		lblUser = new CLabel(top, SWT.SHADOW_IN);
		lblUser.setLayoutData(gridData1);
		// lblUser.setBackground(new Color(Display.getDefault(), 220, 220, 220));

		lblUser = new CLabel(top, SWT.NONE);
		lblUser.setBackground(top.getBackground());
		lblUser.setText(Messages.getString("LABEL.AUTHLIST"));
		createTable();
	}

	public void setFocus() {
		// TODO Auto-generated method stub

	}

	private void createTblUserOwnClass() {
		GridData gridData2 = new GridData();
		gridData2.heightHint = 150;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		ArrayList si = SchemaInfo.SchemaInfo_get(CubridView.Current_db);

		if (si.size() > 0) {
			tblUserOwnClass = new Table(top, SWT.FULL_SELECTION | SWT.BORDER);
			tblUserOwnClass.setHeaderVisible(true);
			tblUserOwnClass.setLayoutData(gridData2);

			TableColumn tblColumn = new TableColumn(tblUserOwnClass, SWT.LEFT);
			tblColumn.setText(Messages.getString("TABLE.NAME"));
			tblColumn = new TableColumn(tblUserOwnClass, SWT.LEFT);
			tblColumn.setText(Messages.getString("TABLE.SCHEMATYPE"));
			tblColumn = new TableColumn(tblUserOwnClass, SWT.LEFT);
			tblColumn.setText(Messages.getString("TABLE.VIRTUAL"));

			SchemaInfo classInfo;
			TableItem item;
			boolean color = true;
			boolean noClass = true;

			for (int i = 0, n = si.size(); i < n; i++) {
				classInfo = (SchemaInfo) si.get(i);
				if (classInfo.schemaowner.equalsIgnoreCase(Current_select)) {
					noClass = false;
					item = new TableItem(tblUserOwnClass, SWT.NONE);
					item.setText(0, classInfo.name);
					item.setText(1, classInfo.type.equals("system") ? Messages
							.getString("TREE.SYSSCHEMA") : Messages
							.getString("TREE.USERSCHEMA"));
					item.setText(2,
							classInfo.virtual.equals("normal") ? Messages
									.getString("TREE.TABLE") : Messages
									.getString("TREE.VIEW"));
					item
							.setBackground((color = !color) ? MainConstants.colorOddLine
									: MainConstants.colorEvenLine);
				}
			}

			TableLayout tlayout = new TableLayout();
			tlayout.addColumnData(new ColumnWeightData(40, true));
			tlayout.addColumnData(new ColumnWeightData(30, true));
			tlayout.addColumnData(new ColumnWeightData(30, true));
			tblUserOwnClass.setLayout(tlayout);

			if (noClass) {
				tblUserOwnClass.dispose();
				GridData gridData = new GridData();
				gridData.horizontalIndent = 20;
				CLabel lblNoClass = new CLabel(top, SWT.NONE);
				lblNoClass
						.setText(Messages.getString("LABEL.NOTEXISTOWNCLASS"));
				lblNoClass.setBackground(top.getBackground());
				lblNoClass.setLayoutData(gridData);
			}
		}
	}

	private void createTable() {
		GridData gridData1 = new GridData();
		gridData1.grabExcessHorizontalSpace = true;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.grabExcessVerticalSpace = true;
		table = new Table(top, SWT.FULL_SELECTION | SWT.BORDER);
		table.setHeaderVisible(true);
		table.setLayoutData(gridData1);

		TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.CLASS"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.SELECT"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.INSERT"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.UPDATE"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.DELETE"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.ALTER"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.INDEX1"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.EXECUTE"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTSELECT"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTINSERT"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTUPDATE"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTDELETE"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTALTER"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTINDEX"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.GRANTEXECUTE"));
		boolean color = true;

		TableItem item;
		int n = objrec.authorizations.size();
		for (int i = 0; i < n; i++) {
			Authorizations auth = (Authorizations) objrec.authorizations.get(i);
			item = new TableItem(table, SWT.NONE);
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
			item.setBackground((color = !color) ? MainConstants.colorOddLine
					: MainConstants.colorEvenLine);
		}

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(20, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(10, true));
		table.setLayout(tlayout);

		if (n < 1) {
			table.dispose();
			GridData gridData = new GridData();
			gridData.horizontalIndent = 20;
			CLabel lblNoClass = new CLabel(top, SWT.NONE);
			if (Current_select.equals("dba"))
				lblNoClass.setText(Messages.getString("LABEL.DBAHASALLAUTH"));
			else
				lblNoClass.setText(Messages.getString("LABEL.NOTEXISTOWNAUTH"));

			lblNoClass.setBackground(top.getBackground());
			lblNoClass.setLayoutData(gridData);
		}

		// for (int i = 0, n = table.getColumnCount(); i < n; i++)
		// table.getColumn(i).pack();
	}
}
