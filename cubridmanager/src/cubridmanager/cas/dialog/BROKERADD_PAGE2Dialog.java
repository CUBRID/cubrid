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

package cubridmanager.cas.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.SWT;

import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cas.CASItem;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.CellEditor;
import org.eclipse.jface.viewers.ComboBoxCellEditor;
import org.eclipse.jface.viewers.ICellModifier;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.jface.viewers.TextCellEditor;
import org.eclipse.jface.viewers.Viewer;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Label;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class BROKERADD_PAGE2Dialog extends WizardPage {
	public static final String PAGE_NAME = "BROKERADD_PAGE2Dialog";
	private Composite comparent = null;
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	public static Table LIST_BROKERADD_LIST1 = null;
	private Group group2 = null;
	private Label label1 = null;
	private static int oldidx = -1;
	public static final String[] PROPS = new String[2];
	public static final String[] onoffstr = { "ON", "OFF" };

	public static String[] param = { "AUTO_ADD_APPL_SERVER",
			"APPL_SERVER_SHM_ID", "APPL_SERVER_MAX_SIZE", "LOG_DIR",
			"LOG_BACKUP", "SOURCE_ENV", "ACCESS_LOG", "SQL_LOG",
			"SQL_LOG_TIME", "SQL_LOG_MAX_SIZE", "KEEP_CONNECTION",
			"TIME_TO_KILL", "SESSION_TIMEOUT", "JOB_QUEUE_SIZE",
			"MAX_STRING_LENGTH", "STRIPPED_COLUMN_NAME", "SESSION",
			"ACCESS_LIST", "FILE_UPLOAD_TEMP_DIR", "FILE_UPLOAD_DELIMITER", };

	public static String[] paramval = { "ON", "", "10", "log", "OFF",
			"Not Specified", "ON", "OFF", "1000000", "2000000", "AUTO", "60",
			"300", "100", "-1", "ON", "OFF", "Not Specified", "$BROKER/tmp",
			"^" };

	CellEditor[] editors = new CellEditor[3];

	public static boolean[] comboflag = null;
	TableViewer tv = null;
	MultiCellModifier cellact = null;

	public BROKERADD_PAGE2Dialog() {
		super(PAGE_NAME, Messages.getString("TITLE.BROKERADD_PAGE2DIALOG"),
				null);
	}

	public void createControl(Composite parent) {
		comparent = parent;
		createComposite();
		sShell.setParent(parent);
		setControl(sShell);
		setPageComplete(true); // <-last page is false, others true. 
		setinfo();
	}

	public int doModal() {
		createSShell();
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.BROKERADD_PAGE2DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData25 = new org.eclipse.swt.layout.GridData();
		gridData25.heightHint = 100;
		gridData25.grabExcessHorizontalSpace = true;
		gridData25.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData25.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData25.widthHint = 400;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessHorizontalSpace = true;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.grabExcessVerticalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.grabExcessVerticalSpace = true;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
		sShell.setLayout(new GridLayout());
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.ADVANCEDOPTION"));
		createTable1();
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData);
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.DESCRIPTION"));
		group2.setLayout(new GridLayout());
		group2.setLayoutData(gridData1);
		label1 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label1.setText("");
		label1.setLayoutData(gridData25);
		sShell.pack();
	}

	private void createTable1() {
		tv = new TableViewer(group1, SWT.FULL_SELECTION | SWT.BORDER
				| SWT.V_SCROLL);
		LIST_BROKERADD_LIST1 = tv.getTable();
		LIST_BROKERADD_LIST1.setBounds(new org.eclipse.swt.graphics.Rectangle(
				10, 24, 422, 202));
		LIST_BROKERADD_LIST1.setLinesVisible(true);
		LIST_BROKERADD_LIST1.setHeaderVisible(true);
		GridData gridData25 = new org.eclipse.swt.layout.GridData();
		gridData25.grabExcessHorizontalSpace = true;
		gridData25.grabExcessVerticalSpace = true;
		gridData25.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData25.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData25.widthHint = 400;
		gridData25.heightHint = 200;
		LIST_BROKERADD_LIST1.setLayoutData(gridData25);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST_BROKERADD_LIST1.setLayout(tlayout);

		LIST_BROKERADD_LIST1
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int curidx = LIST_BROKERADD_LIST1.getSelectionIndex();
						if (curidx >= 0 && curidx != oldidx) {
							oldidx = curidx;
							TableItem item = (TableItem) e.item;
							if (comboflag[curidx]) {
								editors[1] = editors[2];
								if (item.getText(0).equals("KEEP_CONNECTION"))
									((ComboBoxCellEditor) editors[1])
											.setItems(new String[] { "AUTO",
													"ON", "OFF" });
								else
									((ComboBoxCellEditor) editors[1])
											.setItems(onoffstr);
								tv.setCellEditors(editors);
								tv.setCellModifier(cellact);
							} else {
								editors[1] = editors[0];
								tv.setCellEditors(editors);
								tv.setCellModifier(cellact);
							}
							String lbl = LIST_BROKERADD_LIST1.getItem(curidx)
									.getText(0);
							String tiptxt = "TOOLTIP.PARA_" + lbl;
							label1.setText(Messages.getString(tiptxt));
						}
					}
				});
		TableColumn tblcol = new TableColumn(LIST_BROKERADD_LIST1, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.PARAMETER"));
		tblcol = new TableColumn(LIST_BROKERADD_LIST1, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.VALUE"));

		editors[0] = new TextCellEditor(LIST_BROKERADD_LIST1);
		editors[1] = new ComboBoxCellEditor(LIST_BROKERADD_LIST1, onoffstr,
				SWT.READ_ONLY);
		editors[2] = editors[1];
		for (int i = 0; i < 2; i++) {
			PROPS[i] = LIST_BROKERADD_LIST1.getColumn(i).getText();
		}
		tv.setColumnProperties(PROPS);
		tv.setCellEditors(editors);
		cellact = new MultiCellModifier(tv);
	}

	public void setinfo() {
		comboflag = new boolean[param.length];
		String astype = BROKERADD_PAGE1Dialog.COMBO_BROKER_ADD_ASTYPE.getText();
		LIST_BROKERADD_LIST1.removeAll();
		for (int i = 0, j = 0, n = param.length; i < n; i++) {
			if (astype.equals("CAS")) {
				if (param[i].equals("SESSION")
						|| param[i].equals("FILE_UPLOAD_TEMP_DIR")
						|| param[i].equals("FILE_UPLOAD_DELIMITER")) {
					continue;
				}
			} else if (!astype.equals("WAS")) {
				if (param[i].equals("FILE_UPLOAD_TEMP_DIR")
						|| param[i].equals("FILE_UPLOAD_DELIMITER")) {
					continue;
				}
			}
			TableItem item = new TableItem(LIST_BROKERADD_LIST1, SWT.NONE);
			item.setText(0, param[i]);
			if (item.getText(0).equals("APPL_SERVER_SHM_ID"))
				item.setText(1, Integer.toString(BROKERADD_PAGE1Dialog.SHMID));
			else
				item.setText(1, paramval[i]);
			if (param[i].equals("ACCESS_LOG")
					|| param[i].equals("AUTO_ADD_APPL_SERVER")
					|| param[i].equals("LOG_BACKUP")
					|| param[i].equals("SQL_LOG") || param[i].equals("SESSION")
					|| param[i].equals("STRIPPED_COLUMN_NAME")
					|| param[i].equals("KEEP_CONNECTION")) {
				comboflag[j] = true;
			} else
				comboflag[j] = false;
			j++;
		}
		for (int i = 0, n = LIST_BROKERADD_LIST1.getColumnCount(); i < n; i++) {
			LIST_BROKERADD_LIST1.getColumn(i).pack();
		}
	}
}

