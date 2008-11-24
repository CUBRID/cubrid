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
import org.eclipse.swt.SWT;

import java.text.NumberFormat;
import java.util.ArrayList;
import cubridmanager.MainRegistry;
import cubridmanager.cubrid.AddVols;
import cubridmanager.Messages;
import cubridmanager.CommonTool;
import cubridmanager.cubrid.action.CreateAction;

import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Listener;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class CREATEDB_PAGE2Dialog extends WizardPage {
	public static final String PAGE_NAME = "CREATEDB_PAGE2Dialog";
	private Composite comparent = null;
	private String CurrentVol = null;
	private Shell dlgShell = null; // @jve:decl-index=0:visual-constraint="10,61"
	private Composite sShell = null;
	private Group group1 = null;
	// private Button CHECK1 = null;
	private Label label1 = null;
	public Text EDIT_CREATEDB_EXVOLNAME = null;
	private Label label2 = null;
	public Text EDIT_CREATEDB_EXVOLPATH = null;
	private Label label3 = null;
	public Combo COMBO_CREATEDB_EXPURPOSE = null;
	private Label label4 = null;
	public Text EDIT_CREATEDB_EXPAGESIZE = null;
	private Button BUTTON_CREATEDB_ADDVOL = null;
	private Button BUTTON_CREATEDB_DELETEVOL = null;
	private Label label5 = null;
	private Table LIST_CREATEDB_EXVOL = null;
	public ArrayList addvols = new ArrayList();
	private Label label = null;

	public CREATEDB_PAGE2Dialog() {
		super(PAGE_NAME, Messages.getString("PAGE.CREATEDBPAGE2"), null);
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
		dlgShell.setText(Messages.getString("TITLE.CREATEDB_PAGE2DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		sShell = new Composite(comparent, SWT.NONE);
		GridData gridData65 = new org.eclipse.swt.layout.GridData();
		gridData65.widthHint = 90;
		gridData65.grabExcessVerticalSpace = true;
		gridData65.grabExcessHorizontalSpace = true;
		GridData gridData64 = new org.eclipse.swt.layout.GridData();
		gridData64.horizontalSpan = 3;
		gridData64.grabExcessVerticalSpace = true;
		gridData64.grabExcessHorizontalSpace = true;
		gridData64.widthHint = 240;
		GridData gridData63 = new org.eclipse.swt.layout.GridData();
		gridData63.horizontalSpan = 3;
		gridData63.grabExcessHorizontalSpace = true;
		gridData63.grabExcessVerticalSpace = true;
		// GridData gridData62 = new org.eclipse.swt.layout.GridData();
		// gridData62.horizontalSpan = 4;
		// gridData62.grabExcessVerticalSpace = true;
		GridLayout gridLayout61 = new GridLayout();
		gridLayout61.numColumns = 4;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalSpan = 3;
		gridData4.grabExcessVerticalSpace = true;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 110;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 110;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessHorizontalSpace = true;
		gridData1.grabExcessVerticalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 3;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.NEWADDITIONAL"));
		group1.setLayout(gridLayout61);
		group1.setLayoutData(gridData);
		// CHECK1 = new Button(group1, SWT.CHECK);
		// CHECK1.setText(Messages.getString("CHECK.INSERTVOLUME"));
		// CHECK1.setLayoutData(gridData62);
		// CHECK1.setSelection(false);
		// CHECK1.addSelectionListener(new
		// org.eclipse.swt.events.SelectionAdapter() {
		// public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
		// if (CHECK1.getSelection())
		// EDIT_CREATEDB_EXVOLNAME.setEnabled(true);
		// else
		// EDIT_CREATEDB_EXVOLNAME.setEnabled(false);
		// }
		// });
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.VOLUMENAME"));
		EDIT_CREATEDB_EXVOLNAME = new Text(group1, SWT.BORDER);
		EDIT_CREATEDB_EXVOLNAME.setEnabled(false);
		EDIT_CREATEDB_EXVOLNAME.setLayoutData(gridData63);
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.VOLUMEPATH"));
		EDIT_CREATEDB_EXVOLPATH = new Text(group1, SWT.BORDER);
		EDIT_CREATEDB_EXVOLPATH.setLayoutData(gridData64);
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.VOLUMEPURPOSE"));
		createCombo1();
		label4 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.NUMPAGES"));
		EDIT_CREATEDB_EXPAGESIZE = new Text(group1, SWT.BORDER);
		EDIT_CREATEDB_EXPAGESIZE.setLayoutData(gridData65);
		label = new Label(sShell, SWT.NONE);
		label.setLayoutData(gridData1);
		BUTTON_CREATEDB_ADDVOL = new Button(sShell, SWT.NONE);
		BUTTON_CREATEDB_ADDVOL.setText(Messages.getString("BUTTON.ADDVOLUME"));
		BUTTON_CREATEDB_ADDVOL.setLayoutData(gridData2);
		BUTTON_CREATEDB_ADDVOL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						AddVols av = new AddVols(EDIT_CREATEDB_EXVOLNAME
								.getText(), COMBO_CREATEDB_EXPURPOSE.getText(),
								EDIT_CREATEDB_EXPAGESIZE.getText(),
								EDIT_CREATEDB_EXVOLPATH.getText());
						if (av.Volname.length() <= 0) {
							CommonTool.ErrorBox(sShell.getShell(), Messages
									.getString("ERROR.INPUTVOLUMENAME"));
							return;
						}
						if (av.Volname.indexOf(" ") >= 0) {
							CommonTool.ErrorBox(sShell.getShell(), Messages
									.getString("ERROR.INVALIDVOLUMENAME"));
							return;
						}
						if (av.Volname.indexOf("_x") < 0) {
							CommonTool.ErrorBox(sShell.getShell(), Messages
									.getString("ERROR.INVALIDVOLUMENAME_X"));
							return;
						}
						if (CommonTool.atoi(av.Pages) <= 0) {
							CommonTool.ErrorBox(sShell.getShell(), Messages
									.getString("ERROR.INPUTPAGESIZE"));
							return;
						}
						for (int i = 0, n = addvols.size(); i < n; i++) {
							AddVols avtmp = (AddVols) addvols.get(i);
							if (avtmp.Volname.equals(av.Volname)) {
								CommonTool.ErrorBox(sShell.getShell(), Messages
										.getString("ERROR.ALREADYEXISTVOLUME"));
								return;
							}
						}
						addvols.add(av);
						SetDefaultVolname();
						updateTable1();
					}
				});
		BUTTON_CREATEDB_DELETEVOL = new Button(sShell, SWT.NONE);
		BUTTON_CREATEDB_DELETEVOL.setText(Messages
				.getString("BUTTON.DELETEVOLUME"));
		BUTTON_CREATEDB_DELETEVOL.setLayoutData(gridData3);
		BUTTON_CREATEDB_DELETEVOL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CurrentVol == null)
							return;
						for (int i = 0, n = addvols.size(); i < n; i++) {
							AddVols av = (AddVols) addvols.get(i);
							if (av.Volname.equals(CurrentVol)) {
								addvols.remove(i);
								break;
							}
						}
						SetDefaultVolname();
						updateTable1();
					}
				});
		label5 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.CURRENTADDTIONAL"));
		label5.setLayoutData(gridData4);
		createTable1();
		setinfo();
		sShell.getParent().pack();
	}

	private void createCombo1() {
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.widthHint = 84;
		gridData6.grabExcessVerticalSpace = true;
		gridData6.grabExcessHorizontalSpace = true;
		COMBO_CREATEDB_EXPURPOSE = new Combo(group1, SWT.DROP_DOWN
				| SWT.READ_ONLY);
		COMBO_CREATEDB_EXPURPOSE.setLayoutData(gridData6);
		COMBO_CREATEDB_EXPURPOSE
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						SetDefaultVolname();
					}
				});
	}

	private void createTable1() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalSpan = 3;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.heightHint = 80;
		gridData5.widthHint = 350;
		gridData5.grabExcessVerticalSpace = true;
		gridData5.grabExcessHorizontalSpace = true;
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST_CREATEDB_EXVOL = new Table(sShell, SWT.FULL_SELECTION | SWT.SINGLE
				| SWT.BORDER);
		LIST_CREATEDB_EXVOL.setLinesVisible(true);
		LIST_CREATEDB_EXVOL.setLayoutData(gridData5);
		LIST_CREATEDB_EXVOL.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 100, true));
		tlayout.addColumnData(new ColumnWeightData(50, 70, true));
		tlayout.addColumnData(new ColumnWeightData(50, 70, true));
		tlayout.addColumnData(new ColumnWeightData(50, 150, true));
		LIST_CREATEDB_EXVOL.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_CREATEDB_EXVOL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.VOLNAME"));
		tblcol = new TableColumn(LIST_CREATEDB_EXVOL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.PURPOSE"));
		tblcol = new TableColumn(LIST_CREATEDB_EXVOL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NUMPAGES"));
		tblcol = new TableColumn(LIST_CREATEDB_EXVOL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.PATH"));

		LIST_CREATEDB_EXVOL.addListener(SWT.Selection, new Listener() {
			public void handleEvent(Event event) {
				TableItem col = (TableItem) event.item;
				CurrentVol = col.getText(0);
			}
		});

		updateTable1();
	}

	private void updateTable1() {
		LIST_CREATEDB_EXVOL.removeAll();
		for (int i = 0, n = addvols.size(); i < n; i++) {
			AddVols av = (AddVols) addvols.get(i);
			TableItem item = new TableItem(LIST_CREATEDB_EXVOL, SWT.NONE);
			item.setText(0, av.Volname);
			item.setText(1, av.Purpose);
			item.setText(2, av.Pages);
			item.setText(3, av.Path);
		}
		for (int i = 0, n = LIST_CREATEDB_EXVOL.getColumnCount(); i < n; i++) {
			LIST_CREATEDB_EXVOL.getColumn(i).pack();
		}
	}

	public void createControl(Composite parent) {
		comparent = parent;
		createComposite();
		sShell.setParent(parent);
		setControl(sShell);
		setPageComplete(true);
	}

	private void setinfo() {
		COMBO_CREATEDB_EXPURPOSE.add("data", 0);
		COMBO_CREATEDB_EXPURPOSE.add("index", 1);
		COMBO_CREATEDB_EXPURPOSE.add("temp", 2);
		COMBO_CREATEDB_EXPURPOSE.add("generic", 3);
		COMBO_CREATEDB_EXPURPOSE.select(0);

		EDIT_CREATEDB_EXPAGESIZE.setText(MainRegistry.DBPARA_DATANUM);

		EDIT_CREATEDB_EXPAGESIZE.setToolTipText(Messages
				.getString("TOOLTIP.EDITPAGESIZE"));
		COMBO_CREATEDB_EXPURPOSE.setToolTipText(Messages
				.getString("TOOLTIP.COMBOPURPOSE"));
		BUTTON_CREATEDB_DELETEVOL.setToolTipText(Messages
				.getString("TOOLTIP.BTNDELETEVOL"));
		BUTTON_CREATEDB_ADDVOL.setToolTipText(Messages
				.getString("TOOLTIP.BTNADDVOL"));
		EDIT_CREATEDB_EXVOLNAME.setToolTipText(Messages
				.getString("TOOLTIP.EDITVOLNAME"));
		EDIT_CREATEDB_EXVOLPATH.setToolTipText(Messages
				.getString("TOOLTIP.EDITVOLPATH"));
	}

	public void SetDefaultVolname() {
		String dbName = CreateAction.newdb;
		String volname = dbName + "_" + COMBO_CREATEDB_EXPURPOSE.getText()
				+ "_x";
		String temp_volname = volname;

		boolean flag_found = false;
		int idx = 0;
		while (true) {
			NumberFormat nf = NumberFormat.getInstance();
			nf.setMinimumIntegerDigits(3);
			volname = temp_volname + nf.format(idx);

			flag_found = false;
			for (int i = 0, n = addvols.size(); i < n; i++) {
				AddVols av = (AddVols) addvols.get(i);

				if (av.Volname.equals(volname)) {
					flag_found = true;
					break;
				}
			}
			if (!flag_found)
				break;
			idx++;
		}
		EDIT_CREATEDB_EXVOLNAME.setText(volname);
	}

}
