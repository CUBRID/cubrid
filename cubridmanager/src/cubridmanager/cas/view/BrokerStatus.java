package cubridmanager.cas.view;

import java.util.ArrayList;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Listener;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Display;

import cubridmanager.ClientSocket;
import cubridmanager.TreeObject;
import cubridmanager.WorkView;
import cubridmanager.Messages;
import cubridmanager.MainRegistry;
import cubridmanager.cas.CASItem;

public class BrokerStatus extends ViewPart {

	public static final String ID = "workview.BrokerStatus";
	// TODO Needs to be whatever is mentioned in plugin.xml

	public static ArrayList brkinfo = null;
	public static String Current_select = new String("");
	private Composite top = null;
	private Table table = null;

	public BrokerStatus() {
		super();
		brkinfo = MainRegistry.CASinfo;
	}

	public void createPartControl(Composite parent) {
		FillLayout flayout = new FillLayout();
		top = new Composite(parent, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		top.setLayout(flayout);
		createTable();
		setinformation();
	}

	private void setinformation() {
		CASItem casrec;
		TableItem item;
		for (int i = 0, n = brkinfo.size(); i < n; i++) {
			casrec = (CASItem) brkinfo.get(i);

			item = new TableItem(table, SWT.NONE);
			item.setText(0, casrec.broker_name);
			item.setText(1, casrec.type);
			item.setText(2, casrec.state);
			item.setText(3, casrec.pid);
			item.setText(4, casrec.port);
			item.setText(5, casrec.as);
			item.setText(6, casrec.jq);
			item.setText(7, casrec.thr);
			item.setText(8, casrec.cpu);
			item.setText(9, casrec.time);
			item.setText(10, casrec.req);
			item.setText(11, casrec.auto);
			item.setText(12, casrec.ses);
			item.setText(13, casrec.sqll);
			item.setText(14, casrec.log);
		}
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
				.setBounds(new org.eclipse.swt.graphics.Rectangle(8, 27, 278,
						111));

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(20, 80, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 60, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		table.setLayout(tlayout);

		table.addMouseListener(new org.eclipse.swt.events.MouseAdapter() {
			public void mouseDoubleClick(org.eclipse.swt.events.MouseEvent e) {
				if (Current_select.length() > 0) {
					TreeObject lastsel = CASView.SelectBroker(Current_select);
					WorkView.SetView(lastsel.getViewID(), lastsel.getName(),
							null);
				}
			}
		});
		table.addListener(SWT.MouseDown, new Listener() {
			public void handleEvent(Event event) {
				Point pt = new Point(event.x, event.y);
				TableItem item = table.getItem(pt);
				if (item == null)
					Current_select = "";
				else
					Current_select = item.getText(0);
			}
		});

		TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.BROKERNAME"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.BROKERTYPE"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.STATUS"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.PROCESS"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.PORT"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.SERVER"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.QUEUE"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.THREAD"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.PCPU"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.TIME"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.REQUEST"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.AUTOADD"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.SESSION"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.SQLLOG"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.LOG"));
	}

	public void refreshItem() {
		try {
			ClientSocket cs = new ClientSocket();
			if (!cs.SendClientMessage(top.getShell(), "", "getbrokersinfo")) {
			}

			table.removeAll();
			setinformation();
		} catch (Exception ee) {
			return;
		}
	}
}
