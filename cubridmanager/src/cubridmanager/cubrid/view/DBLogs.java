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

package cubridmanager.cubrid.view;

import java.util.ArrayList;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.ui.part.ViewPart;

import cubridmanager.ITreeObjectChangedListener;
import cubridmanager.Messages;
import cubridmanager.cubrid.LogFileInfo;

public class DBLogs extends ViewPart implements ITreeObjectChangedListener {

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
		if (table == null) {
			table = new Table(top, SWT.FULL_SELECTION);
			table.setHeaderVisible(true);
			table.setLinesVisible(true);
			table.setBounds(new org.eclipse.swt.graphics.Rectangle(14, 16, 266,
					124));

			TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
			tblColumn.setText(Messages.getString("TABLE.PROPERTY"));
			tblColumn = new TableColumn(table, SWT.LEFT);
			tblColumn.setText(Messages.getString("TABLE.VALUE"));

			TableLayout tlayout = new TableLayout();
			tlayout.addColumnData(new ColumnWeightData(50, 200, true));
			tlayout.addColumnData(new ColumnWeightData(50, 200, true));
			table.setLayout(tlayout);
		} else {
			table.removeAll();
		}

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

	}

	public void refresh() {
		fileinfo = null;
		if (CubridView.Current_db.length() <= 0)
			return;
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
				return;
		}
		createTable();
		top.layout(true);
	}
}
