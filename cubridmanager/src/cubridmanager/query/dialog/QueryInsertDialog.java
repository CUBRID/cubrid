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

package cubridmanager.query.dialog;

import java.util.ArrayList;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.SashForm;
import org.eclipse.swt.custom.TableEditor;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Listener;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.cubrid.DBAttribute;
import cubridmanager.cubrid.SchemaInfo;
import cubridmanager.query.action.QueryInsertAction;
import org.eclipse.swt.widgets.Label;

public class QueryInsertDialog extends Dialog {
	private static final String NEW_LINE = System.getProperty("line.separator");
	private Shell shlInsert = null; // @jve:decl-index=0:visual-constraint="13,7"
	private SashForm sashForm = null;
	private Composite composite = null;
	private Table tblInsert = null;
	private Text txtHistory = null;
	private Button btnInsert = null;
	private Button btnClear = null;
	private Button btnClose = null;
	private final String dbName;
	private final String tableName;
	private SchemaInfo objrec = null;
	private static ArrayList schemainfo = null;
	private int curIndex = -1;
	private int newIndex = -1;
	private Label lblTotalInsertedCount = null;
	private int cntTotalInsertedRecord = 0;
	private String insertQuery = null;
	public QueryInsertDialog(Shell parent, String dbName, String tableName) {
		super(parent);

		this.dbName = dbName;
		this.tableName = tableName;
		schemainfo = SchemaInfo.SchemaInfo_get(dbName);
		for (int i = 0, n = schemainfo.size(); i < n; i++) {
			if (((SchemaInfo) schemainfo.get(i)).name.equals(tableName)) {
				objrec = (SchemaInfo) schemainfo.get(i);
				break;
			}
		}
		if (objrec == null)
			shlInsert.dispose();
		createShlInsert();
		shlInsert.open();

	}

	/**
	 * This method initializes shlInsert
	 * 
	 */
	private void createShlInsert() {
		shlInsert = new Shell(super.getParent(), SWT.APPLICATION_MODAL
				| SWT.SHELL_TRIM);
		shlInsert.setText(Messages.getString("TITLE.TABLEINSERTACTION").concat(
				": ").concat(tableName));
		shlInsert.setLayout(new GridLayout());
		createSashForm();
		createComposite();
		shlInsert.pack();
		CommonTool.centerShell(shlInsert);
	}

	/**
	 * This method initializes sashForm
	 * 
	 */
	private void createSashForm() {
		GridData gridData = new GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessVerticalSpace = true;
		gridData.widthHint = 400;
		gridData.heightHint = 350;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		sashForm = new SashForm(shlInsert, SWT.NONE);
		sashForm.setOrientation(org.eclipse.swt.SWT.VERTICAL);
		sashForm.setLayoutData(gridData);
		createTblInsert();
		txtHistory = new Text(sashForm, SWT.BORDER | SWT.WRAP | SWT.V_SCROLL);
		sashForm.setWeights(new int[] { 60, 40 });
	}

