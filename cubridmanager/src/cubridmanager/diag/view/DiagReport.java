package cubridmanager.diag.view;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Label;

import cubridmanager.Messages;

public class DiagReport extends ViewPart {

	public static final String ID = "workview.DiagReport";
	public static String CurrentSelect = null;
	public static String CurrentText = null;
	private Composite top = null;
	private Table table = null;
	private Label label = null;

	public DiagReport() {
		super();
		// TODO Auto-generated constructor stub
	}

	public void createPartControl(Composite parent) {
		// TODO Auto-generated method stub
		top = new Composite(parent, SWT.NONE);
		top.setLayout(null);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		createTable();
		label = new Label(top, SWT.NONE);
		label.setBounds(new org.eclipse.swt.graphics.Rectangle(2, 6, 237, 31));
		label.setBackground(Display.getCurrent()
				.getSystemColor(SWT.COLOR_WHITE));
		label.setFont(new Font(Display.getDefault(), "\uad74\ub9bc\uccb4", 18,
				SWT.NORMAL));
		label.setText(Messages.getString("TITLE.DIAGREPORT"));
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
		if (CurrentText.equals("200604_diag_report.drp")) {
			TableItem item1 = new TableItem(table, SWT.NONE);
			item1.setText(0, "200604_diag_report.drp");
			item1.setText(1, "c:\\cubrid_manager\\diag_report\\");
			item1.setText(2, "CUBRID_diag_template");
			item1.setText(3, "2006/05/10 12:10");
			item1.setText(4, "2006/05/10 15:17");
			item1.setText(5, "Complete");
		} else if (CurrentText.equals("200605_diag_report.drp")) {
			TableItem item2 = new TableItem(table, SWT.NONE);
			item2.setText(0, "200605_diag_report.drp");
			item2.setText(1, "c:\\cubrid_manager\\diag_report\\");
			item2.setText(2, "CAS_diag_template");
			item2.setText(3, "2006/08/10 23:00");
			item2.setText(4, "");
			item2.setText(5, "Doesn't start");
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
				.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 47, 300,
						128));
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(17, 150, false));
		tlayout.addColumnData(new ColumnWeightData(36, 300, false));
		tlayout.addColumnData(new ColumnWeightData(9, 80, false));
		tlayout.addColumnData(new ColumnWeightData(15, 120, false));
		tlayout.addColumnData(new ColumnWeightData(15, 120, false));
		tlayout.addColumnData(new ColumnWeightData(8, 50, false));
		table.setLayout(tlayout);

		TableColumn NameColumn = new TableColumn(table, SWT.LEFT);
		TableColumn PathColumn = new TableColumn(table, SWT.LEFT);
		TableColumn templateColumn = new TableColumn(table, SWT.LEFT);
		TableColumn startTimeColumn = new TableColumn(table, SWT.LEFT);
		TableColumn endTimeColumn = new TableColumn(table, SWT.LEFT);
		TableColumn statusColumn = new TableColumn(table, SWT.LEFT);

		NameColumn.setText(Messages.getString("TABLE.DIAGNAME"));
		PathColumn.setText(Messages.getString("TABLE.DIAGPATH"));
		templateColumn.setText(Messages.getString("TABLE.TEMPLATE"));
		startTimeColumn.setText(Messages.getString("TABLE.DIAGSTARTTIME"));
		endTimeColumn.setText(Messages.getString("TABLE.DIAGENDTIME"));
		statusColumn.setText(Messages.getString("TABLE.STATUS"));

		// hookContextMenu(table);
	}

	/*
	 * private void hookContextMenu(Control popctrl) { MenuManager menuMgr = new
	 * MenuManager("PopupMenu", "ContextMenu");
	 * menuMgr.setRemoveAllWhenShown(true); menuMgr.addMenuListener(new
	 * IMenuListener() { public void menuAboutToShow(IMenuManager manager) { int
	 * count = table.getSelectionCount(); if (count > 0) { manager.add(new
	 * Separator()); GroupMarker marker = new
	 * GroupMarker(IWorkbenchActionConstants.MB_ADDITIONS); manager.add(marker);
	 * manager.add(ApplicationActionBarAdvisor.diagRemoveDiagReportAction); } }
	 * });
	 * 
	 * Menu menu = menuMgr.createContextMenu(popctrl); MenuItem
	 * newContextMenuItem = new MenuItem(menu, SWT.NONE);
	 * newContextMenuItem.setText("context.item");
	 * 
	 * popctrl.setMenu(menu); }
	 */
}