class MultiCellModifier implements ICellModifier {
	Table tbl = null;

	TableViewer celltv = null;

	Shell tvshell = null;

	int idx = 0;

	public MultiCellModifier(Viewer viewer) {
		tbl = ((TableViewer) viewer).getTable();
		celltv = (TableViewer) viewer;
		tvshell = tbl.getShell();
	}

	public boolean canModify(Object element, String property) {
		if (property == null)
			return false;
		if (BROKERADD_PAGE2Dialog.PROPS[0].equals(property))
			return false;
		idx = tbl.getSelectionIndex();
		CellEditor[] ce = celltv.getCellEditors();
		if (BROKERADD_PAGE2Dialog.comboflag[idx]) {
			if (ce[1] instanceof TextCellEditor)
				return false;
		} else {
			if (ce[1] instanceof ComboBoxCellEditor)
				return false;
		}
		return true;
	}

	public Object getValue(Object element, String property) {
		idx = tbl.getSelectionIndex();
		TableItem[] tis = tbl.getSelection();
		if (BROKERADD_PAGE2Dialog.comboflag[idx]) {
			if (tis[0].getText(0).equals("KEEP_CONNECTION")) {
				if (tis[0].getText(1).equals("AUTO"))
					return new Integer(0);
				else if (tis[0].getText(1).equals("ON"))
					return new Integer(1);
				else
					return new Integer(2);
			} else
				return (tis[0].getText(1).equals("ON")) ? new Integer(0)
						: new Integer(1);
		} else {
			return tis[0].getText(1);
		}
	}

