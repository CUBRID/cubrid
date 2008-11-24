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
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.graphics.Rectangle;

import cubridmanager.Messages;

public class DiagTemplate extends ViewPart {

	public static final String ID = "workview.DiagTemplate"; 
	public static String CurrentSelect = null;
	public static String CurrentText = null;
	private Composite top = null;
	private Table table = null;
	private Label label = null;

	public DiagTemplate() {
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
		label.setBounds(new org.eclipse.swt.graphics.Rectangle(2, 3, 243, 32));
		label.setBackground(Display.getCurrent()
				.getSystemColor(SWT.COLOR_WHITE));
		label.setFont(new Font(Display.getDefault(), "\uad74\ub9bc\uccb4", 18,
				SWT.NORMAL));
		label.setText(Messages.getString("TITLE.DIAGTEMPLATE"));
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
		 * if (CurrentText.equals("CUBRID_diag_template")) { TableItem item1 =
		 * new TableItem(table, SWT.NONE); item1.setText(0,
		 * "CUBRID_diag_template"); item1.setText(1, "CUBRID Server diag");
		 * item1.setText(2, ""); } else if
		 * (CurrentText.equals("CAS_diag_template")) { TableItem item2 = new
		 * TableItem(table, SWT.NONE); item2.setText(0, "CAS_diag_template");
		 * item2.setText(1, "CUBRID CAS diag"); item2.setText(2, ""); }
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
				.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 40, 300,
						135));
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(20, 100, false));
		tlayout.addColumnData(new ColumnWeightData(20, 100, false));
		tlayout.addColumnData(new ColumnWeightData(80, 100, false));
		table.setLayout(tlayout);

		TableColumn NameColumn = new TableColumn(table, SWT.LEFT);
		TableColumn DescColumn = new TableColumn(table, SWT.LEFT);
		TableColumn TargetColumn = new TableColumn(table, SWT.LEFT);

		NameColumn.setText(Messages.getString("TABLE.DIAGNAME"));
		DescColumn.setText(Messages.getString("TABLE.DESCRIPTION"));
		TargetColumn.setText(Messages.getString("TABLE.DIAGTARGET"));
	}
}
