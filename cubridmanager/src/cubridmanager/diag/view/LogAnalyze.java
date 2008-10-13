package cubridmanager.diag.view;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Label;

public class LogAnalyze extends ViewPart {

	public static final String ID = "workview.LogAnalyze";
	private Composite top = null;
	private Table table = null;
	private Label label = null;

	public LogAnalyze() {
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
		label.setBounds(new org.eclipse.swt.graphics.Rectangle(3, 4, 202, 33));
		label.setBackground(Display.getCurrent()
				.getSystemColor(SWT.COLOR_WHITE));
		label.setFont(new Font(Display.getDefault(), "\uad74\ub9bc\uccb4", 18,
				SWT.NORMAL));
		label.setText("Log analysis report");
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
		/*
		 * Insert List Item for status template.
		 */
		TableItem item1 = new TableItem(table, SWT.NONE);
		item1.setText(0, "resource_20060510_log_report.rpt");
		item1.setText(1, "c:\\cubrid_manager\\log\\");

		TableItem item2 = new TableItem(table, SWT.NONE);
		item2.setText(0, "query_20060510_log_report.rpt");
		item2.setText(1, "c:\\cubrid_manager\\log\\");
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
				.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 43, 300,
						132));
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(30, 200, false));
		tlayout.addColumnData(new ColumnWeightData(70, 500, false));
		table.setLayout(tlayout);

		TableColumn NameColumn = new TableColumn(table, SWT.LEFT);
		TableColumn PathColumn = new TableColumn(table, SWT.LEFT);

		NameColumn.setText("Name");
		PathColumn.setText("Location");

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
	 * manager.add(ApplicationActionBarAdvisor.diagRemoveLogAnalysisReportAction); } }
	 * });
	 * 
	 * Menu menu = menuMgr.createContextMenu(popctrl); MenuItem
	 * newContextMenuItem = new MenuItem(menu, SWT.NONE);
	 * newContextMenuItem.setText("context.item");
	 * 
	 * popctrl.setMenu(menu); }
	 */
}
