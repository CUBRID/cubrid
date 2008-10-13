package cubridmanager.cubrid.view;

import java.util.ArrayList;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;

import cubridmanager.cubrid.LogFileInfo;
import cubridmanager.Messages;

public class DBLogs extends ViewPart {

	public static final String ID = "workview.DBLogs";
	public static final String OBJ = "DBLogsObj";
	private Composite top = null;
	private Table table = null;
	public static ArrayList DBLoginfo = null;
	public static String Current_select = new String("");
	private LogFileInfo fileinfo = null;

	public DBLogs() {
		super();
		if (CubridView.Current_db.length() <= 0)
			this.dispose();
		else {
			DBLoginfo = LogFileInfo.DBLogInfo_get(CubridView.Current_db);
			for (int i = 0, n = DBLoginfo.size(); i < n; i++) {
				if (((LogFileInfo) DBLoginfo.get(i)).filename
						.equals(Current_select)) {
					fileinfo = (LogFileInfo) DBLoginfo.get(i);
					break;
				}
			}
			if (fileinfo == null)
				this.dispose();
		}
	}

	public void createPartControl(Composite parent) {
		FillLayout flayout = new FillLayout();
		top = new Composite(parent, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		top.setLayout(flayout);
		createTable();
	}

	public void setFocus() {
		// TODO Auto-generated method stub

	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		table = new Table(top, SWT.FULL_SELECTION);
		table.setHeaderVisible(true);
		table.setLinesVisible(true);
		table
				.setBounds(new org.eclipse.swt.graphics.Rectangle(14, 16, 266,
						124));

		TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.PROPERTY"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.VALUE"));

		TableItem item;
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.FILENAME"));
		item.setText(1, fileinfo.filename);
		// item = new TableItem(table, SWT.NONE);
		// item.setText(0, Messages.getString("TABLE.FILEOWNER") );
		// item.setText(1, fileinfo.fileowner );
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.FILESIZE"));
		item.setText(1, fileinfo.size + " byte(s)");
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.CHANGEDATE"));
		item.setText(1, fileinfo.date);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.FILEPATH"));
		item.setText(1, fileinfo.path);

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 200, true));
		tlayout.addColumnData(new ColumnWeightData(50, 200, true));
		table.setLayout(tlayout);
	}
}
