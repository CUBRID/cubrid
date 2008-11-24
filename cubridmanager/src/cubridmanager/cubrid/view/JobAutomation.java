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
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;

import cubridmanager.cubrid.Jobs;
import cubridmanager.cubrid.AutoQuery;
import cubridmanager.CommonTool;
import cubridmanager.Messages;

public class JobAutomation extends ViewPart {
	// TODO Needs to be whatever is mentioned in plugin.xml
	public static final String ID = "workview.JobAutomation";
	public static final String BACKJOBS = "JobAutomation.backjobs";
	public static final String QUERYJOBS = "JobAutomation.queryjobs";
	public static final String BACKJOB = "JobAutomation.backjob";
	public static final String QUERYJOB = "JobAutomation.queryjob";
	private Composite top = null;
	private Table table = null;
	public static String Current_select = new String("");
	public static String CurrentObj = new String("");
	public static ArrayList jobinfo = null;
	public static Jobs objrec = null;
	public static AutoQuery objaq = null;

	public JobAutomation() {
		super();
		objrec = null;
		objaq = null;
		if (CubridView.Current_db.length() <= 0)
			this.dispose();
		else {
			if (Current_select.equals(BACKJOB)) {
				jobinfo = Jobs.JobsInfo_get(CubridView.Current_db);
				for (int i = 0, n = jobinfo.size(); i < n; i++) {
					if (((Jobs) jobinfo.get(i)).backupid.equals(CurrentObj)) {
						objrec = (Jobs) jobinfo.get(i);
						break;
					}
				}
				if (objrec == null)
					this.dispose();
			} else {
				jobinfo = AutoQuery.AutoQueryInfo_get(CubridView.Current_db);
				for (int i = 0, n = jobinfo.size(); i < n; i++) {
					if (((AutoQuery) jobinfo.get(i)).QueryID.equals(CurrentObj)) {
						objaq = (AutoQuery) jobinfo.get(i);
						break;
					}
				}
				if (objaq == null)
					this.dispose();
			}
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
		table = new Table(top, SWT.FULL_SELECTION);
		table.setHeaderVisible(true);
		table.setLinesVisible(true);

		TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.PROPERTY"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.VALUE"));

