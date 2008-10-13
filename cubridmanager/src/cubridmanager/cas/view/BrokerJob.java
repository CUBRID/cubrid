package cubridmanager.cas.view;

import java.util.ArrayList;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.graphics.Cursor;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.graphics.Rectangle;

import cubridmanager.cas.BrokerAS;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.cas.BrokerJobStatus;
import cubridmanager.cas.view.CASView;

import org.eclipse.swt.widgets.Display;

public class BrokerJob extends ViewPart {

	public static final String ID = "workview.BrokerJob";
	// TODO Needs to be whatever is mentioned in plugin.xml

	public static ArrayList asinfo = new ArrayList();
	public static ArrayList jobqinfo = new ArrayList();
	public static String Current_broker = new String("");
	private Composite top = null;
	private Table table = null;
	private CLabel cLabel = null;
	private CLabel cLabel1 = null;
	private Table table1 = null;

	public BrokerJob() {
		super();
		if (Current_broker.length() <= 0) {
			if (CASView.Current_broker.length() <= 0)
				this.dispose();
			Current_broker = CASView.Current_broker;
		}
	}

	public void createPartControl(Composite parent) {
		// TODO Auto-generated method stub
		top = new Composite(parent, SWT.NONE);
		getbrokerstatus();
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		createTable();
		cLabel = new CLabel(top, SWT.NONE);
		cLabel.setText(Current_broker + Messages.getString("TXT.STATUS")); 
		cLabel
				.setBounds(new org.eclipse.swt.graphics.Rectangle(10, 12, 201,
						21));
		cLabel.setFont(new Font(Display.getDefault(), cLabel.getFont()
				.toString(), 14, SWT.NORMAL));
		cLabel.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		cLabel1 = new CLabel(top, SWT.NONE);
		cLabel1.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		cLabel1.setFont(new Font(Display.getDefault(), cLabel1.getFont()
				.toString(), 14, SWT.NORMAL));
		cLabel1.setText(Messages.getString("TXT.JOBQUEUESTATUS")); 
		cLabel1.setBounds(new org.eclipse.swt.graphics.Rectangle(11, 83, 195,
				25));
		createTable1();
		setinformation();
	}

	public void setFocus() {
		// TODO Auto-generated method stub

	}

