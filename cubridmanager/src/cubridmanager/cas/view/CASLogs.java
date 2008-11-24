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

package cubridmanager.cas.view;

import java.util.ArrayList;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.widgets.Display;

import cubridmanager.cubrid.LogFileInfo;
import cubridmanager.Messages;
import cubridmanager.MainRegistry;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;

public class CASLogs extends ViewPart {

	public static final String ID = "workview.CASLogs";	
	// TODO Needs to be whatever is mentioned in plugin.xml

	public static final String LOGS_ALL = "CASLOG_ALL";
	public static final String LOGS_ACCESS = "CASLOG_ACCESS";
	public static final String LOGS_ERROR = "CASLOG_ERROR";
	public static final String LOGS_SCRIPT = "CASLOG_SCRIPT";
	public static final String LOGS_ADMIN = "CASLOG_ADMIN";
	public static final String ADMINOBJ = "ADMINOBJ";
	public static final String LOGSOBJ = "LOGSOBJ";
	public static final String SCRIPTOBJ = "SCRIPTOBJ";
	public static String CurrentSelect = new String("");;
	public static String CurrentObj = new String("");;
	public static String CurrentText = new String("");;
	public static ArrayList loginfo = null;
	private Composite top = null;
	public static LogFileInfo fileinfo = null;
	private static Table table = null;
	private CLabel cLabel = null;

	public CASLogs() {
		super();

		if (MainRegistry.CASLogView_RequestedInDiag) {
			loginfo = LogFileInfo
					.BrokerLog_get(MainRegistry.CASLogView_RequestBrokername);
			fileinfo = null;
			for (int i = 0, n = loginfo.size(); i < n; i++) {
				if (((LogFileInfo) loginfo.get(i)).filename.equals(CurrentText)) {
					fileinfo = (LogFileInfo) loginfo.get(i);
					break;
				}
			}
		} else {
			if (!CurrentSelect.equals(ADMINOBJ)
					&& CASView.Current_broker.length() <= 0)
				this.dispose();
			else {
				if (CurrentSelect.equals(LOGS_ADMIN)
						|| CurrentSelect.equals(ADMINOBJ))
					loginfo = MainRegistry.CASadminlog;
				else
					loginfo = LogFileInfo.BrokerLog_get(CASView.Current_broker);
				fileinfo = null;
				for (int i = 0, n = loginfo.size(); i < n; i++) {
					if (((LogFileInfo) loginfo.get(i)).filename
							.equals(CurrentObj)) {
						fileinfo = (LogFileInfo) loginfo.get(i);
						break;
					}
				}
				if (fileinfo == null)
					this.dispose();
			}
		}
	}

	public void createPartControl(Composite parent) {
		// TODO Auto-generated method stub
		GridLayout gridLayout = new GridLayout();
		gridLayout.horizontalSpacing = 0;
		gridLayout.marginWidth = 0;
		gridLayout.verticalSpacing = 0;
		gridLayout.marginHeight = 0;
		GridData gridData1 = new GridData();
		gridData1.heightHint = 30;
		top = new Composite(parent, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		top.setLayout(gridLayout);
		cLabel = new CLabel(top, SWT.NONE);
		cLabel.setText(CurrentText);
		cLabel.setFont(new Font(Display.getDefault(), cLabel.getFont()
				.toString(), 14, SWT.NORMAL));
		cLabel.setLayoutData(gridData1);
		cLabel.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
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
		GridData gridData = new GridData();
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessVerticalSpace = true;
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		table = new Table(top, SWT.FULL_SELECTION | SWT.BORDER);
		table.setHeaderVisible(true);
		table.setLayoutData(gridData);
		table.setLinesVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 200, true));
		tlayout.addColumnData(new ColumnWeightData(50, 200, true));
		table.setLayout(tlayout);

		TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.PROPERTY"));
		tblColumn = new TableColumn(table, SWT.LEFT);
		tblColumn.setText(Messages.getString("TABLE.VALUE"));

		TableItem item;
		if (!CurrentSelect.equals(ADMINOBJ)) {
			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("TABLE.LOGTYPE"));
			item.setText(1, fileinfo.type);
		}
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.getString("TABLE.FILENAME"));
		item.setText(1, fileinfo.filename);
		if (!MainRegistry.hostOsInfo.equals("NT")) {
			item = new TableItem(table, SWT.NONE);
			item.setText(0, Messages.getString("TABLE.FILEOWNER"));
			item.setText(1, fileinfo.fileowner);
		}
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
}
