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

package cubridmanager.diag.view;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.graphics.Font;

import cubridmanager.Messages;

public class StatusWarning extends ViewPart {

	public static final String ID = "workview.StatusWarning";
	private Composite top = null;
	private Table table = null;
	private Label label = null;

	public StatusWarning() {
		super();
		// TODO Auto-generated constructor stub
	}

	public void createPartControl(Composite parent) {
		// TODO Auto-generated method stub
		top = new Composite(parent, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		top.setLayout(null);

		createTable();

		label = new Label(top, SWT.NONE);
		label.setBounds(new org.eclipse.swt.graphics.Rectangle(3, 5, 133, 26));
		label.setBackground(Display.getCurrent()
				.getSystemColor(SWT.COLOR_WHITE));
		label.setFont(new Font(Display.getDefault(), "\uad74\ub9bc\uccb4", 18,
				SWT.NORMAL));
		label.setText(Messages.getString("TITLE.STATUSWARNING"));
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
		TableItem item1 = new TableItem(table, SWT.NONE);
		item1.setText(0, "Memory usage monitoring");
		item1.setText(1, "Memory");
		item1.setText(2, "host_resource");
		item1.setText(3, "2006/05/01 18:00:00");
		item1.setText(4, "Manual");
		item1.setText(5, "c:\\cubrid_manager\\memory_alert.bat");
		item1.setText(6, "Start");
		/*
		 * TableItem item2 = new TableItem(table, SWT.NONE); item2.setText(0,
		 * "Opened file count monitor"); item2.setText(1, "Query"); item2.setText(2,
		 * "num_files_tables"); item2.setText(3, "2006/05/01 18:00:00");
		 * item2.setText(4, "Manual"); item2.setText(5,
		 * "c:\\cubrid_manager\\open_file_alert.bat"); item2.setText(6, "Start");
		 */
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
				.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 50, 300,
						200));

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(30, 120, true));
		tlayout.addColumnData(new ColumnWeightData(10, 80, true));
		tlayout.addColumnData(new ColumnWeightData(20, 100, true));
		tlayout.addColumnData(new ColumnWeightData(20, 100, true));
		tlayout.addColumnData(new ColumnWeightData(20, 100, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		table.setLayout(tlayout);

		TableColumn DescColumn = new TableColumn(table, SWT.LEFT);
		DescColumn.setText(Messages.getString("TABLE.DESCRIPTION"));
		TableColumn CounterColumn = new TableColumn(table, SWT.LEFT);
		CounterColumn.setText(Messages.getString("TABLE.COUNTER"));
		TableColumn LogNameColumn = new TableColumn(table, SWT.LEFT);
		LogNameColumn.setText(Messages.getString("TABLE.DIAGSTATUSLOGNAME"));
		TableColumn LogStartColumn = new TableColumn(table, SWT.LEFT);
		LogStartColumn.setText(Messages.getString("TABLE.DIAGSTARTTIME"));
		TableColumn LogEndColumn = new TableColumn(table, SWT.LEFT);
		LogEndColumn.setText(Messages.getString("TABLE.DIAGENDTIME"));
		TableColumn programColumn = new TableColumn(table, SWT.LEFT);
		programColumn.setText(Messages.getString("TABLE.DIAGPROGRAMNAME"));
		TableColumn StatusColumn = new TableColumn(table, SWT.LEFT);
		StatusColumn.setText(Messages.getString("TABLE.STATUS"));

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
	 * manager.add(ApplicationActionBarAdvisor.diagUpdateStatusWarningAction);
	 * manager.add(new Separator());
	 * manager.add(ApplicationActionBarAdvisor.diagNewStatusWarningAction);
	 * manager.add(ApplicationActionBarAdvisor.diagRemoveStatusWarningAction);
	 * manager.add(new Separator());
	 * manager.add(ApplicationActionBarAdvisor.diagStartStatusWarningAction);
	 * manager.add(ApplicationActionBarAdvisor.diagStopStatusWarningAction); } }
	 * });
	 * 
	 * Menu menu = menuMgr.createContextMenu(popctrl); MenuItem
	 * newContextMenuItem = new MenuItem(menu, SWT.NONE);
	 * newContextMenuItem.setText("context.item");
	 * 
	 * popctrl.setMenu(menu); }
	 */
}