		TableItem item;
		if (Current_select.equals(BACKJOB)) {
			setPartName(Messages.getString("TREE.BACKJOBS")
					+ Messages.getString("STRING.INFORMATION"));
			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("TABLE.BACKUPID"));
			item.setText(1, objrec.backupid);

			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("TABLE.BACKUPPATH"));
			item.setText(1, objrec.path);

			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("TABLE.BACKUPINTERVAL"));
			if (objrec.periodtype.equals("Monthly"))
				item.setText(1, Messages.getString("COMBOITEM.MONTHLY"));
			else if (objrec.periodtype.equals("Weekly"))
				item.setText(1, Messages.getString("COMBOITEM.WEEKLY"));
			else if (objrec.periodtype.equals("Daily"))
				item.setText(1, Messages.getString("COMBOITEM.DAILY"));
			else if (objrec.periodtype.equals("Special"))
				item.setText(1, Messages.getString("COMBOITEM.SPECIAL"));

			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("TABLE.BACKUPDATE"));
			if (objrec.periodtype.equals("Weekly")) {
				if (objrec.perioddetail.equals("Sunday"))
					item.setText(1, Messages.getString("COMBOITEM.DAY0"));
				else if (objrec.perioddetail.equals("Monday"))
					item.setText(1, Messages.getString("COMBOITEM.DAY1"));
				else if (objrec.perioddetail.equals("Tuesday"))
					item.setText(1, Messages.getString("COMBOITEM.DAY2"));
				else if (objrec.perioddetail.equals("Wednesday"))
					item.setText(1, Messages.getString("COMBOITEM.DAY3"));
				else if (objrec.perioddetail.equals("Thursday"))
					item.setText(1, Messages.getString("COMBOITEM.DAY4"));
				else if (objrec.perioddetail.equals("Friday"))
					item.setText(1, Messages.getString("COMBOITEM.DAY5"));
				else if (objrec.perioddetail.equals("Saturday"))
					item.setText(1, Messages.getString("COMBOITEM.DAY6"));
				else
					item.setText(1, objrec.perioddetail);
			} else
				item.setText(1, objrec.perioddetail);

			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("TABLE.BACKUPTIME"));
			item.setText(1, CommonTool.convertHH24MON(objrec.time));

			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("TABLE.BACKUPLEVEL"));
			item.setText(1, objrec.level);

			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("CHECK.STOREOLDBACKUP"));
			item.setText(1, objrec.storeold.equals("ON") ? Messages
					.getString("TABLE.YES") : Messages.getString("TABLE.NO"));

			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("CHECK.DELETEARCHIVE"));
			item.setText(1, objrec.archivedel.equals("ON") ? Messages
					.getString("TABLE.YES") : Messages.getString("TABLE.NO"));

			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("CHECK.UPDATESTATISTICS1"));
			item.setText(1, objrec.updatestatus.equals("ON") ? Messages
					.getString("TABLE.YES") : Messages.getString("TABLE.NO"));

			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("GROUP.ONLINEOFFLINE"));
			item.setText(1, objrec.onoff.equals("ON") ? Messages
					.getString("RADIO.ONLINEBACKUP") : Messages
					.getString("RADIO.OFFLINEBACKUP"));

			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("LABEL.COMPRESS"));
			item.setText(1, objrec.zip.equals("y") ? Messages
					.getString("TABLE.YES") : Messages.getString("TABLE.NO"));

			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("CHECK.CHCKINGCONSISTENCY"));
			item.setText(1, objrec.check.equals("y") ? Messages
					.getString("TABLE.YES") : Messages.getString("TABLE.NO"));

			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("LABEL.READTHREADALLOWED"));
			item.setText(1, objrec.mt);
		} else {
			setPartName(Messages.getString("TREE.QUERYJOBS")
					+ Messages.getString("STRING.INFORMATION"));
			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("TABLE.AUTOQUERYID"));
			item.setText(1, objaq.QueryID);
			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("TABLE.QUERYPERIOD"));
			if (objaq.Period.equals("MONTH"))
				item.setText(1, Messages.getString("COMBOITEM.MONTHLY"));
			else if (objaq.Period.equals("WEEK"))
				item.setText(1, Messages.getString("COMBOITEM.WEEKLY"));
			else if (objaq.Period.equals("DAY"))
				item.setText(1, Messages.getString("COMBOITEM.DAILY"));
			else if (objaq.Period.equals("ONE"))
				item.setText(1, Messages.getString("COMBOITEM.SPECIAL"));
			String[] queryDetail = objaq.TimeDetail.split(" ");
			if (queryDetail.length == 2) {
				if (!queryDetail[0].equals("EVERYDAY")) {
					item = new TableItem(table, SWT.NONE);
					item.setText(0, Messages
							.getString("TABLE.QUERYPERIODDETAIL"));
					if (queryDetail[0].equals("SUN"))
						item.setText(1, Messages.getString("COMBOITEM.DAY0"));
					else if (queryDetail[0].equals("MON"))
						item.setText(1, Messages.getString("COMBOITEM.DAY1"));
					else if (queryDetail[0].equals("TUE"))
						item.setText(1, Messages.getString("COMBOITEM.DAY2"));
					else if (queryDetail[0].equals("WED"))
						item.setText(1, Messages.getString("COMBOITEM.DAY3"));
					else if (queryDetail[0].equals("THU"))
						item.setText(1, Messages.getString("COMBOITEM.DAY4"));
					else if (queryDetail[0].equals("FRI"))
						item.setText(1, Messages.getString("COMBOITEM.DAY5"));
					else if (queryDetail[0].equals("SAT"))
						item.setText(1, Messages.getString("COMBOITEM.DAY6"));
					else
						item.setText(1, queryDetail[0].replaceAll("/", "-"));
				} else if (queryDetail[0].equals("EVERYDAY"))
					; // empty statement!
				item = new TableItem(table, SWT.NONE);
				item.setText(0, Messages.getString("TABLE.QUERYTIME"));
				item.setText(1, queryDetail[1]);
			}

			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("TABLE.QUERYCONTENTS"));
			item.setText(1, objaq.QueryString);
		}

		table.pack();

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, true));
		tlayout.addColumnData(new ColumnWeightData(50, true));
		table.setLayout(tlayout);
	}
}