	/**
	 * This method initializes composite
	 * 
	 */
	private void createComposite() {
		GridData gridData5 = new GridData();
		gridData5.grabExcessHorizontalSpace = true;
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData4 = new GridData();
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData3 = new GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData2 = new GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		GridData gridData1 = new GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.grabExcessHorizontalSpace = true;
		composite = new Composite(shlInsert, SWT.NONE);
		composite.setLayoutData(gridData1);
		composite.setLayout(gridLayout);

		lblTotalInsertedCount = new Label(composite, SWT.NONE);
		lblTotalInsertedCount.setText("");
		lblTotalInsertedCount.setLayoutData(gridData5);
		btnInsert = new Button(composite, SWT.NONE);
		btnInsert.setText(Messages.getString("QEDIT.INSERT"));
		btnInsert.setLayoutData(gridData4);
		btnInsert.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setTxtInsert();
						if (insertQuery.trim().length() > 0) {
							if (QueryInsertAction.insertRecord(dbName,
									insertQuery)) {
								cntTotalInsertedRecord++;
								lblTotalInsertedCount.setText(Messages
										.getString("QEDIT.TOTAL")
										+ cntTotalInsertedRecord
										+ Messages.getString("QEDIT.INSERTED"));
							}
							txtHistory.append(insertQuery);
							txtHistory.append(NEW_LINE);
							txtHistory.append("// ");
							txtHistory.append(QueryInsertAction.resultMsg);
							txtHistory.append(NEW_LINE);
						}
					}
				});
		btnClear = new Button(composite, SWT.NONE);
		btnClear.setText(Messages.getString("QEDIT.CLEAR"));
		btnClear.setLayoutData(gridData3);
		btnClear.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						clearInsert();
					}
				});
		btnClose = new Button(composite, SWT.NONE);
		btnClose.setText(Messages.getString("BUTTON.CLOSE"));
		btnClose.setLayoutData(gridData2);
		btnClose.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				shlInsert.dispose();
			}
		});
	}

	/**
	 * This method initializes tblInsert
	 * 
	 */
	private void createTblInsert() {

		tblInsert = new Table(sashForm, SWT.BORDER | SWT.FULL_SELECTION);
		tblInsert.setHeaderVisible(true);
		tblInsert.setLinesVisible(true);

		TableLayout layout = new TableLayout();
		layout.addColumnData(new ColumnWeightData(30));
		layout.addColumnData(new ColumnWeightData(30));
		layout.addColumnData(new ColumnWeightData(40));
		tblInsert.setLayout(layout);

		TableColumn tblColumn;
		tblColumn = new TableColumn(tblInsert, SWT.NONE);
		tblColumn.setText(Messages.getString("TABLE.ATTRIBUTE"));
		tblColumn = new TableColumn(tblInsert, SWT.NONE);
		tblColumn.setText(Messages.getString("TABLE.DOMAIN"));
		tblColumn = new TableColumn(tblInsert, SWT.NONE);
		tblColumn.setText(Messages.getString("TABLE.VALUE"));

		TableItem item;
		for (int i = 0, n = objrec.attributes.size(); i < n; i++) {
			DBAttribute da = (DBAttribute) objrec.attributes.get(i);
			item = new TableItem(tblInsert, SWT.NONE);
			item.setText(0, da.name);
			item.setText(1, da.type);
		}

		final TableEditor editor = new TableEditor(tblInsert);
		editor.horizontalAlignment = SWT.LEFT;
		editor.grabHorizontal = true;

		tblInsert.addListener(SWT.MouseUp, new Listener() {
			public void handleEvent(Event event) {
				Rectangle clientArea = tblInsert.getClientArea();
				Point pt = new Point(event.x, event.y);
				int index = tblInsert.getTopIndex();
				curIndex = newIndex;
				newIndex = tblInsert.getSelectionIndex();
				if (curIndex < 0 || curIndex != newIndex)
					return;
				while (index < tblInsert.getItemCount()) {
					boolean visible = false;
					final TableItem item = tblInsert.getItem(index);
					// for (int i = 0; i < tblInsert.getColumnCount(); i++) {
					Rectangle rect = item.getBounds(2);
					if (rect.contains(pt)) {
						// final int column = i;
						final Text text = new Text(tblInsert, SWT.MULTI);
						// String type = tblInsert.getColumn(1).getText();
						// if (type.equals("CLASS") || type.equals("SET") ||
						// type.equals("MULTISET") || type.equals("SEQUENCE") ||
						// item.getText(0).equals("NONE"))
						// text.setEditable(false);
						// else
						// text.setEditable(true);
						Listener textListener = new Listener() {
							public void handleEvent(final Event e) {
								switch (e.type) {
								case SWT.FocusOut:
									if (!text.getText().equals(item.getText(2))) {
										item.setText(2, text.getText());
									}
									text.dispose();
									break;
								case SWT.Traverse:
									switch (e.detail) {
									case SWT.TRAVERSE_RETURN:
										if (!text.getText().equals(
												item.getText(2))) {
											item.setText(2, text.getText());
										}
									case SWT.TRAVERSE_ESCAPE:
										text.dispose();
										e.doit = false;
									}
									break;
								}
							}
						};
						text.addListener(SWT.FocusOut, textListener);
						text.addListener(SWT.Traverse, textListener);
						editor.setEditor(text, item, 2);
						// if (item.getText(i).equals(QueryEditor.STR_NULL))
						// text.setText("");
						// else
						text.setText(item.getText(2));
						text.selectAll();
						text.setFocus();
						return;
					}
					if (!visible && rect.intersects(clientArea)) {
						visible = true;
					}
					// }
					if (!visible)
						return;
					index++;
				}
			}
		});

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(30, true));
		tlayout.addColumnData(new ColumnWeightData(30, true));
		tlayout.addColumnData(new ColumnWeightData(40, true));
		tblInsert.setLayout(tlayout);
	}

	private void clearInsert() {
		for (int i = 0; i < tblInsert.getItemCount(); i++) {
			tblInsert.getItem(i).setText(2, "");
		}
	}

	private void setTxtInsert() {
		StringBuffer sql = new StringBuffer("insert into ");
		sql.append(tableName);
		sql.append(" (");
		StringBuffer columns = new StringBuffer("");
		StringBuffer values = new StringBuffer("");

		for (int i = 0; i < tblInsert.getItemCount(); i++) {
			String type = tblInsert.getItem(i).getText(1);
			String value = tblInsert.getItem(i).getText(2);
			if (value.length() > 0) {
				if (values.length() > 0) {
					columns.append(", ");
					values.append(", ");
				}
				columns.append(tblInsert.getItem(i).getText(0));
				if (type.startsWith("set")) {
					if (value.startsWith("{"))
						values.append(value);
					else {
						values.append("{");
						values.append(value);
						values.append("}");
					}
				} else if (type.startsWith("character")) {
					if (value.startsWith("'"))
						values.append(value);
					else {
						values.append("'");
						values.append(value);
						values.append("'");
					}
				} else if (type.startsWith("national")) {
					if (value.startsWith("N'"))
						values.append(value);
					else if (value.startsWith("'")) {
						values.append("N");
						values.append(value);
					} else {
						values.append("N'");
						values.append(value);
						values.append("'");
					}
				} else if (type.startsWith("bit")) {
					if (value.startsWith("X'") || value.startsWith("B'"))
						values.append(value);
					else if (value.replaceAll("'", "").replaceAll("0", "")
							.replaceAll("1", "").length() == 0) {
						values.append("B'");
						values.append(value);
						values.append("'");
					} else if (value.startsWith("'")) {
						values.append("X");
						values.append(value);
					} else {
						values.append("X'");
						values.append(value);
						values.append("'");
					}
				} else if (type.startsWith("time") || type.startsWith("date")) {
					if (value.startsWith("to_") || value.startsWith("'"))
						values.append(value);
					else {
						values.append("'");
						values.append(value);
						values.append("'");
					}
				} else
					values.append(value);
			}
		}
		if (columns.length() > 0 && values.length() > 0) {
			sql.append(columns);
			sql.append(") values (");
			sql.append(values);
			sql.append(")");
		} else
			sql = new StringBuffer("");

		insertQuery = new String(sql);
	}
}
