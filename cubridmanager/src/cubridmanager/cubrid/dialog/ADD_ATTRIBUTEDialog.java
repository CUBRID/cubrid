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

import org.eclipse.swt.custom.CCombo;
import org.eclipse.swt.custom.TableEditor;
import org.eclipse.swt.events.FocusAdapter;
import org.eclipse.swt.events.FocusEvent;
import org.eclipse.swt.events.KeyEvent;
import org.eclipse.swt.events.MouseAdapter;
import org.eclipse.swt.events.MouseEvent;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.TraverseEvent;
import org.eclipse.swt.events.TraverseListener;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.VerifyDigitListener;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.SchemaInfo;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.dialog.PROPPAGE_CLASS_PAGE1Dialog;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Table;

public class ADD_ATTRIBUTEDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Label label2 = null;
	private Button RADIO_ATTRIBUTE_ADD_INSTANCE = null;
	private Button RADIO_ATTRIBUTE_ADD_SHARED = null;
	private Button RADIO_ATTRIBUTE_ADD_CLASS = null;
	private Label label3 = null;
	private Text EDIT_ATTRIBUTE_ADD_NAME = null;
	private Label label4 = null;
	private Combo COMBO_ATTRIBUTE_ADD_TYPE = null;
	private Button CHECK_ATTRIBUTE_ADD_UNIQUE = null;
	private Button CHECK_ATTRIBUTE_ADD_NOTNULL = null;
	private Label label5 = null;
	private Text EDIT_ATTRIBUTE_ADD_DEFAULT = null;
	private Label label6 = null;
	private Label label7 = null;
	private Label label8 = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;
	Label dummylabel = null;
	private Composite cmpDomainArea = null;
	private Composite cmpPrecisionScaleArea = null;
	private Label lblPrecision = null;
	private Text txtPrecision = null;
	private Label lblScale = null;
	private Text txtScale = null;
	private Composite cmpSetItemArea = null;
	private Table tblSetItems = null;
	private int beforeSelectionIndex = -1;
	private int currentSelectionIndex = -1;
	private VerifyDigitListener verifyListener = null;

	public ADD_ATTRIBUTEDialog(Shell parent) {
		super(parent);
	}

	public ADD_ATTRIBUTEDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		RADIO_ATTRIBUTE_ADD_INSTANCE.setSelection(true);
		COMBO_ATTRIBUTE_ADD_TYPE.select(0);
		hideSetArea();
		showManualInputArea();
		dlgShell.pack();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void createSShell() {
		dlgShell = new Shell(super.getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.ADD_ATTRIBUTEDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.horizontalSpacing = 10;
		gridLayout.marginWidth = 10;
		gridLayout.numColumns = 2;
		gridLayout.verticalSpacing = 10;
		gridLayout.marginHeight = 10;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);

		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.horizontalSpacing = 10;
		gridLayout1.marginWidth = 10;
		gridLayout1.verticalSpacing = 10;
		gridLayout1.numColumns = 4;
		gridLayout1.marginHeight = 10;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayoutData(gridData1);
		group1.setLayout(gridLayout1);

		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.TYPE"));
		RADIO_ATTRIBUTE_ADD_INSTANCE = new Button(group1, SWT.RADIO);
		RADIO_ATTRIBUTE_ADD_INSTANCE.setText(Messages
				.getString("RADIO.INSTANCE"));
		RADIO_ATTRIBUTE_ADD_SHARED = new Button(group1, SWT.RADIO);
		RADIO_ATTRIBUTE_ADD_SHARED.setText(Messages.getString("RADIO.SHARED"));
		RADIO_ATTRIBUTE_ADD_SHARED
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (RADIO_ATTRIBUTE_ADD_SHARED.getSelection()) {
							CHECK_ATTRIBUTE_ADD_UNIQUE.setSelection(false);
							CHECK_ATTRIBUTE_ADD_UNIQUE.setEnabled(false);
						} else {
							CHECK_ATTRIBUTE_ADD_UNIQUE.setEnabled(true);
						}
					}
				});
		RADIO_ATTRIBUTE_ADD_CLASS = new Button(group1, SWT.RADIO);
		RADIO_ATTRIBUTE_ADD_CLASS.setText(Messages.getString("RADIO.CLASS"));
		RADIO_ATTRIBUTE_ADD_CLASS
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (RADIO_ATTRIBUTE_ADD_CLASS.getSelection()) {
							CHECK_ATTRIBUTE_ADD_UNIQUE.setSelection(false);
							CHECK_ATTRIBUTE_ADD_UNIQUE.setEnabled(false);
							CHECK_ATTRIBUTE_ADD_NOTNULL.setSelection(false);
							CHECK_ATTRIBUTE_ADD_NOTNULL.setEnabled(false);
						} else {
							CHECK_ATTRIBUTE_ADD_UNIQUE.setEnabled(true);
							CHECK_ATTRIBUTE_ADD_NOTNULL.setEnabled(true);
						}
					}
				});

		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.NAME"));
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 3;
		gridData2.widthHint = 254;
		EDIT_ATTRIBUTE_ADD_NAME = new Text(group1, SWT.BORDER);
		EDIT_ATTRIBUTE_ADD_NAME.setLayoutData(gridData2);

		GridData gridData = new GridData();
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		label4 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.DOMAIN"));
		label4.setLayoutData(gridData);
		createCmpDomainArea();

		GridData gridCheckArea = new org.eclipse.swt.layout.GridData();
		gridCheckArea.horizontalSpan = 4;
		gridCheckArea.grabExcessHorizontalSpace = true;
		gridCheckArea.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout layoutCheckArea = new GridLayout();
		layoutCheckArea.numColumns = 2;
		layoutCheckArea.makeColumnsEqualWidth = true;
		Composite cmpCheckArea = new Composite(group1, SWT.NONE);
		cmpCheckArea.setLayout(layoutCheckArea);
		cmpCheckArea.setLayoutData(gridCheckArea);
		GridData gridUnique = new GridData();
		gridUnique.grabExcessHorizontalSpace = true;
		CHECK_ATTRIBUTE_ADD_UNIQUE = new Button(cmpCheckArea, SWT.CHECK);
		CHECK_ATTRIBUTE_ADD_UNIQUE.setText(Messages.getString("TABLE.UNIQUE"));
		CHECK_ATTRIBUTE_ADD_UNIQUE.setLayoutData(gridUnique);
		CHECK_ATTRIBUTE_ADD_UNIQUE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_ATTRIBUTE_ADD_UNIQUE.getSelection()) {
							RADIO_ATTRIBUTE_ADD_SHARED.setSelection(false);
							RADIO_ATTRIBUTE_ADD_SHARED.setEnabled(false);
						} else {
							RADIO_ATTRIBUTE_ADD_SHARED.setEnabled(true);
						}
					}
				});
		CHECK_ATTRIBUTE_ADD_NOTNULL = new Button(cmpCheckArea, SWT.CHECK);
		CHECK_ATTRIBUTE_ADD_NOTNULL
				.setText(Messages.getString("TABLE.NOTNULL"));

		label5 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.DEFAULTVALUE"));
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalSpan = 3;
		gridData4.widthHint = 254;
		EDIT_ATTRIBUTE_ADD_DEFAULT = new Text(group1, SWT.BORDER);
		EDIT_ATTRIBUTE_ADD_DEFAULT.setLayoutData(gridData4);

		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.verticalSpan = 3;
		dummylabel = new Label(group1, SWT.NONE);
		dummylabel.setLayoutData(gridData11);
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalSpan = 3;
		label6 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.NOTEALLTYPES"));
		label6.setLayoutData(gridData5);
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.horizontalSpan = 3;
		label7 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label7.setText(Messages.getString("LABEL.MUSTBEENCLOSED"));
		label7.setLayoutData(gridData6);
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.horizontalSpan = 3;
		label8 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label8.setText(Messages.getString("LABEL.EXABCDEFABC"));
		label8.setLayoutData(gridData7);

		GridData gridData31 = new org.eclipse.swt.layout.GridData();
		gridData31.widthHint = 100;
		gridData31.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData31.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData31);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String attname = EDIT_ATTRIBUTE_ADD_NAME.getText()
								.trim();
						String attdeft = EDIT_ATTRIBUTE_ADD_DEFAULT.getText();
						String retstr = CommonTool
								.ValidateCheckInIdentifier(attname);
						if (retstr.length() > 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INVALIDATTRNAME"));
							return;
						}
						String atttype = makeType();
						if (atttype.length() <= 0) {
							return;
						}
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "classname:"
								+ PROPPAGE_CLASS_PAGE1Dialog.si.name + "\n";
						msg += "attributename:" + attname + "\n";
						msg += "type:" + atttype + "\n";
						msg += "default:" + attdeft + "\n";
						msg += "category:";
						if (RADIO_ATTRIBUTE_ADD_INSTANCE.getSelection())
							msg += "instance\n";
						else if (RADIO_ATTRIBUTE_ADD_SHARED.getSelection())
							msg += "shared\n";
						else if (RADIO_ATTRIBUTE_ADD_CLASS.getSelection())
							msg += "class\n";
						msg += "unique:"
								+ (CHECK_ATTRIBUTE_ADD_UNIQUE.getSelection() ? "yes"
										: "no") + "\n";
						msg += "notnull:"
								+ (CHECK_ATTRIBUTE_ADD_NOTNULL.getSelection() ? "yes"
										: "no") + "\n";

						ClientSocket cs = new ClientSocket();

						if (!cs.SendBackGround(dlgShell, msg, "addattribute",
								Messages.getString("WAITING.ADDATTR"))) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}

						ret = true;
						dlgShell.dispose();
					}
				});

		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.widthHint = 100;
		gridData21.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData21.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData21);
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

	private void addDomainItem() {
		int i = 0;
		// 0 : direct input, 1 ~ 6 : need size, 7 : need precision, scale, 17 ~ 19 : set type.
		COMBO_ATTRIBUTE_ADD_TYPE.add(Messages.getString("COMBO.INPUT"), i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("CHAR", i++); // 1
		COMBO_ATTRIBUTE_ADD_TYPE.add("VARCHAR", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("NCHAR", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("NCHAR VARYING", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("BIT", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("BIT VARYING", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("NUMERIC", i++); // 7
		COMBO_ATTRIBUTE_ADD_TYPE.add("INTEGER", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("SMALLINT", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("MONETARY", i++); // 10
		COMBO_ATTRIBUTE_ADD_TYPE.add("FLOAT", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("DOUBLE", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("DATE", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("TIME", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("TIMESTAMP", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("STRING", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("SET", i++); // 17
		COMBO_ATTRIBUTE_ADD_TYPE.add("MULTISET", i++);
		COMBO_ATTRIBUTE_ADD_TYPE.add("SEQUENCE", i++);

		ArrayList sinfo = null;
		AuthItem authrec = null;

		for (int j = 0, n = MainRegistry.Authinfo.size(); j < n; j++) {
			authrec = (AuthItem) MainRegistry.Authinfo.get(j);
			if (authrec.dbname.equals(CubridView.Current_db)) {
				sinfo = authrec.Schema;
				for (int ai = 0, an = sinfo.size(); ai < an; ai++) {
					if (((SchemaInfo) sinfo.get(ai)).virtual.equals("normal"))
						COMBO_ATTRIBUTE_ADD_TYPE.add(((SchemaInfo) sinfo
								.get(ai)).name, i++);
				}
			}
		}
	}

	/**
	 * This method initializes cmpDomainArea
	 * 
	 */
	private void createCmpDomainArea() {
		GridData gridData8 = new GridData();
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData8.grabExcessHorizontalSpace = true;
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.marginHeight = 0;
		gridLayout2.verticalSpacing = 0;
		gridLayout2.marginWidth = 0;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalSpan = 3;
		gridData3.grabExcessHorizontalSpace = true;
		gridData3.horizontalAlignment = SWT.FILL;
		cmpDomainArea = new Composite(group1, SWT.NONE);
		cmpDomainArea.setLayoutData(gridData3);
		cmpDomainArea.setLayout(gridLayout2);

		COMBO_ATTRIBUTE_ADD_TYPE = new Combo(cmpDomainArea, SWT.BORDER
				| SWT.READ_ONLY);
		COMBO_ATTRIBUTE_ADD_TYPE.setVisibleItemCount(20);
		COMBO_ATTRIBUTE_ADD_TYPE.setLayoutData(gridData8);
		addDomainItem();

		COMBO_ATTRIBUTE_ADD_TYPE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						txtPrecision.setText("");
						txtScale.setText("");
						switch (COMBO_ATTRIBUTE_ADD_TYPE.getSelectionIndex()) {
						case 0:
							hideSetArea();
							showManualInputArea();
							break;
						case 1:
						case 2:
						case 3:
						case 4:
						case 5:
						case 6:
							hideSetArea();
							showSizeArea();
							break;
						case 7:
							hideSetArea();
							showPrecisionScaleArea();
							break;
						case 17:
						case 18:
						case 19:
							hideSizeArea();
							showSetArea();
							break;
						default:
							hideSizeArea();
							hideSetArea();
						}
						dlgShell.pack();
					}
				});
		createCmpPrecisionScaleArea();
		createCmpSetItemArea();
	}

	/**
	 * This method initializes cmpPrecisionScaleArea
	 * 
	 */
	private void createCmpPrecisionScaleArea() {
		GridData gridData12 = new GridData();
		gridData12.grabExcessHorizontalSpace = false;
		gridData12.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData10 = new GridData();
		gridData10.grabExcessHorizontalSpace = true;
		gridData10.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout4 = new GridLayout();
		gridLayout4.numColumns = 4;
		gridLayout4.verticalSpacing = 0;
		gridLayout4.marginWidth = 0;
		cmpPrecisionScaleArea = new Composite(cmpDomainArea, SWT.NONE);
		cmpPrecisionScaleArea.setLayout(gridLayout4);
		cmpPrecisionScaleArea.setLayoutData(gridData10);
		lblPrecision = new Label(cmpPrecisionScaleArea, SWT.NONE);
		txtPrecision = new Text(cmpPrecisionScaleArea, SWT.BORDER);
		lblScale = new Label(cmpPrecisionScaleArea, SWT.NONE);
		lblScale.setLayoutData(gridData12);
		lblScale.setText(Messages.getString("LABEL.SCALE"));
		txtScale = new Text(cmpPrecisionScaleArea, SWT.BORDER);
		txtScale.setTextLimit(2);
		verifyListener = new VerifyDigitListener();
		txtScale.addListener(SWT.Verify, verifyListener);
	}

	/**
	 * This method initializes cmpSetItemArea
	 * 
	 */
	private void createCmpSetItemArea() {
		GridData gridData9 = new GridData();
		gridData9.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData9.heightHint = 50;
		cmpSetItemArea = new Composite(cmpDomainArea, SWT.NONE);
		cmpSetItemArea.setLayoutData(gridData9);
		cmpSetItemArea.setLayout(new FillLayout());

		tblSetItems = new Table(cmpSetItemArea, SWT.BORDER | SWT.FULL_SELECTION);
		tblSetItems.setHeaderVisible(true);
		tblSetItems.setLinesVisible(true);

		TableColumn tblCol = new TableColumn(tblSetItems, SWT.NONE);
		tblCol.setText(Messages.getString("LABEL.DOMAIN2"));
		tblCol = new TableColumn(tblSetItems, SWT.NONE);
		tblCol.setText(Messages.getString("LABEL.SIZE"));
		tblCol = new TableColumn(tblSetItems, SWT.NONE);
		tblCol.setText(Messages.getString("LABEL.SCALE"));

		final TableEditor editor = new TableEditor(tblSetItems);
		editor.horizontalAlignment = SWT.RIGHT;
		editor.grabHorizontal = true;

		tblSetItems.addMouseListener(new MouseAdapter() {
			public void mouseDown(MouseEvent event) {
				Control old = editor.getEditor();
				if (old != null)
					old.dispose();

				Point pt = new Point(event.x, event.y);

				final TableItem item = tblSetItems.getItem(pt);

				beforeSelectionIndex = currentSelectionIndex;
				currentSelectionIndex = tblSetItems.getSelectionIndex();
				if (beforeSelectionIndex < 0
						|| beforeSelectionIndex != currentSelectionIndex)
					return;

				if (item != null) {
					int column = -1;
					for (int i = 0, n = tblSetItems.getColumnCount(); i < n; i++) {
						Rectangle rect = item.getBounds(i);
						if (rect.contains(pt)) {
							column = i;
							break;
						}
					}

					// db create auth field, cas auth field
					if (column == 0) {
						final CCombo combo = new CCombo(tblSetItems,
								SWT.READ_ONLY);
						combo.setVisibleItemCount(15);
						combo.add(Messages.getString("COMBOITEM.SELECTION"));
						for (int i = 0; i < COMBO_ATTRIBUTE_ADD_TYPE
								.getItemCount(); i++) {
							String domain = COMBO_ATTRIBUTE_ADD_TYPE.getItem(i);
							if (i == 0 || (i > 16 && i < 20))
								continue;
							combo.add(domain);
						}

						combo.select(combo.indexOf(item.getText(column)));

						editor.minimumWidth = combo.computeSize(SWT.DEFAULT,
								SWT.DEFAULT).x;
						tblSetItems.getColumn(column).setWidth(
								editor.minimumWidth);

						combo.setFocus();
						editor.setEditor(combo, item, column);

						combo.addSelectionListener(new SelectionAdapter() {
							public void widgetSelected(SelectionEvent event) {
								if (combo.getSelectionIndex() != 0) {
									if (item
											.getText(0)
											.equals(
													Messages
															.getString("COMBOITEM.SELECTION"))) {
										TableItem newItem = new TableItem(
												tblSetItems, SWT.NONE);
										newItem
												.setText(
														0,
														Messages
																.getString("COMBOITEM.SELECTION"));
									}
									item.setText(0, combo.getText());
								} else {
									if (tblSetItems.getSelectionIndex() < (tblSetItems
											.getItemCount() - 1))
										tblSetItems.remove(tblSetItems
												.getSelectionIndex());
									else {
										item.setText(1, "");
										item.setText(2, "");
									}
								}
								combo.dispose();
							}
						});
					} else {
						String domain = item.getText(0);
						if (column == 1) {
							if (!(domain.equals("CHAR")
									|| domain.equals("VARCHAR")
									|| domain.equals("NCHAR")
									|| domain.equals("NCHAR VARYING")
									|| domain.equals("BIT")
									|| domain.equals("BIT VARYING") || domain
									.equals("NUMERIC")))
								return;
						} else {
							if (!domain.equals("NUMERIC"))
								return;
						}

						final Text text = new Text(tblSetItems, SWT.NONE);
						text.setForeground(item.getForeground());

						if (column == 1 && !domain.equals("NUMERIC"))
							text.setTextLimit(10);
						else
							text.setTextLimit(2);

						text.setText((String) item.getText(column));
						text.setForeground(item.getForeground());
						text.selectAll();
						text.setFocus();

						editor.minimumWidth = text.getBounds().width;

						editor.setEditor(text, item, column);

						final int col = column;
						text.addTraverseListener(new TraverseListener() {
							public void keyTraversed(TraverseEvent e) {
								switch (e.detail) {
								case SWT.TRAVERSE_ESCAPE:
									e.doit = false;
									text.dispose();
									break;
								case SWT.TRAVERSE_ARROW_NEXT:
								case SWT.TRAVERSE_ARROW_PREVIOUS:
								case SWT.TRAVERSE_PAGE_NEXT:
								case SWT.TRAVERSE_PAGE_PREVIOUS:
								case SWT.TRAVERSE_TAB_NEXT:
								case SWT.TRAVERSE_TAB_PREVIOUS:
								case SWT.TRAVERSE_RETURN:
								default:
									item.setText(col, text.getText());
									text.dispose();
									break;
								}
							}
						});

						text.addFocusListener(new FocusAdapter() {
							public void focusLost(FocusEvent e) {
								item.setText(col, text.getText());
								text.dispose();
							}
						});

						text.addListener(SWT.Verify, new VerifyDigitListener());
					}
				}
			}
		});

		TableItem emptyItem = new TableItem(tblSetItems, SWT.NONE);
		emptyItem.setText(Messages.getString("COMBOITEM.SELECTION"));

		for (int i = 0, n = tblSetItems.getColumnCount(); i < n; i++)
			tblSetItems.getColumn(i).pack();

		Menu menu = new Menu(dlgShell, SWT.POP_UP);
		final MenuItem itemDelete = new MenuItem(menu, SWT.PUSH);
		itemDelete.setText(Messages.getString("QEDIT.DELETE"));
		tblSetItems.setMenu(menu);
		itemDelete.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent event) {
				deleteRecord(tblSetItems.getSelectionIndex());
			}
		});

		tblSetItems.addKeyListener(new org.eclipse.swt.events.KeyAdapter() {
			public void keyReleased(KeyEvent e) {
				if (e.keyCode == SWT.DEL) {
					deleteRecord(tblSetItems.getSelectionIndex());
				}
			}
		});
	}

	private void deleteRecord(int idx) {
		tblSetItems.remove(idx);
		if (tblSetItems.getItemCount() < 1
				|| !tblSetItems.getItem(tblSetItems.getItemCount() - 1)
						.getText(0).equals(
								Messages.getString("COMBOITEM.SELECTION"))) {
			TableItem emptyNew = new TableItem(tblSetItems, SWT.NONE);
			emptyNew.setText(Messages.getString("COMBOITEM.SELECTION")); 
		}
	}

	private void hideSizeArea() {
		GridData hideVertical = new GridData();
		hideVertical.heightHint = 0;
		hideVertical.grabExcessHorizontalSpace = true;
		hideVertical.horizontalAlignment = SWT.FILL;
		cmpPrecisionScaleArea.setVisible(false);
		cmpPrecisionScaleArea.setLayoutData(hideVertical);
	}

	private void hideSetArea() {
		GridData hideVertical = new GridData();
		hideVertical.heightHint = 0;
		cmpSetItemArea.setLayoutData(hideVertical);
	}

	private void showManualInputArea() {
		showSizeArea();
		lblPrecision.setText(Messages.getString("LABEL.DOMAIN2"));
		txtPrecision.setTextLimit(2147483646);
		if (verifyListener != null)
			txtPrecision.removeListener(SWT.Verify, verifyListener);
	}

	private void showSizeArea() {
		hideSizeArea();
		dlgShell.pack();

		GridData showVertical = new GridData();
		showVertical.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		showVertical.grabExcessHorizontalSpace = true;
		cmpPrecisionScaleArea.setVisible(true);
		cmpPrecisionScaleArea.setLayoutData(showVertical);

		lblScale.setVisible(false);
		txtScale.setVisible(false);

		lblPrecision.setLayoutData(new GridData());

		GridData gridPrecision = new GridData();
		gridPrecision.grabExcessHorizontalSpace = true;
		gridPrecision.horizontalAlignment = SWT.FILL;
		txtPrecision.setLayoutData(gridPrecision);

		GridData hideHorizontal = new GridData();
		hideHorizontal.widthHint = 0;

		lblScale.setLayoutData(hideHorizontal);
		txtScale.setLayoutData(hideHorizontal);

		lblPrecision.setText(Messages.getString("LABEL.SIZE"));
		txtPrecision.setTextLimit(10);

		if (verifyListener != null)
			txtPrecision.removeListener(SWT.Verify, verifyListener);
		verifyListener = new VerifyDigitListener();
		txtPrecision.addListener(SWT.Verify, verifyListener);
	}

	private void showPrecisionScaleArea() {
		hideSizeArea();
		dlgShell.pack();

		GridData showVertical = new GridData();
		showVertical.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		showVertical.grabExcessHorizontalSpace = true;
		cmpPrecisionScaleArea.setVisible(true);
		cmpPrecisionScaleArea.setLayoutData(showVertical);

		lblScale.setVisible(true);
		txtScale.setVisible(true);

		lblPrecision.setLayoutData(new GridData());

		GridData gridTextBox = new GridData();
		gridTextBox.widthHint = 60;
		txtPrecision.setLayoutData(gridTextBox);

		GridData gridScale = new GridData();
		gridScale.grabExcessHorizontalSpace = true;
		gridScale.horizontalAlignment = SWT.END;
		lblScale.setLayoutData(gridScale);

		txtScale.setLayoutData(gridTextBox);

		lblPrecision.setText(Messages.getString("LABEL.PRECISION"));
		txtPrecision.setTextLimit(2);

		if (verifyListener != null)
			txtPrecision.removeListener(SWT.Verify, verifyListener);
		verifyListener = new VerifyDigitListener();
		txtPrecision.addListener(SWT.Verify, verifyListener);
	}

	private void showSetArea() {
		GridData showVertical = new GridData();
		showVertical.grabExcessHorizontalSpace = true;
		showVertical.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		showVertical.heightHint = 80;
		cmpSetItemArea.setLayoutData(showVertical);
	}

	private String makeType() {
		String domain = COMBO_ATTRIBUTE_ADD_TYPE.getText();
		String errmsg = "";
		switch (COMBO_ATTRIBUTE_ADD_TYPE.getSelectionIndex()) {
		case 0:
			domain = txtPrecision.getText();
			if (domain.length() < 1) {
				errmsg = Messages.getString("ERROR.INVALIDATTRTYPE");
				txtPrecision.setFocus();
			}
			break;
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
			if (txtPrecision.getText().length() > 0) {
				int size = stringToUnsignedInteger(txtPrecision.getText());
				if (size < 0) {
					errmsg = Messages.getString("ERROR.INVALIDSIZE");
					txtPrecision.setFocus();
				} else if (size > 1073741823) {
					errmsg = Messages.getString("ERROR.MAXSIZEOVER");
					txtPrecision.setFocus();
				} else
					domain += "(" + txtPrecision.getText() + ")";
			}
			break;
		case 7:
			if (txtPrecision.getText().length() > 0
					|| txtScale.getText().length() > 0) {
				int precision = stringToUnsignedInteger(txtPrecision.getText());
				int scale = stringToUnsignedInteger(txtScale.getText());
				if (precision < 0) {
					errmsg = Messages.getString("ERROR.INVALIDPRECISION");
					txtPrecision.setFocus();
				} else if (precision > 38) {
					errmsg = Messages.getString("ERROR.MAXPRECISIONOVER");
					txtPrecision.setFocus();
				} else if (scale < 0) {
					if (txtScale.getText().length() != 0) {
						errmsg = Messages.getString("ERROR.INVALIDSCALE");
						txtScale.setFocus();
					} else
						domain += "(" + txtPrecision.getText() + ")";
				} else if (scale > 38) {
					errmsg = Messages.getString("ERROR.MAXSCALEOVER");
					txtScale.setFocus();
				} else if (precision < scale) {
					errmsg = Messages.getString("ERROR.INVALIDCOMBINATION");
					txtPrecision.setFocus();
				} else
					domain += "(" + txtPrecision.getText() + ","
							+ txtScale.getText() + ")";
			}
			break;
		case 17:
		case 18:
		case 19:
			domain += "(";
			for (int i = 0; i < tblSetItems.getItemCount() - 1; i++) {
				TableItem item = tblSetItems.getItem(i);
				if (i > 0)
					domain += ",";

				domain += item.getText(0);

				if (item.getText(0).equals("CHAR")
						|| item.getText(0).equals("VARCHAR")
						|| item.getText(0).equals("NCHAR")
						|| item.getText(0).equals("NCHAR VARYING")
						|| item.getText(0).equals("BIT")
						|| item.getText(0).equals("BIT VARYING")) {
					if (item.getText(1).length() > 0) {
						int size = stringToUnsignedInteger(item.getText(1));
						if (size < 0) {
							errmsg = Messages.getString("ERROR.INVALIDSIZE");
							break;
						} else if (size > 1073741823) {
							errmsg = Messages.getString("ERROR.MAXSIZEOVER");
							break;
						} else
							domain += "(" + item.getText(1) + ")";
					}
				} else if (item.getText(0).equals("NUMERIC")) {
					if (item.getText(1).length() > 0
							|| item.getText(2).length() > 0) {
						int precision = stringToUnsignedInteger(item.getText(1));
						int scale = stringToUnsignedInteger(item.getText(2));
						if (precision < 0) {
							errmsg = Messages
									.getString("ERROR.INVALIDPRECISION");
							break;
						} else if (precision > 38) {
							errmsg = Messages
									.getString("ERROR.MAXPRECISIONOVER");
							break;
						} else if (scale < 0) {
							if (item.getText(2).length() != 0) {
								errmsg = Messages
										.getString("ERROR.INVALIDSCALE");
								break;
							} else
								domain += "(" + item.getText(1) + ")";
						} else if (scale > 38) {
							errmsg = Messages.getString("ERROR.MAXSCALEOVER");
							break;
						} else if (precision < scale) {
							errmsg = Messages
									.getString("ERROR.INVALIDCOMBINATION");
							break;
						} else
							domain += "(" + item.getText(1) + ","
									+ item.getText(2) + ")";
					}
				}
			}
			domain += ")";
			break;
		}

		if (errmsg.length() > 0) {
			CommonTool.ErrorBox(dlgShell, errmsg);
			domain = "";
		}

		return domain;
	}

	private int stringToUnsignedInteger(String strVal) {
		int retVal;
		try {
			retVal = Integer.parseInt(strVal);
		} catch (Exception e) {
			retVal = -1;
		}
		return retVal;
	}
}
