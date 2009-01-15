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
import cubridmanager.cubrid.Trigger;

public class DBTriggers extends ViewPart implements ITreeObjectChangedListener {

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
		if (table == null) {
			table = new Table(top, SWT.FULL_SELECTION);
			table.setHeaderVisible(true);
			table.setLinesVisible(true);
			table.setBounds(new org.eclipse.swt.graphics.Rectangle(12, 20, 263,
					119));

			TableLayout tlayout = new TableLayout();
			tlayout.addColumnData(new ColumnWeightData(50, 200, true));
			tlayout.addColumnData(new ColumnWeightData(50, 200, true));
			table.setLayout(tlayout);

			TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
			tblColumn.setText(Messages.getString("TABLE.PROPERTY"));
			tblColumn = new TableColumn(table, SWT.LEFT);
			tblColumn.setText(Messages.getString("TABLE.VALUE"));
		} else {
			table.removeAll();
		}

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

	public void refresh() {
		objrec = null;
		if (CubridView.Current_db.length() <= 0)
			return;
		else {
			triggerinfo = Trigger.TriggerInfo_get(CubridView.Current_db);
			for (int i = 0, n = triggerinfo.size(); i < n; i++) {
				if (((Trigger) triggerinfo.get(i)).Name.equals(Current_select)) {
					objrec = (Trigger) triggerinfo.get(i);
					break;
				}
			}
			if (objrec == null)
				return;
		}
		createTable();
		top.layout(true);
	}

}
