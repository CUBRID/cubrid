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

import java.util.ArrayList;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.MainRegistry;
import cubridmanager.cas.CASItem;
import cubridmanager.cubrid.DBAttribute;
import cubridmanager.cubrid.Constraint;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.AutoQuery;
import cubridmanager.cubrid.DBUserInfo;
import cubridmanager.cubrid.Jobs;
import cubridmanager.cubrid.LocalDatabase;
import cubridmanager.cubrid.LogFileInfo;
import cubridmanager.cubrid.SchemaInfo;
import cubridmanager.cubrid.Trigger;
import cubridmanager.cubrid.UserInfo;
import cubridmanager.cubrid.VolumeInfo;
import cubridmanager.cubrid.action.LogViewAction;
import cubridmanager.cubrid.dialog.LoginDialog;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Text;
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
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.layout.*;
import org.eclipse.swt.widgets.*;
import org.eclipse.swt.custom.*;
import org.eclipse.swt.layout.FormLayout;
import org.eclipse.swt.layout.RowLayout;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;

public class ADD_CONSTRAINTDialog extends Dialog {
	private Shell dlgShell = null;  //  @jve:decl-index=0:visual-constraint="10,51"
	private Composite sShell = null;
	private Label label4 = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;
	private Label label = null;
	private TabFolder tabFolder = null;
	private final Color black = new Color(null, 0, 0, 0);
	private final Color gray = new Color(null, 128, 128, 128);
	private final Color white = new Color(null, 255, 255, 255);	
	private Group group1 = null;
	private Label g1_name = null;
	private Text g1_text = null;
	private Label g1_attr = null;
	private Table g1_table = null;
	private Composite g1_composite = null;
	private Button g1_upbtn = null;
	private Button g1_dnbtn = null;
	private Group group2 = null;
	private Group group3 = null;
	private Label g3_name = null;
	private Text g3_text = null;
	private Label g3_text2 = null;
	private Combo g3_combo = null;
	private Label g3_attr = null;
	public static Table g3_table = null;
	private Composite g3_composite = null;
	private Button g3_upbtn = null;
	private Button g3_dnbtn = null;
	
	private String C_UNIQUE = "UNIQUE";
	private String C_INDEX = "INDEX";
	private String C_RUNIQUE = "REVERSE UNIQUE";
	private String C_RINDEX = "REVERSE INDEX";

	public String prikey[] = null; 
	
	public CCombo OldCombo = null;
	private Label g2_name = null;
	private Text g2_text = null;
	private Label g2_target = null;
	private Label g2_targetdef = null;
	private Table g2_table = null;
	private Combo g2_combo = null;
	private Label g2_attr = null;
	private Table g2_table2 = null;
	public ADD_CONSTRAINTDialog(Shell parent) {
		super(parent);
	}

	public ADD_CONSTRAINTDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();
		
