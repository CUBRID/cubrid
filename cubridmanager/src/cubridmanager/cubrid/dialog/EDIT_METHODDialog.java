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

package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.view.CubridView;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.CellEditor;
import org.eclipse.jface.viewers.ICellModifier;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.jface.viewers.TextCellEditor;
import org.eclipse.jface.viewers.Viewer;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class EDIT_METHODDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Label label2 = null;
	private Button RADIO_METHOD_EDIT_CATEGORY_CLASS = null;
	private Button RADIO_METHOD_EDIT_CATEGORY_INSTANCE = null;
	private Label label3 = null;
	private Text EDIT_METHOD_EDIT_NAME = null;
	private Label label4 = null;
	private Text EDIT_METHOD_EDIT_RETURN = null;
	private Label label5 = null;
	private Table LIST_METHOD_EDIT_ARGUMENTS = null;
	private Label label6 = null;
	private Text EDIT_METHOD_EDIT_IMPLEMENTATION = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;
	public static final String[] PROPS = new String[15];
	private int oldLastIndex = -1;
	private CLabel cLabel = null;
	private TableItem item = null;

	public EDIT_METHODDialog(Shell parent) {
		super(parent);
	}

	public EDIT_METHODDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal(TableItem ti) {
		item = ti;
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		setinfo();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void createSShell() {
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.EDIT_METHODDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData36 = new org.eclipse.swt.layout.GridData();
		gridData36.horizontalSpan = 2;
		gridData36.widthHint = 240;
		GridData gridData35 = new org.eclipse.swt.layout.GridData();
		gridData35.horizontalSpan = 3;
		GridData gridData34 = new org.eclipse.swt.layout.GridData();
		gridData34.horizontalSpan = 2;
		gridData34.widthHint = 240;
		GridData gridData33 = new org.eclipse.swt.layout.GridData();
		gridData33.horizontalSpan = 2;
		gridData33.widthHint = 240;
		GridLayout gridLayout32 = new GridLayout();
		gridLayout32.numColumns = 3;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 100;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 100;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.grabExcessHorizontalSpace = true;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayoutData(gridData1);
		group1.setLayout(gridLayout32);
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.ISCLASSMEMBER"));
		RADIO_METHOD_EDIT_CATEGORY_CLASS = new Button(group1, SWT.RADIO);
		RADIO_METHOD_EDIT_CATEGORY_CLASS.setText(Messages
				.getString("RADIO.CLASS"));
		RADIO_METHOD_EDIT_CATEGORY_INSTANCE = new Button(group1, SWT.RADIO);
		RADIO_METHOD_EDIT_CATEGORY_INSTANCE.setText(Messages
				.getString("RADIO.INSTANCE"));
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.METHODNAME"));
		EDIT_METHOD_EDIT_NAME = new Text(group1, SWT.BORDER);
		EDIT_METHOD_EDIT_NAME.setLayoutData(gridData33);
		label4 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.RETURNVALUEDOMAIN"));
		EDIT_METHOD_EDIT_RETURN = new Text(group1, SWT.BORDER);
		EDIT_METHOD_EDIT_RETURN.setLayoutData(gridData34);
		label5 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.ARGUMENTDOMAIN"));
		label5.setLayoutData(gridData35);
		createTable1();
		label6 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.FUNCTIONNAME"));
		EDIT_METHOD_EDIT_IMPLEMENTATION = new Text(group1, SWT.BORDER);
		EDIT_METHOD_EDIT_IMPLEMENTATION.setLayoutData(gridData36);
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData2);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData3);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String mname = EDIT_METHOD_EDIT_NAME.getText().trim();
						if (!MainRegistry.isMultibyteSupport && !CommonTool.isAscii(mname)) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INVALIDMETHODNAMENONEASCII"));
							return;
						}
						
						String mret = EDIT_METHOD_EDIT_RETURN.getText().trim();
						String mimpl = EDIT_METHOD_EDIT_IMPLEMENTATION
								.getText().trim();
						if (mname.length() <= 0 || mimpl.length() <= 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INPUTMETHODNAME"));
							return;
						}
						if (mret.equals("void"))
							mret = "";

						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "classname:"
								+ PROPPAGE_CLASS_PAGE3Dialog.si.name + "\n";
						msg += "oldmethodname:" + item.getText(1) + "\n";
						msg += "newmethodname:" + mname + "\n";
						msg += "argument:" + mret + "\n";
						for (int i = 0, newlast = -1; (i < 10 && newlast == -1)
								|| i <= oldLastIndex; i++) {
							if (newlast != -1)
								msg += "argument:" + "" + "\n";
							else {
								TableItem ti = LIST_METHOD_EDIT_ARGUMENTS
										.getItem(i);

								String arg = ti.getText(1).trim();
								if (arg.length() <= 0) {
									newlast = i;
									if (i > oldLastIndex)
										continue;
								} else {
									if (arg.equals("void"))
										arg = "";
								}
								msg += "argument:" + arg + "\n";
							}
						}
						msg += "implementation:" + mimpl + "\n";
						msg += "category:"
								+ ((RADIO_METHOD_EDIT_CATEGORY_INSTANCE
										.getSelection()) ? "instance" : "class");

						ClientSocket cs = new ClientSocket();

						if (!cs.SendBackGround(dlgShell, msg, "updatemethod",
								Messages.getString("WAITING.UPDATEMETHOD"))) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}

						ret = true;
						dlgShell.dispose();
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData4);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	private void createTable1() {
		final TableViewer tv = new TableViewer(group1, SWT.FULL_SELECTION
				| SWT.BORDER);

		LIST_METHOD_EDIT_ARGUMENTS = tv.getTable();
		LIST_METHOD_EDIT_ARGUMENTS
				.setBounds(new org.eclipse.swt.graphics.Rectangle(168, 144,
						240, 176));
		LIST_METHOD_EDIT_ARGUMENTS.setLinesVisible(true);
		LIST_METHOD_EDIT_ARGUMENTS.setHeaderVisible(false);
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		gridData1.widthHint = 240;
		gridData1.heightHint = 176;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST_METHOD_EDIT_ARGUMENTS.setLayoutData(gridData1);

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(30, 30, true));
		tlayout.addColumnData(new ColumnWeightData(100, 100, true));
		LIST_METHOD_EDIT_ARGUMENTS.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_METHOD_EDIT_ARGUMENTS,
				SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NO"));
		tblcol = new TableColumn(LIST_METHOD_EDIT_ARGUMENTS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.TYPE"));

		CellEditor[] editors = new CellEditor[2];
		editors[0] = null;
		editors[1] = new TextCellEditor(LIST_METHOD_EDIT_ARGUMENTS);
		for (int i = 0; i < 2; i++) {
			PROPS[i] = LIST_METHOD_EDIT_ARGUMENTS.getColumn(i).getText();
		}
		tv.setColumnProperties(PROPS);
		tv.setCellEditors(editors);
		tv.setCellModifier(new ArgCellModifier(tv));
	}

	private void setinfo() {
		if (item.getText(0).equals("Yes")) {
			RADIO_METHOD_EDIT_CATEGORY_CLASS.setSelection(true);
			RADIO_METHOD_EDIT_CATEGORY_INSTANCE.setSelection(false);
		} else {
			RADIO_METHOD_EDIT_CATEGORY_CLASS.setSelection(false);
			RADIO_METHOD_EDIT_CATEGORY_INSTANCE.setSelection(true);
		}
		RADIO_METHOD_EDIT_CATEGORY_CLASS.setEnabled(false);
		RADIO_METHOD_EDIT_CATEGORY_INSTANCE.setEnabled(false);
		EDIT_METHOD_EDIT_NAME.setText(item.getText(1));
		EDIT_METHOD_EDIT_RETURN.setText(item.getText(3));
		EDIT_METHOD_EDIT_IMPLEMENTATION.setText(item.getText(5));
		String[] args = item.getText(4).split(", ");
		for (int i = 0, n = args.length; i < 10; i++) {
			TableItem item = new TableItem(LIST_METHOD_EDIT_ARGUMENTS, SWT.NONE);
			item.setText(0, Integer.toString(i + 1));
			if (i < n) {
				item.setText(1, args[i]);
				oldLastIndex = i;
			} else {
				item.setText(1, "");
			}
		}
		for (int i = 0, n = LIST_METHOD_EDIT_ARGUMENTS.getColumnCount(); i < n; i++) {
			LIST_METHOD_EDIT_ARGUMENTS.getColumn(i).pack();
		}
	}
}

class ArgCellModifier implements ICellModifier {
	Table tbl = null;

	public ArgCellModifier(Viewer viewer) {
		tbl = ((TableViewer) viewer).getTable();
	}

	public boolean canModify(Object element, String property) {
		if (property == null)
			return false;
		if (EDIT_METHODDialog.PROPS[0].equals(property))
			return false;
		return true;
	}

	public Object getValue(Object element, String property) {
		TableItem[] tis = tbl.getSelection();
		return tis[0].getText(1);
	}

	public void modify(Object element, String property, Object value) {
		((TableItem) element).setText(1, (String) value);
		((TableItem) element).getParent().redraw();
	}
}