	public void modify(Object element, String property, Object value) {
		if (value == null)
			return;
		String lbl = ((TableItem) element).getText(0);
		if (BROKERADD_PAGE2Dialog.comboflag[idx]) {
			if (lbl.equals("KEEP_CONNECTION")) {
				int iKeepConnectionValue = ((Integer) value).intValue();
				if (iKeepConnectionValue == 0)
					((TableItem) element).setText(1, "AUTO");
				else if (iKeepConnectionValue == 1)
					((TableItem) element).setText(1, "ON");
				else
					((TableItem) element).setText(1, "OFF");
			} else {
				String yn = (((Integer) value).intValue() == 0) ? "ON" : "OFF";
				((TableItem) element).setText(1, yn);
			}
		} else {
			// check logic
			String chkval = ((String) value).trim();
			if (chkval.length() <= 0 || chkval.equals("Not Specified")) {
				if (lbl.equals("ACCESS_LIST") || lbl.equals("SOURCE_ENV")) {
					((TableItem) element).setText(1, "Not Specified");
				}
				// others skip new value
				return;
			}
			if (lbl.equals("ACCESS_LIST") || lbl.equals("SOURCE_ENV")) {
				for (int i = 0; i < chkval.length(); i++) {
					if (!Character.isLetterOrDigit(chkval.charAt(i))) {
						if (chkval.charAt(i) == '.' || chkval.charAt(i) == '_')
							continue;
						CommonTool.ErrorBox(tvshell, Messages
								.getString("ERROR.INVALIDFILENAME")
								+ " " + lbl);
						((TableItem) element).setText(1, "Not Specified");
						return;
					}
				}
			}
			if (CheckValid(lbl, chkval))
				((TableItem) element).setText(1, (String) value);
		}
		((TableItem) element).getParent().redraw();
	}

	boolean CheckValid(String name, String value) {
		String err = null;
		if (name.equals("BROKER_PORT")) {
			if (IsInteger(value)) {
				int newport = CommonTool.atoi(value);
				CASItem casrec;
				for (int i = 0, n = MainRegistry.CASinfo.size(); i < n; i++) {
					casrec = (CASItem) MainRegistry.CASinfo.get(i);
					if (casrec.broker_port == newport) {
						err = Messages.getString("ERROR.BROKERPORTEXIST");
						break;
					}
				}
				if (err != null)
					return true;
			} else
				err = Messages.getString("ERROR.ISNOTINTEGER");
		} else if (name.equals("MIN_NUM_APPL_SERVER")
				|| name.equals("MAX_NUM_APPL_SERVER")
				|| name.equals("APPL_SERVER_SHM_ID")
				|| name.equals("APPL_SERVER_MAX_SIZE")
				|| name.equals("COMPRESS_SIZE") || name.equals("TIME_TO_KILL")
				|| name.equals("PRIORITY_GAP")) {
			if (IsInteger(value))
				return true;
			else
				err = Messages.getString("ERROR.ISNOTINTEGER");
		} else if (name.equals("SQL_LOG_MAX_SIZE")) {
			int size = CommonTool.atoi(value);
			if (size > 0 && size < 2000001)
				return true;
			else
				err = Messages.getString("ERROR.INVALIDSQLMAXSIZE");
		} else if (name.equals("SESSION_TIMEOUT")
				|| name.equals("SQL_LOG_TIME")
				|| name.equals("MAX_STRING_LENGTH")) {
			if (value.equals("-1"))
				return true;
			if (IsInteger(value))
				return true;
			else
				err = Messages.getString("ERROR.ISNOTINTEGER");
		} else if (name.equals("SOURCE_ENV")) {
			if (!value.endsWith(".env"))
				err = Messages.getString("ERROR.WRONGENVFILENAME");
			else
				return true;
		} else if (name.equals("JOB_QUEUE_SIZE")) {
			if (IsInteger(value)) {
				int var = CommonTool.atoi(value);
				if (var > 127)
					err = Messages.getString("ERROR.JOBQUEUEMAXOVER");
				else
					return true;
			} else
				err = Messages.getString("ERROR.ISNOTINTEGER");
		} else if (name.equals("FILE_UPLOAD_DELIMITER")) {
			if (IsInteger(value))
				err = Messages.getString("ERROR.ISNOTSTRING");
			else
				return true;
		} else if (name.equals("LOG_DIR") || name.equals("APPL_ROOT")
				|| name.equals("FILE_UPLOAD_TEMP_DIR")
				|| name.equals("ACCESS_LIST"))
			return true;

		CommonTool.ErrorBox(tvshell, err);
		return false;
	}

	boolean IsInteger(String str) {
		for (int i = 0; i < str.length(); i++) {
			if (!Character.isDigit(str.charAt(i)))
				return false;
		}
		return true;
	}

}