		g1_setinfo();
		g2_setinfo();
		g3_setinfo();
		
		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}
	
	private void g2_getTabledata() {
		g2_table.removeAll();
		
		if (OldCombo != null) {
			OldCombo.dispose();
		}
		
		ArrayList sinfo = SchemaInfo.SchemaInfo_get(CubridView.Current_db);
		SchemaInfo si;
	
		for (int ai = 0, an = sinfo.size(); ai < an; ai++) {
			si = (SchemaInfo) sinfo.get(ai);
			if (si.name.equals(g2_combo.getText())) {
				for (int j=0, m = si.constraints.size(); j < m; j++) {
					Constraint elm = (Constraint) si.constraints.get(j);
					if (elm.type.equals("PRIMARY KEY")) {
						int len = elm.attributes.size();
						prikey = new String[len];
						for (int z = 0; z < len; z++)
							prikey[z] = (String) elm.attributes.get(z);
					}
				}
				
				if (prikey != null) {
					for (int i = 0, n = si.attributes.size(); i < n; i++) {
						DBAttribute da = (DBAttribute) si.attributes.get(i);
						for (int j = 0, m = prikey.length; j < m; j++) {
							if (da.name.equals(prikey[j])) {
								TableItem item = new TableItem(g2_table, SWT.NONE);
								item.setText(0, da.name);
								item.setText(1, da.type);	
							}
						}
					}
				}
				break;
			}
		}
		
		for (int i = 0, n = g2_table.getColumnCount(); i < n; i++) {
			g2_table.getColumn(i).pack();
		}

		if (g2_table2.getItemCount() > 0) {
			TableItem items[] = g2_table2.getItems();
			for (int i = 0, n = items.length; i < n; i++)
				items[i].setText(2, "");
		}
	}
	
	private void g1_setinfo() {
		for (int i = 0, n = PROPPAGE_CLASS_PAGE1Dialog.si.attributes.size(); i < n; i++) {
			DBAttribute da = (DBAttribute) PROPPAGE_CLASS_PAGE1Dialog.si.attributes
					.get(i);
			TableItem item = new TableItem(g1_table, SWT.NONE);
			item.setText(1, da.name);
			item.setText(2, da.type);
			item.setForeground(gray);
		}
		
		for (int i = 0, n = g1_table.getColumnCount(); i < n; i++) {
			g1_table.getColumn(i).pack();
		}
		
		g1_setBtnEnable();		
	}
	
	private void g2_setinfo() {
		ArrayList sinfo = SchemaInfo.SchemaInfo_get(CubridView.Current_db);
		SchemaInfo si;
		ClientSocket cs = new ClientSocket();
		
		if (sinfo.size() <= 1) {
			//nothing to do
		} else {
			int idx = 0;
			for (int ai = 0, an = sinfo.size(); ai < an; ai++) {
				si = (SchemaInfo) sinfo.get(ai);
				if (!si.type.equals("system") && 
					si.virtual.equals("normal")) {
					g2_combo.add(si.name, idx++);
					if (!cs.SendClientMessage(dlgShell, "dbname:"
							+ CubridView.Current_db + "\nclassname:" + si.name,
							"class")) {
						CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
					}
				}
			}
			g2_combo.select(0);
		}

		g2_getTabledata();		
		
		for (int i = 0, n = PROPPAGE_CLASS_PAGE1Dialog.si.attributes.size(); i < n; i++) {
			DBAttribute da = (DBAttribute) PROPPAGE_CLASS_PAGE1Dialog.si.attributes
					.get(i);
			TableItem item = new TableItem(g2_table2, SWT.NONE);
			item.setText(0, da.name);
			item.setText(1, da.type);
			item.setText(2, "");
		}
		
		for (int i = 0, n = g2_table2.getColumnCount(); i < n; i++) {
			g2_table2.getColumn(i).pack();
		}
	}

	private void g3_setinfo() {
		g3_text.setEnabled(true);
		int idx = 0;
		g3_combo.add(C_UNIQUE, idx++);
		g3_combo.add(C_INDEX, idx++);
		g3_combo.add(C_RUNIQUE, idx++);
		g3_combo.add(C_RINDEX, idx++);

		g3_combo.select(0);

		for (int i = 0, n = PROPPAGE_CLASS_PAGE1Dialog.si.attributes.size(); i < n; i++) {
			DBAttribute da = (DBAttribute) PROPPAGE_CLASS_PAGE1Dialog.si.attributes
					.get(i);
			TableItem item = new TableItem(g3_table, SWT.NONE);
			item.setText(1, da.name);
			item.setText(2, da.type);
			item.setText(3, "ASC");
			item.setForeground(gray);
		}

		for (int i = 0, n = g3_table.getColumnCount(); i < n-1; i++) {
			g3_table.getColumn(i).pack();
		}

		g3_setBtnEnable();
	}	
	
	private void createSShell() {
/*		dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);*/
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.ADD_CONSTRAINTDIALOG"));
		main_createComposite();

		dlgShell.setLayout(new FillLayout());
		dlgShell.setSize(new org.eclipse.swt.graphics.Point(360,410));
	}

	private void main_createComposite() {
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		createTabFolder();


		label = new Label(sShell, SWT.NONE);
		label.setLayoutData(gridData2);
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 75;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData3);
		IDOK.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				int tabidx = tabFolder.getSelectionIndex();
				if (tabidx == 0) { //PK
					ArrayList selected = new ArrayList();
					for (int i = 0, n = g1_table.getItemCount(); i < n; i++) {
						if (g1_table.getItem(i).getChecked())
							selected.add(g1_table.getItem(i));
					}

					Object[] tis = selected.toArray();
					if (tis.length <= 0) {
						CommonTool.ErrorBox(dlgShell, Messages
								.getString("ERROR.NOATTRSELECTED"));
						return;
					}
					String conname = g1_text.getText().trim();
					String contype = "PRIMARY KEY";

					boolean isclass = false;
					String attrs = "";
					for (int i = 0, n = tis.length; i < n; i++) {
						if (((TableItem) tis[i]).getText(0).equals("Yes"))
							isclass = true;
						attrs += "attribute:"
								+ ((TableItem) tis[i]).getText(1) + "\n";
					}

					if (isclass == true) {
						CommonTool.ErrorBox(dlgShell, Messages
								.getString("ERROR.CONSTRAINTREAUIREINSTANCE"));
						return;
					}

					String msg = "dbname:" + CubridView.Current_db + "\n";
					msg += "classname:"
							+ PROPPAGE_CLASS_PAGE1Dialog.si.name + "\n";
					msg += "type:" + contype + "\n";
					msg += "name:" + conname + "\n";

					msg += "attributecount:" + tis.length + "\n";
					msg += attrs;

					if (isclass)
						msg += "category:class\n";
					else
						msg += "category:instance\n";
					ClientSocket cs = new ClientSocket();

					if (!cs.SendBackGround(dlgShell, msg, "addconstraint",
							Messages.getString("WAITING.ADDCONSTRAINT"))) {
						CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
						return;
					}

					ret = true;
					dlgShell.dispose();
				} else
				if (tabidx == 1) { //FK
					String conname = g2_text.getText().trim();
					String contype = "FOREIGN KEY";

					String attrs = "";
					int cnt = 0;
					for (int i = 0, n = g2_table2.getItemCount(); i < n; i++) {
						if (!g2_table2.getItem(i).getText(2).equals("")) {
							attrs += "forikey:"
								+ g2_table2.getItem(i).getText(0) + "\n";
							attrs += "refkey:"
								+ g2_table2.getItem(i).getText(2) + "\n";
							cnt++;
						}
					}					
		
					if (cnt == 0) {
						CommonTool.ErrorBox(dlgShell, Messages
								.getString("ERROR.NOATTRSELECTED"));
						return;						
					}
							if (conname == null || conname.length() == 0) {
								CommonTool.ErrorBox(dlgShell, Messages
										.getString("ERROR.ADDCONSTRAINT.FK"));
								g2_text.setFocus();
								return;
							}
					String msg = "dbname:" + CubridView.Current_db + "\n";
					msg += "classname:"
							+ PROPPAGE_CLASS_PAGE1Dialog.si.name + "\n";
					msg += "refclsname:"
							+ g2_combo.getText() + "\n";
					
					msg += "type:" + contype + "\n";
					msg += "name:" + conname + "\n";

					msg += "attributecount:" + cnt + "\n";
					msg += attrs;

					msg += "category:instance\n";
					
					ClientSocket cs = new ClientSocket();

					if (!cs.SendBackGround(dlgShell, msg, "addconstraint",
							Messages.getString("WAITING.ADDCONSTRAINT"))) {
						CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
						return;
					}

					ret = true;
					dlgShell.dispose();
				} else
				if (tabidx == 2) { //Index
					ArrayList selected = new ArrayList();
					for (int i = 0, n = g3_table.getItemCount(); i < n; i++) {
						if (g3_table.getItem(i).getChecked())
							selected.add(g3_table.getItem(i));
					}

					Object[] tis = selected.toArray();
					if (tis.length <= 0) {
						CommonTool.ErrorBox(dlgShell, Messages
								.getString("ERROR.NOATTRSELECTED"));
						return;
					}
					String conname = g3_text.getText().trim();
							if (conname == null || conname.length() == 0) {
								CommonTool
										.ErrorBox(
												dlgShell,
												Messages
														.getString("ERROR.ADDCONSTRAINT.INDEX"));
								g3_text.setFocus();
								return;
							}
					String contype = g3_combo.getText();

					boolean isclass = false;
					String attrs = "";
					for (int i = 0, n = tis.length; i < n; i++) {
						if (((TableItem) tis[i]).getText(0).equals("Yes"))
							isclass = true;
						attrs += "attribute:"
							+ ((TableItem) tis[i]).getText(1) + "\n";
						attrs += "attribute_order:"
							+ ((TableItem) tis[i]).getText(3).toLowerCase() + "\n";
					}

					// INDEX, UNIQUE are only instance.
					if (!contype.equals("NOT NULL") && isclass == true) {
						CommonTool.ErrorBox(dlgShell, Messages
								.getString("ERROR.CONSTRAINTREAUIREINSTANCE"));
						return;
					}

					// NOT NULL is select one item only
					if (contype.equals("NOT NULL") && tis.length > 1) {
						CommonTool.ErrorBox(dlgShell, Messages
								.getString("ERROR.NOTNULLREQUIREONEATTR"));
						return;
					}

					String msg = "dbname:" + CubridView.Current_db + "\n";
					msg += "classname:"
							+ PROPPAGE_CLASS_PAGE1Dialog.si.name + "\n";
					msg += "type:" + contype + "\n";
					msg += "name:" + conname + "\n";

					msg += "attributecount:" + tis.length + "\n";
					msg += attrs;

					if (isclass)
						msg += "category:class\n";
					else
						msg += "category:instance\n";
					ClientSocket cs = new ClientSocket();

					if (!cs.SendBackGround(dlgShell, msg, "addconstraint",
							Messages.getString("WAITING.ADDCONSTRAINT"))) {
						CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
						return;
					}

					ret = true;
					dlgShell.dispose();
				}
			}
		});
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 75;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
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
		dlgShell.pack(true);
	}

	/**
	 * This method initializes tabFolder	
	 *
	 */
	private void createTabFolder() {
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.horizontalSpan = 3;
		gridData.grabExcessHorizontalSpace = true;
		gridData.grabExcessVerticalSpace = true;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		tabFolder = new TabFolder(sShell, SWT.NONE);
		tabFolder.setLayoutData(gridData);
		createGroup1();
		createGroup2();
		createGroup3();

		/* code for makes Eclipse happy (null pointer exception avoidance)
		TabItem tabItem = new TabItem(tabFolder, SWT.NONE);
		tabItem.setText("TAB.PRIKEY");
		tabItem.setControl(group1);
		TabItem tabItem3 = new TabItem(tabFolder, SWT.NONE);
		tabItem3.setText("TAB.FORKEY");
		tabItem3.setControl(group2);
		TabItem tabItem5 = new TabItem(tabFolder, SWT.NONE);
		tabItem5.setText("TAB.INDEX");
		tabItem5.setControl(group3);*/
	
		TabItem tabItem = new TabItem(tabFolder, SWT.NONE);
		tabItem.setText(Messages.getString("TAB.PRIKEY"));
		tabItem.setControl(group1);
		TabItem tabItem3 = new TabItem(tabFolder, SWT.NONE);
		tabItem3.setText(Messages.getString("TAB.FORKEY"));
		tabItem3.setControl(group2);
		TabItem tabItem5 = new TabItem(tabFolder, SWT.NONE);
		tabItem5.setText(Messages.getString("TAB.INDEX"));
		tabItem5.setControl(group3);		

	}

	/**
	 * This method initializes group1	
	 *
	 */
	private void createGroup1() {
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.verticalSpan = 1;
		gridData6.horizontalSpan = 2;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.grabExcessHorizontalSpace = true;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 2;
		group1 = new Group(tabFolder, SWT.NONE);
		group1.setLayout(gridLayout2);
		g1_name = new Label(group1, SWT.NONE);
		g1_name.setText(Messages.getString("LABEL.NAME"));
		g1_text = new Text(group1, SWT.BORDER);
		g1_text.setLayoutData(gridData5);
		g1_attr = new Label(group1, SWT.NONE);
		g1_attr.setText(Messages.getString("LABEL.ATTRIBUTESSELECT"));
		g1_attr.setLayoutData(gridData6);
		g1_createtable();

		g1_createcomposite();

	}

	/**
	 * This method initializes g1_table	
	 *
	 */
	private void g1_createtable() {
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData7.grabExcessHorizontalSpace = true;
		gridData7.horizontalSpan = 2;
		gridData7.verticalSpan = 2;
		gridData7.grabExcessVerticalSpace = true;
		gridData7.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		g1_table = new Table(group1, SWT.FULL_SELECTION
				| SWT.BORDER | SWT.SINGLE | SWT.CHECK);
		g1_table.setHeaderVisible(true);
		g1_table.setLayoutData(gridData7);
		g1_table.setLinesVisible(true);

		TableColumn tblcol = new TableColumn(g1_table, SWT.LEFT);
		tblcol.setText(Messages.getString("LABEL.USETHISCOLUMN"));
		tblcol = new TableColumn(g1_table, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NAME"));
		tblcol = new TableColumn(g1_table, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.DOMAIN"));

		g1_table.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (e.detail == SWT.CHECK) {
					TableItem item = (TableItem) e.item;
					if (item.getChecked())
						item.setForeground(black);
					else
						item.setForeground(gray);
					g1_table.setSelection(new TableItem[] { item });
				}
				g1_setBtnEnable();
			}
		});		
	}

	private void g1_setBtnEnable() {
		if (g1_table.getSelectionCount() > 0) {
			g1_dnbtn.setEnabled(true);
			g1_upbtn.setEnabled(true);
		} else {
			g1_dnbtn.setEnabled(false);
			g1_upbtn.setEnabled(false);
		}

		if (g1_table.getSelectionIndex() <= 0)
			g1_upbtn.setEnabled(false);

		if (g1_table.getSelectionIndex() >= g1_table.getItemCount() - 1)
			g1_dnbtn.setEnabled(false);
	}	
	
	/**
	 * This method initializes g1_composite	
	 *
	 */
	private void g1_createcomposite() {
		GridData gridData10 = new org.eclipse.swt.layout.GridData();
		gridData10.grabExcessHorizontalSpace = true;
		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData9.grabExcessHorizontalSpace = true;
		gridData9.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout3 = new GridLayout();
		gridLayout3.numColumns = 2;
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData8.horizontalSpan = 2;
		gridData8.grabExcessHorizontalSpace = true;
		gridData8.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		g1_composite = new Composite(group1, SWT.NONE);
		g1_composite.setLayoutData(gridData8);
		g1_composite.setLayout(gridLayout3);
		g1_upbtn = new Button(g1_composite, SWT.NONE);
		g1_upbtn.setText(Messages.getString("BUTTON.UP"));
		g1_upbtn.setLayoutData(gridData9);
		g1_upbtn.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (g1_table.getSelectionCount() < 1)
					return;

				int selectionIndex = g1_table.getSelectionIndex();
				if (selectionIndex == 0)
					return;

				boolean tmpCheck;
				String tmpName, tmpDomain;
				Color tmpColor;
				TableItem selectedItem = g1_table.getSelection()[0];
				TableItem targetItem = g1_table.getItem(selectionIndex - 1);
				tmpCheck = targetItem.getChecked();
				tmpName = targetItem.getText(1);
				tmpDomain = targetItem.getText(2);
				tmpColor = targetItem.getForeground();
				targetItem.setChecked(selectedItem.getChecked());
				targetItem.setText(1, selectedItem.getText(1));
				targetItem.setText(2, selectedItem.getText(2));
				targetItem.setForeground(selectedItem.getForeground());
				selectedItem.setChecked(tmpCheck);
				selectedItem.setText(1, tmpName);
				selectedItem.setText(2, tmpDomain);
				selectedItem.setForeground(tmpColor);
				g1_table.setSelection(selectionIndex - 1);
				g1_setBtnEnable();
			}
		});
		g1_dnbtn = new Button(g1_composite, SWT.NONE);
		g1_dnbtn.setText(Messages.getString("BUTTON.DOWN"));
		g1_dnbtn.setLayoutData(gridData10);
		g1_dnbtn.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (g1_table.getSelectionCount() < 1)
					return;

				int selectionIndex = g1_table.getSelectionIndex();
				if (selectionIndex == g1_table.getItemCount() - 1)
					return;

				boolean tmpCheck;
				String tmpName, tmpDomain;
				Color tmpColor;
				TableItem selectedItem = g1_table.getSelection()[0];
				TableItem targetItem = g1_table.getItem(selectionIndex + 1);
				tmpCheck = targetItem.getChecked();
				tmpName = targetItem.getText(1);
				tmpDomain = targetItem.getText(2);
				tmpColor = targetItem.getForeground();
				targetItem.setChecked(selectedItem.getChecked());
				targetItem.setText(1, selectedItem.getText(1));
				targetItem.setText(2, selectedItem.getText(2));
				targetItem.setForeground(selectedItem.getForeground());
				selectedItem.setChecked(tmpCheck);
				selectedItem.setText(1, tmpName);
				selectedItem.setText(2, tmpDomain);
				selectedItem.setForeground(tmpColor);
				g1_table.setSelection(selectionIndex + 1);
				g1_setBtnEnable();
			}
		});
	}

	/**
	 * This method initializes group2	
	 * TODO
	 */
	private void createGroup2() {
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData21.horizontalSpan = 2;
		gridData21.grabExcessHorizontalSpace = true;
		gridData21.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData19 = new org.eclipse.swt.layout.GridData();
		gridData19.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData19.grabExcessHorizontalSpace = true;
		gridData19.horizontalSpan = 2;
		gridData19.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData17 = new org.eclipse.swt.layout.GridData();
		gridData17.grabExcessHorizontalSpace = true;
		gridData17.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData17.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 2;
		group2 = new Group(tabFolder, SWT.NONE);
		group2.setLayout(gridLayout1);
		g2_name = new Label(group2, SWT.NONE);
		g2_name.setText(Messages.getString("LABEL.NAME"));
		g2_text = new Text(group2, SWT.BORDER);
		g2_text.setLayoutData(gridData17);
		g2_target = new Label(group2, SWT.NONE);
		g2_target.setText(Messages.getString("LABEL.TARGETCLASS"));
		createG2_combo();
		g2_targetdef = new Label(group2, SWT.NONE);
		g2_targetdef.setText(Messages.getString("LABEL.TARGETPKEY"));
		g2_targetdef.setLayoutData(gridData19);
		createG2_table();
		g2_attr = new Label(group2, SWT.NONE);
		g2_attr.setText(Messages.getString("LABEL.ATTRIBUTESSELECT"));
		g2_attr.setLayoutData(gridData21);
		createG2_table2();
	}

	/**
	 * This method initializes group3	
	 *
	 */
	private void createGroup3() {
		GridData gridData13 = new org.eclipse.swt.layout.GridData();
		gridData13.horizontalSpan = 2;
		gridData13.verticalSpan = 1;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.grabExcessHorizontalSpace = true;
		gridData11.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout4 = new GridLayout();
		gridLayout4.numColumns = 2;
		group3 = new Group(tabFolder, SWT.NONE);
		group3.setLayout(gridLayout4);
		g3_name = new Label(group3, SWT.NONE);
		g3_name.setText(Messages.getString("LABEL.NAME"));
		g3_text = new Text(group3, SWT.BORDER);
		g3_text.setLayoutData(gridData11);
		g3_text2 = new Label(group3, SWT.NONE);
		g3_text2.setText(Messages.getString("LABEL.INDEXTYPE"));
		g3_create_combo();

		g3_attr = new Label(group3, SWT.NONE);
		g3_attr.setText(Messages.getString("LABEL.ATTRIBUTESSELECT"));
		g3_attr.setLayoutData(gridData13);
		g3_create_table();

		g3_create_composite();

	}

	/**
	 * This method initializes g3_combo	
	 *
	 */
	private void g3_create_combo() {
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData12.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		g3_combo = new Combo(group3, SWT.NONE | SWT.READ_ONLY);
		g3_combo.setLayoutData(gridData12);
	}

	/**
	 * This method initializes g3_table	
	 *
	 */
	private void g3_create_table() {
		GridData gridData14 = new org.eclipse.swt.layout.GridData();
		gridData14.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData14.grabExcessHorizontalSpace = true;
		gridData14.horizontalSpan = 2;
		gridData14.grabExcessVerticalSpace = true;
		gridData14.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		//tv = new TableViewer(group3, SWT.FULL_SELECTION | SWT.BORDER | SWT.SINGLE | SWT.CHECK);
		
		g3_table = new Table(group3, SWT.FULL_SELECTION | SWT.BORDER | SWT.SINGLE | SWT.CHECK);
		//g3_table = tv.getTable();
		g3_table.setHeaderVisible(true);
		g3_table.setLayoutData(gridData14);
		g3_table.setLinesVisible(true);
		g3_table.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				final TableItem item = (TableItem) e.item;
				if (e.detail == SWT.CHECK) {
					if (item.getChecked())
						item.setForeground(black);
					else
						item.setForeground(gray);
					g3_table.setSelection(new TableItem[] { item });
				}
				if (OldCombo != null) {
					OldCombo.dispose();
				}
				if (g3_combo.getText().equals(C_INDEX) ||
					g3_combo.getText().equals(C_UNIQUE)) {
					final CCombo combo = new CCombo (g3_table, SWT.NONE);
					TableEditor editor = new TableEditor (g3_table);
					combo.setEditable(false);
					combo.setBackground(white);					
					combo.add("ASC");
					combo.add("DESC");
					editor.grabHorizontal = true;
					editor.setEditor(combo, item, 3);
					if (item.getText(3).equals("ASC")) {
						combo.select(0);
					} else {
						combo.select(1);
					}
					combo.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
						public void widgetSelected(org.eclipse.swt.events.SelectionEvent event) {
							item.setText(3, combo.getText());
							//combo.dispose();
						}
					});
					OldCombo = combo;
				}
				g3_setBtnEnable();
			}
		});
		TableColumn tblcol = new TableColumn(g3_table, SWT.LEFT);
		tblcol.setText(Messages.getString("LABEL.USETHISCOLUMN"));
		tblcol = new TableColumn(g3_table, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NAME"));
		tblcol = new TableColumn(g3_table, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.DOMAIN"));		
		TableColumn tblcombocol = new TableColumn(g3_table,SWT.LEFT);
		tblcombocol.setWidth(100);
		tblcombocol.setText(Messages.getString("TABLE.ORDER"));
	}

	private void g3_setBtnEnable() {
		if (g3_table.getSelectionCount() > 0) {
			g3_dnbtn.setEnabled(true);
			g3_upbtn.setEnabled(true);
		} else {
			g3_dnbtn.setEnabled(false);
			g3_upbtn.setEnabled(false);
		}

		if (g3_table.getSelectionIndex() <= 0)
			g3_upbtn.setEnabled(false);

		if (g3_table.getSelectionIndex() >= g3_table.getItemCount() - 1)
			g3_dnbtn.setEnabled(false);
	}
	
	/**
	 * This method initializes g3_composite	
	 *
	 */
	private void g3_create_composite() {
		GridData gridData16 = new org.eclipse.swt.layout.GridData();
		gridData16.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData16.grabExcessHorizontalSpace = true;
		gridData16.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessHorizontalSpace = true;
		GridLayout gridLayout5 = new GridLayout();
		gridLayout5.numColumns = 2;
		GridData gridData15 = new org.eclipse.swt.layout.GridData();
		gridData15.horizontalSpan = 2;
		gridData15.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData15.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData15.grabExcessHorizontalSpace = true;
		g3_composite = new Composite(group3, SWT.NONE);
		g3_composite.setLayoutData(gridData15);
		g3_composite.setLayout(gridLayout5);
		g3_upbtn = new Button(g3_composite, SWT.NONE);
		g3_upbtn.setText(Messages.getString("BUTTON.UP"));
		g3_upbtn.setLayoutData(gridData16);
		g3_upbtn.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (OldCombo != null) {
					OldCombo.dispose();
				}
				
				if (g3_table.getSelectionCount() < 1)
					return;

				int selectionIndex = g3_table.getSelectionIndex();
				if (selectionIndex == 0)
					return;

				boolean tmpCheck;
				String tmpName, tmpDomain, tmpOrder;
				Color tmpColor;
				TableItem selectedItem = g3_table.getSelection()[0];
				TableItem targetItem = g3_table.getItem(selectionIndex - 1);
				tmpCheck = targetItem.getChecked();
				tmpName = targetItem.getText(1);
				tmpDomain = targetItem.getText(2);
				tmpOrder = targetItem.getText(3);
				tmpColor = targetItem.getForeground();
				targetItem.setChecked(selectedItem.getChecked());
				targetItem.setText(1, selectedItem.getText(1));
				targetItem.setText(2, selectedItem.getText(2));
				targetItem.setText(3, selectedItem.getText(3));
				targetItem.setForeground(selectedItem.getForeground());
				selectedItem.setChecked(tmpCheck);
				selectedItem.setText(1, tmpName);
				selectedItem.setText(2, tmpDomain);
				selectedItem.setText(3, tmpOrder);
				selectedItem.setForeground(tmpColor);
				g3_table.setSelection(selectionIndex - 1);
				g3_setBtnEnable();
			}
		});
		g3_dnbtn = new Button(g3_composite, SWT.NONE);
		g3_dnbtn.setText(Messages.getString("BUTTON.DOWN"));
		g3_dnbtn.setLayoutData(gridData1);
		g3_dnbtn.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (OldCombo != null) {
					OldCombo.dispose();
				}
				
				if (g3_table.getSelectionCount() < 1)
					return;

				int selectionIndex = g3_table.getSelectionIndex();
				if (selectionIndex == g3_table.getItemCount() - 1)
					return;

				boolean tmpCheck;
				String tmpName, tmpDomain, tmpOrder;
				Color tmpColor;
				TableItem selectedItem = g3_table.getSelection()[0];
				TableItem targetItem = g3_table.getItem(selectionIndex + 1);
				tmpCheck = targetItem.getChecked();
				tmpName = targetItem.getText(1);
				tmpDomain = targetItem.getText(2);
				tmpOrder = targetItem.getText(3);
				tmpColor = targetItem.getForeground();
				targetItem.setChecked(selectedItem.getChecked());
				targetItem.setText(1, selectedItem.getText(1));
				targetItem.setText(2, selectedItem.getText(2));
				targetItem.setText(3, selectedItem.getText(3));			
				targetItem.setForeground(selectedItem.getForeground());
				selectedItem.setChecked(tmpCheck);
				selectedItem.setText(1, tmpName);
				selectedItem.setText(2, tmpDomain);
				selectedItem.setText(3, tmpOrder);
				selectedItem.setForeground(tmpColor);
				g3_table.setSelection(selectionIndex + 1);
				g3_setBtnEnable();
			}
		});
	}

	/**
	 * This method initializes g2_table	
	 * TODO
	 */
	private void createG2_table() {
		GridData gridData20 = new org.eclipse.swt.layout.GridData();
		gridData20.horizontalSpan = 2;
		gridData20.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData20.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData20.verticalSpan = 7;
		gridData20.grabExcessVerticalSpace = true;
		gridData20.grabExcessHorizontalSpace = true;
		g2_table = new Table(group2, SWT.FULL_SELECTION | SWT.BORDER | SWT.SINGLE);
		g2_table.setHeaderVisible(true);
		g2_table.setLayoutData(gridData20);
		g2_table.setLinesVisible(true);
		g2_table.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				System.out.println("widgetSelected()"); // TODO Auto-generated Event stub widgetSelected()
			}
		});

		TableColumn tblcol = new TableColumn(g2_table, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NAME"));
		tblcol = new TableColumn(g2_table, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.DOMAIN"));
	}

	/**
	 * This method initializes g2_combo	
	 *
	 */
	private void createG2_combo() {
		GridData gridData18 = new org.eclipse.swt.layout.GridData();
		gridData18.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData18.grabExcessHorizontalSpace = true;
		gridData18.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		g2_combo = new Combo(group2, SWT.NONE | SWT.READ_ONLY);
		g2_combo.setLayoutData(gridData18);
		g2_combo.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				g2_getTabledata();
			}
			public void widgetDefaultSelected(org.eclipse.swt.events.SelectionEvent e) {
			}
		});
	}

	/**
	 * This method initializes g2_table2	
	 *
	 */
	private void createG2_table2() {
		GridData gridData22 = new org.eclipse.swt.layout.GridData();
		gridData22.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData22.grabExcessHorizontalSpace = true;
		gridData22.horizontalSpan = 2;
		gridData22.grabExcessVerticalSpace = true;
		gridData22.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		g2_table2 = new Table(group2, SWT.FULL_SELECTION | SWT.BORDER | SWT.SINGLE);
		g2_table2.setHeaderVisible(true);
		g2_table2.setLayoutData(gridData22);
		g2_table2.setLinesVisible(true);
		g2_table2.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				final TableItem item = (TableItem) e.item;
				if (OldCombo != null) {
					OldCombo.dispose();
				}
				final CCombo combo = new CCombo (g2_table2, SWT.NONE);
				TableEditor editor = new TableEditor (g2_table2);
				combo.setEditable(false);
				combo.setBackground(white);
				combo.add("");
				if(g2_table.getItemCount() != 0)
					for (int i = 0, n = prikey.length; i < n; i ++)
						combo.add(prikey[i]);
				editor.grabHorizontal = true;
				editor.setEditor(combo, item, 2);
				combo.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(org.eclipse.swt.events.SelectionEvent event) {
						item.setText(2, combo.getText());
						//combo.dispose();
					}
				});
				OldCombo = combo;
			}
		});
		TableColumn tblcol = new TableColumn(g2_table2, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NAME"));
		tblcol = new TableColumn(g2_table2, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.DOMAIN"));		
		TableColumn tblcombocol = new TableColumn(g2_table2,SWT.LEFT);
		tblcombocol.setWidth(100);
		tblcombocol.setText(Messages.getString("TABLE.TARGETNAME"));
	}

}
