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

import java.text.NumberFormat;
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

import cubridmanager.CommonTool;
import cubridmanager.ITreeObjectChangedListener;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.VolumeInfo;

public class DBSpace extends ViewPart implements ITreeObjectChangedListener {

	public static final String ID = "workview.DBSpace";
	// TODO Needs to be whatever is mentioned in plugin.xml
	public static final String VOL_GENERAL = "VOL_GENERAL";
	public static final String VOL_ACTIVE = "VOL_ACTIVE";
	public static final String VOL_ARCHIVE = "VOL_ARCHIVE";
	public static final String VOL_OBJECT = "VOL_OBJECT";
	public static String CurrentSelect = new String("");
	public static String CurrentObj = new String("");
	public static String CurrentVolumeType = new String("");
	private VolumeInfo objrec = null;
	private AuthItem DB_Auth = null;
	private ArrayList Volinfo = null;
	private Composite top = null;
	private Table table = null;

	public DBSpace() {
		super();
		if (CubridView.Current_db.length() < 1 || CurrentSelect.length() < 1)
			this.dispose();
		else {
			DB_Auth = MainRegistry.Authinfo_find(CubridView.Current_db);
			Volinfo = DB_Auth.Volinfo;
			for (int i = 0, n = Volinfo.size(); i < n; i++) {
				if (((VolumeInfo) Volinfo.get(i)).spacename.equals(CurrentObj)) {
					objrec = (VolumeInfo) Volinfo.get(i);
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

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		if (CurrentVolumeType.equals(VOL_ACTIVE))
			setPartName(Messages.getString("TREE.ACTIVE")
					+ Messages.getString("STRING.INFORMATION"));
		else if (CurrentVolumeType.equals(VOL_ARCHIVE))
			setPartName(Messages.getString("TREE.ARCHIVE")
					+ Messages.getString("STRING.INFORMATION"));
		else if (CurrentVolumeType.equals(VOL_GENERAL))
			setPartName(Messages.getString("TREE.GENERIC")
					+ Messages.getString("STRING.INFORMATION"));

		if (table == null) {
			table = new Table(top, SWT.FULL_SELECTION);
			table.setHeaderVisible(true);
			table.setLinesVisible(true);
			table.setBounds(new org.eclipse.swt.graphics.Rectangle(10, 49, 272,
					95));

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
		item.setText(0, Messages.getString("TABLE.VOLUMENAME"));
		item.setText(1, objrec.spacename);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.VOLUMEPATH"));
		item.setText(1, objrec.location);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.CHANGEDATE"));
		item.setText(1, CommonTool.convertYYYYMMDD(objrec.date));
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.VOLUMETYPE"));
		item.setText(1, objrec.type.toUpperCase().replaceAll("_", " "));
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.TOTALSIZEPAGES"));
		item.setText(1, objrec.tot);
		if (!objrec.type.equals("Active_log")
				&& !objrec.type.equals("Archive_log")) {
			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("TABLE.REMAINSIZEPAGES"));
			item.setText(1, objrec.free);
		}
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.SIZEMB"));
		double mb = (DB_Auth.pagesize * CommonTool.atol(objrec.tot))
				/ (double) MainConstants.MEGABYTES;
		NumberFormat nf = NumberFormat.getInstance();
		nf.setMaximumFractionDigits(2);
		nf.setGroupingUsed(false);
		item.setText(1, nf.format(mb));

	}

	public void refresh() {
		if (CubridView.Current_db.length() < 1 || CurrentSelect.length() < 1)
			return;
		else {
			DB_Auth = MainRegistry.Authinfo_find(CubridView.Current_db);
			Volinfo = DB_Auth.Volinfo;
			for (int i = 0, n = Volinfo.size(); i < n; i++) {
				if (((VolumeInfo) Volinfo.get(i)).spacename.equals(CurrentObj)) {
					objrec = (VolumeInfo) Volinfo.get(i);
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
