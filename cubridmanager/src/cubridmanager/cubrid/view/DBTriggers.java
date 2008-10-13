package cubridmanager.cubrid.view;

import java.util.ArrayList;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;

import cubridmanager.Messages;
import cubridmanager.cubrid.*;

public class DBTriggers extends ViewPart {

	public static final String ID = "workview.DBTriggers";
	// TODO Needs to be whatever is mentioned in plugin.xml
	public static final String OBJ = "TriggerObj";
	// TODO Needs to be whatever is mentioned in plugin.xml
	private Composite top = null;
	private Trigger objrec = null;
	private Table table = null;
	public static String Current_select = new String(""); //$NON-NLS-1$
	public static ArrayList triggerinfo = null;

	public DBTriggers() {
		super();
		if (CubridView.Current_db.length() <= 0)
			this.dispose();
		else {
			triggerinfo = Trigger.TriggerInfo_get(CubridView.Current_db);
			for (int i = 0, n = triggerinfo.size(); i < n; i++) {
				if (((Trigger) triggerinfo.get(i)).Name.equals(Current_select)) {
					objrec = (Trigger) triggerinfo.get(i);
					break;
				}
			}
			if (objrec == null)
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

	private void createTable() {
		table = new Table(top, SWT.FULL_SELECTION);
		table.setHeaderVisible(true);
		table.setLinesVisible(true);
		table
				.setBounds(new org.eclipse.swt.graphics.Rectangle(12, 20, 263,
						119));

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 200, true));
		tlayout.addColumnData(new ColumnWeightData(50, 200, true));
		table.setLayout(tlayout);

		TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.PROPERTY"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.VALUE"));

		TableItem item;
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.NAME"));
		item.setText(1, objrec.Name);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.CONDITIONAPPLY"));
		item.setText(1, objrec.ConditionTime);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.EVENT"));
		item.setText(1, objrec.EventType);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.COMPENSATION"));
		item.setText(1, objrec.EventTarget);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.CONDITION"));
		item.setText(1, objrec.ConditionString);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.EXECUTIONTIME"));
		item.setText(1, objrec.ActionTime);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.CONTENT"));
		item.setText(1, objrec.ActionString);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.STATUS"));
		item.setText(1, objrec.Status);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.PRIORITY"));
		item.setText(1, objrec.Priority);
	}

}
