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
import cubridmanager.cubrid.*;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.TabFolder;
import cubridmanager.cubrid.action.TablePropertyAction;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.SchemaInfo;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class PROPPAGE_CLASS_PAGE2Dialog extends Dialog {
	private Shell dlgShell = null;
	private Composite comparent = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Text EDIT_CLASS_SUBCLASSES = null;
	private Label label2 = null;
	private Table LIST_CLASS_SUPERCLASSES = null;
	private Button BUTTON_CLASS_SUPERCLASS_ADD = null;
	private Button BUTTON_CLASS_SUPERCLASS_DELETE = null;
	private Label label3 = null;
	private Table LIST_CLASS_RESOLUTIONS = null;
	private Button BUTTON_CLASS_RESOLUTION_ADD = null;
	private Button BUTTON_CLASS_RESOLUTION_DELETE = null;
	public static SchemaInfo si = null;

	public PROPPAGE_CLASS_PAGE2Dialog(Shell parent) {
		super(parent);
	}

	public PROPPAGE_CLASS_PAGE2Dialog(Shell parent, int style) {
		super(parent, style);
	}

	public Composite SetTabPart(TabFolder parent) {
		comparent = parent;
		createComposite();
		setinfo();
		sShell.setParent(parent);
		return sShell;
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
		dlgShell
				.setText(Messages.getString("TITLE.PROPPAGE_CLASS_PAGE2DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.widthHint = 90;
		gridData9.grabExcessHorizontalSpace = true;
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData8.widthHint = 90;
		gridData8.grabExcessHorizontalSpace = true;
		gridData8.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.horizontalSpan = 2;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.widthHint = 90;
		gridData5.grabExcessHorizontalSpace = true;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 90;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData4.grabExcessHorizontalSpace = true;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 2;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		gridData1.widthHint = 550;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
		sShell.setLayout(gridLayout);
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.SUBCLASSES"));
		label1.setLayoutData(gridData);
		EDIT_CLASS_SUBCLASSES = new Text(sShell, SWT.BORDER);
		EDIT_CLASS_SUBCLASSES.setEditable(false);
		EDIT_CLASS_SUBCLASSES.setLayoutData(gridData1);
		label2 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.SUPERCLASSES"));
		label2.setLayoutData(gridData2);
		createTable1();
		BUTTON_CLASS_SUPERCLASS_ADD = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_SUPERCLASS_ADD.setText(Messages.getString("BUTTON.ADD1"));
		BUTTON_CLASS_SUPERCLASS_ADD.setLayoutData(gridData4);
		BUTTON_CLASS_SUPERCLASS_ADD
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ADD_SUPERDialog dlg = new ADD_SUPERDialog(sShell
								.getShell());
						if (dlg.doModal()) {
							setinfo();
						}
					}
				});
		BUTTON_CLASS_SUPERCLASS_DELETE = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_SUPERCLASS_DELETE.setText(Messages
				.getString("BUTTON.DELETE"));
		BUTTON_CLASS_SUPERCLASS_DELETE.setLayoutData(gridData5);
		BUTTON_CLASS_SUPERCLASS_DELETE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (LIST_CLASS_SUPERCLASSES.getSelectionCount() < 1)
							return;
						String col0 = LIST_CLASS_SUPERCLASSES.getSelection()[0]
								.getText(0);
						if (CommonTool.WarnYesNo(sShell.getShell(), Messages
								.getString("WARNYESNO.DELETE")
								+ " " + col0) != SWT.YES)
							return;
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "classname:" + si.name + "\n";
						msg += "super:" + col0;
						ClientSocket cs = new ClientSocket();

						if (!cs.SendBackGround(sShell.getShell(), msg,
								"dropsuper", Messages
										.getString("WAITING.DROPSUPER"))) {
							CommonTool.ErrorBox(sShell.getShell(), cs.ErrorMsg);
							return;
						}
						setinfo();
					}
				});
		label3 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.RESOLUTIONS"));
		label3.setLayoutData(gridData6);
		createTable2();
		BUTTON_CLASS_RESOLUTION_ADD = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_RESOLUTION_ADD.setText(Messages.getString("BUTTON.ADD1"));
		BUTTON_CLASS_RESOLUTION_ADD.setLayoutData(gridData8);
		BUTTON_CLASS_RESOLUTION_ADD
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "classname:" + si.name;
						ClientSocket cs = new ClientSocket();

						if (!cs.SendClientMessage(sShell.getShell(), msg,
								"getsuperclassesinfo")) {
							CommonTool.ErrorBox(sShell.getShell(), cs.ErrorMsg);
							return;
						}

						if (MainRegistry.Tmpchkrst.size() < 1)
							return;

						ADD_RESOLUTIONDialog dlg = new ADD_RESOLUTIONDialog(
								sShell.getShell());
						if (dlg.doModal()) {
							setinfo();
						}
					}
				});
		BUTTON_CLASS_RESOLUTION_DELETE = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_RESOLUTION_DELETE.setText(Messages
				.getString("BUTTON.DELETE"));
		BUTTON_CLASS_RESOLUTION_DELETE.setLayoutData(gridData9);
		BUTTON_CLASS_RESOLUTION_DELETE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (LIST_CLASS_RESOLUTIONS.getSelectionCount() < 1)
							return;
						String col0 = LIST_CLASS_RESOLUTIONS.getSelection()[0]
								.getText(0);
						String col1 = LIST_CLASS_RESOLUTIONS.getSelection()[0]
								.getText(1);
						String col2 = LIST_CLASS_RESOLUTIONS.getSelection()[0]
								.getText(2);
						if (CommonTool.WarnYesNo(sShell.getShell(), Messages
								.getString("WARNYESNO.DELETE")
								+ " " + col1) != SWT.YES)
							return;
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "classname:" + si.name + "\n";
						msg += "super:" + col2 + "\n";
						msg += "name:" + col1 + "\n";
						msg += "category:"
								+ ((col0.equals("Yes") ? "class" : "instance"));

						ClientSocket cs = new ClientSocket();
						if (!cs.SendBackGround(sShell.getShell(), msg,
								"dropresolution", Messages
										.getString("WAITING.DROPRESOLUTION"))) {
							CommonTool.ErrorBox(sShell.getShell(), cs.ErrorMsg);
							return;
						}
						setinfo();
					}
				});
		sShell.pack();
	}

	private void createTable1() {
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalSpan = 2;
		gridData3.grabExcessVerticalSpace = true;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.heightHint = 100;
		gridData3.widthHint = 550;
		gridData3.grabExcessHorizontalSpace = true;
		LIST_CLASS_SUPERCLASSES = new Table(sShell, SWT.FULL_SELECTION
				| SWT.SINGLE | SWT.BORDER);
		LIST_CLASS_SUPERCLASSES.setLinesVisible(true);
		LIST_CLASS_SUPERCLASSES.setLayoutData(gridData3);
		LIST_CLASS_SUPERCLASSES.setHeaderVisible(false);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(100, 30, true));
		LIST_CLASS_SUPERCLASSES.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_CLASS_SUPERCLASSES, SWT.LEFT);
		tblcol.setText("col1");
		LIST_CLASS_SUPERCLASSES
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setBtnEnable();
					}
				});
	}

	private void createTable2() {
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.grabExcessHorizontalSpace = true;
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData7.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData7.horizontalSpan = 2;
		gridData7.heightHint = 130;
		gridData7.widthHint = 550;
		gridData7.grabExcessVerticalSpace = true;
		LIST_CLASS_RESOLUTIONS = new Table(sShell, SWT.FULL_SELECTION
				| SWT.SINGLE | SWT.BORDER);
		LIST_CLASS_RESOLUTIONS.setLinesVisible(true);
		LIST_CLASS_RESOLUTIONS.setLayoutData(gridData7);
		LIST_CLASS_RESOLUTIONS.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(25, 50, true));
		tlayout.addColumnData(new ColumnWeightData(25, 50, true));
		tlayout.addColumnData(new ColumnWeightData(25, 50, true));
		tlayout.addColumnData(new ColumnWeightData(25, 50, true));
		LIST_CLASS_RESOLUTIONS.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_CLASS_RESOLUTIONS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.ISCLASSMEMBER"));
		tblcol = new TableColumn(LIST_CLASS_RESOLUTIONS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.SUPERCLASS"));
		tblcol = new TableColumn(LIST_CLASS_RESOLUTIONS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.MEMBERNAME"));
		tblcol = new TableColumn(LIST_CLASS_RESOLUTIONS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.ALIAS"));
		LIST_CLASS_RESOLUTIONS
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setBtnEnable();
					}
				});
	}

	private void setinfo() {
		si = TablePropertyAction.si;
		EDIT_CLASS_SUBCLASSES.setToolTipText(Messages
				.getString("TOOLTIP.CLASSEDITSUBCLASSES"));
		LIST_CLASS_SUPERCLASSES.setToolTipText(Messages
				.getString("TOOLTIP.CLASSLISTSUPERCLASSES"));
		LIST_CLASS_RESOLUTIONS.setToolTipText(Messages
				.getString("TOOLTIP.CLASSLISTRESOLUTIONS"));
		EDIT_CLASS_SUBCLASSES.setText(CommonTool.ArrayToString(si.subClasses));

		LIST_CLASS_SUPERCLASSES.removeAll();
		for (int i = 0, n = si.superClasses.size(); i < n; i++) {
			String rec = (String) si.superClasses.get(i);
			TableItem item = new TableItem(LIST_CLASS_SUPERCLASSES, SWT.NONE);
			item.setText(0, rec);
		}

		LIST_CLASS_RESOLUTIONS.removeAll();
		for (int i = 0, n = si.classResolutions.size(); i < n; i++) {
			DBResolution rec = (DBResolution) si.classResolutions.get(i);
			TableItem item = new TableItem(LIST_CLASS_RESOLUTIONS, SWT.NONE);
			item.setText(0, Messages.getString("RADIO.CLASS"));
			item.setText(1, rec.className);
			item.setText(2, rec.name);
			item.setText(3, rec.alias);
		}
		for (int i = 0, n = si.resolutions.size(); i < n; i++) {
			DBResolution rec = (DBResolution) si.resolutions.get(i);
			TableItem item = new TableItem(LIST_CLASS_RESOLUTIONS, SWT.NONE);
			item.setText(0, Messages.getString("RADIO.INSTANCE"));
			item.setText(1, rec.className);
			item.setText(2, rec.name);
			item.setText(3, rec.alias);
		}
		if (si.type.equals("system")) {
			BUTTON_CLASS_SUPERCLASS_ADD.setEnabled(false);
			BUTTON_CLASS_SUPERCLASS_DELETE.setEnabled(false);
			BUTTON_CLASS_RESOLUTION_ADD.setEnabled(false);
			BUTTON_CLASS_RESOLUTION_DELETE.setEnabled(false);
		}

		setBtnEnable();
	}

	private void setBtnEnable() {
		if (si.type.equals("system"))
			return;
		if (LIST_CLASS_SUPERCLASSES.getSelectionCount() > 0)
			BUTTON_CLASS_SUPERCLASS_DELETE.setEnabled(true);
		else
			BUTTON_CLASS_SUPERCLASS_DELETE.setEnabled(false);

		if (LIST_CLASS_RESOLUTIONS.getSelectionCount() > 0)
			BUTTON_CLASS_RESOLUTION_DELETE.setEnabled(true);
		else
			BUTTON_CLASS_RESOLUTION_DELETE.setEnabled(false);
	}
}
