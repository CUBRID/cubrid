package cubridmanager.diag.view;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.graphics.Rectangle;

import cubridmanager.Messages;

public class ServiceReport extends ViewPart {

	public static final String ID = "workview.ServiceReport"; 
	public static String CurrentSelect = null;
	public static String CurrentText = null;
	private Composite top = null;
	private Table table = null;
	private Label label = null;

	public ServiceReport() {
		super();
		// TODO Auto-generated constructor stub
	}

	public void createPartControl(Composite parent) {
		// TODO Auto-generated method stub
		top = new Composite(parent, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		createTable();
		label = new Label(top, SWT.NONE);
		label
				.setBounds(new org.eclipse.swt.graphics.Rectangle(13, 11, 143,
						35));
		label.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 18,
				SWT.NORMAL));
		label.setBackground(Display.getCurrent()
				.getSystemColor(SWT.COLOR_WHITE));
		label.setText(Messages.getString("TITLE.TROUBLEINFO"));
		setinformation();
	}

	private void setinformation() {
		top.addPaintListener(new PaintListener() {
			public void paintControl(PaintEvent e) {
				adjustWindows();
			}
		});

		insertListData();
	}

	public void adjustWindows() {
		Rectangle toprect = top.getBounds();
		Rectangle wrkrect = new Rectangle(toprect.x + 5, toprect.y + 5,
				toprect.width - 10, toprect.height - 10);
		wrkrect.height = 30;
		label.setBounds(wrkrect);

		wrkrect.x = 0;
		wrkrect.y = 35; // first window's height+5
		wrkrect.width = toprect.width;
		wrkrect.height = toprect.height - 40; // first window's height+10
		if (wrkrect.height <= 0)
			wrkrect.height = 0;
		table.setBounds(wrkrect);
	}

	public void insertListData() {
		if (CurrentText.equals("subway.err(-100)")) {
			TableItem item = new TableItem(table, SWT.NONE);
			item.setText(0, "subway.err");
			item.setText(1, "subway");
			item.setText(2, "-100");
			item.setText(3, "2006/07/20 15:13:10");
			item.setText(4, "");
			item.setText(5, "/home/CUBRID/admin/sqlx.init");
			item.setText(6, "");
		} else if (CurrentText.equals("subway.err(-110)")) {
			TableItem item = new TableItem(table, SWT.NONE);
			item.setText(0, "subway.err");
			item.setText(1, "subway");
			item.setText(2, "-110");
			item.setText(3, "2006/07/21 10:12:16");
			item.setText(4, "/home/CUBRID/core");
			item.setText(5, "/home/CUBRID/admin/sqlx.init");
			item.setText(6, "c:\\CUBRIDMANAGER\\packaging\\TR_20060510.cpk");
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
				.setBounds(new org.eclipse.swt.graphics.Rectangle(8, 54, 276,
						109));

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(15, 90, false));
		tlayout.addColumnData(new ColumnWeightData(10, 60, false));
		tlayout.addColumnData(new ColumnWeightData(10, 60, false));
		tlayout.addColumnData(new ColumnWeightData(15, 90, false));
		tlayout.addColumnData(new ColumnWeightData(15, 90, false));
		tlayout.addColumnData(new ColumnWeightData(15, 90, false));
		tlayout.addColumnData(new ColumnWeightData(15, 90, false));
		table.setLayout(tlayout);

		TableColumn errFile = new TableColumn(table, SWT.NONE);
		TableColumn dbName = new TableColumn(table, SWT.NONE);
		TableColumn errorNum = new TableColumn(table, SWT.NONE);
		TableColumn time = new TableColumn(table, SWT.NONE);
		TableColumn coreFile = new TableColumn(table, SWT.NONE);
		TableColumn sqlxInit = new TableColumn(table, SWT.NONE);
		TableColumn packagingFile = new TableColumn(table, SWT.NONE);

		errFile.setText(Messages.getString("TABLE.ERRORFILE"));
		dbName.setText(Messages.getString("TABLE.TARGETDB"));
		errorNum.setText(Messages.getString("TABLE.ERRORNUM"));
		time.setText(Messages.getString("TABLE.ERRTIME"));
		coreFile.setText(Messages.getString("TABLE.COREFILE"));
		sqlxInit.setText(Messages.getString("TABLE.CONFIGFILE"));
		packagingFile.setText(Messages.getString("TABLE.PACKAGEFILE"));

		// hookContextMenu(table);
	}
	/*
	 * private void hookContextMenu(Control popctrl) { MenuManager menuMgr = new
	 * MenuManager("PopupMenu", "contextMenu");
	 * menuMgr.setRemoveAllWhenShown(true); menuMgr.addMenuListener(new
	 * IMenuListener() { public void menuAboutToShow(IMenuManager manager) { int
	 * count = table.getSelectionCount(); if (count > 0) { manager.add(new
	 * Separator()); GroupMarker marker = new
	 * GroupMarker(IWorkbenchActionConstants.MB_ADDITIONS); manager.add(marker);
	 * manager.add(ApplicationActionBarAdvisor.diagTroubleTraceAction);
	 * manager.add(new Separator());
	 * manager.add(ApplicationActionBarAdvisor.diagNewUserTroubleAction);
	 * manager.add(ApplicationActionBarAdvisor.diagRemoveTroubleAction); } } });
	 * 
	 * Menu menu = menuMgr.createContextMenu(popctrl); MenuItem
	 * newContextMenuItem = new MenuItem(menu, SWT.NONE);
	 * newContextMenuItem.setText("context.item");
	 * 
	 * popctrl.setMenu(menu); }
	 */
}