	private void setinformation() {
		top.addPaintListener(new PaintListener() {
			public void paintControl(PaintEvent e) {
				adjustWindows();
			}
		});

		BrokerJobStatus bjrec;
		BrokerAS asrec;
		TableItem item;
		for (int i = 0, n = asinfo.size(); i < n; i++) {
			asrec = (BrokerAS) asinfo.get(i);

			item = new TableItem(table, SWT.NONE);
			item.setText(0, asrec.ID);
			item.setText(1, asrec.PID);
			item.setText(2, asrec.ClientRequests);
			item.setText(3, asrec.PSize);
			item.setText(4, asrec.Status);
			item.setText(5, asrec.CPU);
			item.setText(6, asrec.CTime);
			item.setText(7, asrec.LastAccessTime);
			item.setText(8, asrec.JobInfo);
		}

		for (int i = 0, n = jobqinfo.size(); i < n; i++) {
			bjrec = (BrokerJobStatus) jobqinfo.get(i);

			item = new TableItem(table1, SWT.NONE);
			item.setText(0, bjrec.ID);
			item.setText(1, bjrec.Priority);
			item.setText(2, bjrec.IP);
			item.setText(3, bjrec.jobTime);
			item.setText(4, bjrec.Request);
		}
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		table = new Table(top, SWT.FULL_SELECTION | SWT.BORDER);
		table.setHeaderVisible(true);
		table.setLinesVisible(true);
		table.setBounds(new org.eclipse.swt.graphics.Rectangle(7, 38, 282, 37));

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(20, 80, true));
		tlayout.addColumnData(new ColumnWeightData(20, 60, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(20, 70, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(20, 120, true));
		tlayout.addColumnData(new ColumnWeightData(20, 80, true));
		table.setLayout(tlayout);

		TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.SERVERID")); 
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.PROCESS")); 
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.REQUEST")); 
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.SIZE")); 
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.STATUS")); 
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.PCPU")); 
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.TIME")); 
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.LASTACCESS")); 
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.JOBINFO")); 
	}

	/**
	 * This method initializes table1
	 * 
	 */
	private void createTable1() {
		table1 = new Table(top, SWT.FULL_SELECTION | SWT.BORDER);
		table1.setHeaderVisible(true);
		table1.setLinesVisible(true);
		table1.setBounds(new org.eclipse.swt.graphics.Rectangle(14, 120, 274,
				41));

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(20, 100, true));
		tlayout.addColumnData(new ColumnWeightData(20, 100, true));
		tlayout.addColumnData(new ColumnWeightData(20, 100, true));
		tlayout.addColumnData(new ColumnWeightData(20, 100, true));
		tlayout.addColumnData(new ColumnWeightData(20, 100, true));
		table1.setLayout(tlayout);

		TableColumn tblColumn = new TableColumn(table1, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.QUEUEID")); 
		tblColumn = new TableColumn(table1, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.PRIORITY")); 
		tblColumn = new TableColumn(table1, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.ADDR")); 
		tblColumn = new TableColumn(table1, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.TIME")); 
		tblColumn = new TableColumn(table1, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.REQUEST")); 
	}

	public void adjustWindows() {
		Rectangle toprect = top.getBounds();
		Rectangle wrkrect = new Rectangle(toprect.x + 5, toprect.y + 5,
				toprect.width - 10, toprect.height - 10);
		wrkrect.height = 30;
		cLabel.setBounds(wrkrect);

		wrkrect.x = 0;
		wrkrect.y = 35; // first window's height+5
		wrkrect.width = toprect.width;
		wrkrect.height = (toprect.height / 2) - 40; // first window's height+10
		if (wrkrect.height <= 0)
			wrkrect.height = 0;
		table.setBounds(wrkrect);

		wrkrect.x = toprect.x + 5;
		wrkrect.y = (toprect.height / 2) + 5;
		wrkrect.width = toprect.width - 10;
		wrkrect.height = 30;
		cLabel1.setBounds(wrkrect);

		wrkrect.x = 0;
		wrkrect.y = (toprect.height / 2) + 35;
		wrkrect.width = toprect.width;
		wrkrect.height = (toprect.height / 2) - 40; // first window's height+10
		if (wrkrect.height <= 0)
			wrkrect.height = 0;
		table1.setBounds(wrkrect);
	}

	private void getbrokerstatus() {
		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(top.getShell(), "bname:" + Current_broker,
				"getbrokerstatus")) {
			CommonTool.ErrorBox(top.getShell(), cs.ErrorMsg);
			this.dispose();
			return;
		}
		Shell psh = top.getShell();
		psh.setEnabled(false);
		psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_WAIT));
		ClientSocket cs3 = new ClientSocket();
		cs3.tempcmd = Current_broker;
		boolean rst = cs3.SendClientMessage(psh, "bname:" + Current_broker,
				"getaslimit");
		psh.setEnabled(true);
		psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_ARROW));
		if (!rst) {
			CommonTool.ErrorBox(psh, cs3.ErrorMsg);
			this.dispose();
			return;
		}
	}

	public void refreshItem() {
		try {
			ClientSocket cs = new ClientSocket();
			if (!cs.SendClientMessage(top.getShell(),
					"bname:" + Current_broker, "getbrokerstatus")) {
			}
			Shell psh = top.getShell();
			ClientSocket cs3 = new ClientSocket();
			cs3.tempcmd = Current_broker;
			cs3.SendClientMessage(psh, "bname:" + Current_broker, "getaslimit");

			table.removeAll();
			setinformation();
		} catch (Exception ee) {
			return;
		}
	}
}
