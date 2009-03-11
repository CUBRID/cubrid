/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */
package cubridmanager.cubrid.dialog;

import java.text.DateFormat;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;

public class AutoQueryLogDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite dlgComposite = null;
	private Button okButton = null;
	private Button cancelButton = null;
	private Table autoQueryErrorTable = null;
	private Group logsGroup = null;
	private Button refreshButton = null;
	private CLabel cLabel = null;

	public AutoQueryLogDialog(Shell parent) {
		super(parent);
	}

	public AutoQueryLogDialog(Shell parent,int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(okButton);
		dlgShell.open();

		setinfo();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.AUTOQUERY_LOGDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData4.widthHint = 100;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData3.widthHint = 100;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData2.widthHint = 100;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessHorizontalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 4;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.heightHint = -1;
		gridData.widthHint = -1;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		dlgComposite = new Composite(dlgShell, SWT.NONE);
		dlgComposite.setLayout(gridLayout);
		logsGroup = new Group(dlgComposite, SWT.NONE);
		logsGroup.setText(Messages.getString("GROUP.LOGS"));
		logsGroup.setLayout(new GridLayout());
		logsGroup.setLayoutData(gridData);
		createTable();
		cLabel = new CLabel(dlgComposite, SWT.NONE);
		cLabel.setLayoutData(gridData1);
		okButton = new Button(dlgComposite, SWT.NONE);
		okButton.setText(Messages.getString("BUTTON.OK"));
		okButton.setLayoutData(gridData2);
		okButton.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				dlgShell.dispose();
			}
		});
		cancelButton = new Button(dlgComposite, SWT.NONE);
		cancelButton.setText(Messages.getString("BUTTON.CANCEL"));
		cancelButton.setLayoutData(gridData3);
		cancelButton.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				dlgShell.dispose();
			}
		});
		refreshButton = new Button(dlgComposite, SWT.NONE);
		refreshButton.setText(Messages.getString("BUTTON.REFRESH"));
		refreshButton.setLayoutData(gridData4);
		refreshButton.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				setinfo();
			}
		});
		dlgShell.pack();
	}

	private void createTable() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.heightHint = 240;
		gridData5.widthHint = 580;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		autoQueryErrorTable = new Table(logsGroup, SWT.FULL_SELECTION | SWT.BORDER);
		autoQueryErrorTable.setLinesVisible(true);
		autoQueryErrorTable.setLayoutData(gridData5);
		autoQueryErrorTable.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 100, true));
		autoQueryErrorTable.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(autoQueryErrorTable, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.DATABASE"));		
		tblcol = new TableColumn(autoQueryErrorTable, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.CMUSER"));
		tblcol = new TableColumn(autoQueryErrorTable, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.QUERYID"));
		tblcol = new TableColumn(autoQueryErrorTable, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.LOGTIME"));
		tblcol = new TableColumn(autoQueryErrorTable, SWT.RIGHT);
		tblcol.setText(Messages.getString("TABLE.LOGCODE"));
		tblcol = new TableColumn(autoQueryErrorTable, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.LOGDESC"));
	}

	private void setinfo() {
		autoQueryErrorTable.removeAll();
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(dlgShell, "", "getautoexecqueryerrlog", Messages.getString("WAITING.GETTINGLOGINFORM"))) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			return;
		}
		List<Map<String, String>> queryPlanList = new ArrayList<Map<String, String>>();
		for (int i = 0, n = MainRegistry.Tmpchkrst.size(); i < n; i += 6) {
			Map<String, String> queryPlanMap = new HashMap<String, String>();
			queryPlanMap.put("0", (String) MainRegistry.Tmpchkrst.get(i));
			queryPlanMap.put("1", (String) MainRegistry.Tmpchkrst.get(i + 1));
			queryPlanMap.put("2", (String) MainRegistry.Tmpchkrst.get(i + 2));
			queryPlanMap.put("3", (String) MainRegistry.Tmpchkrst.get(i + 3));
			queryPlanMap.put("4", (String) MainRegistry.Tmpchkrst.get(i + 4));
			queryPlanMap.put("5", (String) MainRegistry.Tmpchkrst.get(i + 5));
			queryPlanList.add(queryPlanMap);
		}
		Collections.sort(queryPlanList, new Comparator<Object>() {
			@SuppressWarnings("unchecked")
			public int compare(Object o1, Object o2) {
				Map<String, String> map1 = (Map<String, String>) o1;
				Map<String, String> map2 = (Map<String, String>) o2;
				String str1 = ((String) map1.get("3")).trim();
				String str2 = ((String) map2.get("3")).trim();
				if (str1.length() == 0 && str2.length() > 0) {
					return 1;
				} else if (str1.length() == 0 && str2.length() == 0) {
					return 0;
				} else if (str1.length() > 0 && str2.length() == 0) {
					return -1;
				}
				DateFormat dateFormat = new SimpleDateFormat("yyyy/MM/dd hh:mm:ss");
				try {
					Date date1 = dateFormat.parse(str1);
					Date date2 = dateFormat.parse(str2);
					return date2.compareTo(date1);
				} catch (ParseException e) {
				}
				return 0;
			}
		});
		for (int i = 0; i < queryPlanList.size(); i++) {
			Map<String, String> queryPlanMap = queryPlanList.get(i);
			TableItem item = new TableItem(autoQueryErrorTable, SWT.NONE);
			item.setText(0, queryPlanMap.get("0"));
			item.setText(1, queryPlanMap.get("1"));
			item.setText(2, queryPlanMap.get("2"));
			item.setText(3, queryPlanMap.get("3"));
			String code = queryPlanMap.get("4");
			item.setText(4, code);
			item.setText(5, queryPlanMap.get("5"));

			if (code != null && !code.trim().equals("0")) {
				item.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_GRAY));
			} else {
				item.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_GREEN));
			}
		}

		for (int i = 0, n = autoQueryErrorTable.getColumnCount(); i < n; i++) {
			autoQueryErrorTable.getColumn(i).pack();
		}
	}
}
