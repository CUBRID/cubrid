/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
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
package com.cubrid.cubridmanager.ui.cubrid.table.dialog;

import org.apache.log4j.Logger;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.SashForm;
import org.eclipse.swt.custom.TableEditor;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.TraverseEvent;
import org.eclipse.swt.events.TraverseListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.model.DataType;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.table.JDBCUtil;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;

public class QueryInsertDialog extends
		CMTitleAreaDialog {
	private static final Logger logger = LogUtil.getLogger(QueryInsertDialog.class);
	private static final String NEW_LINE = System.getProperty("line.separator");
	private SashForm sashForm = null;
	private Composite composite = null;
	private Table tableInsert = null;
	private Text txtHistory = null;
	private TableEditor editor = null;

	private final String tableName;
	private SchemaInfo objrec = null;

	private Label lblTotalInsertedCount = null;
	private int cntTotalInsertedRecord = 0;
	private String insertQuery = null;

	CubridDatabase database = null;

	private int BTN_INSERT_ID = 100;
	private int BTN_CLEAR_ID = 200;
	private int BTN_CLOSE_ID = 300;
	private static final int editColumn = 3;

	/**
	 * 
	 * @param parentShell
	 */
	public QueryInsertDialog(Shell parentShell, DefaultSchemaNode table) {
		super(parentShell);
		this.tableName = table.getName();
		this.database = table.getDatabase();
		this.objrec = table.getDatabase().getDatabaseInfo().getSchemaInfo(
				tableName);

	}

	@Override
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp, CubridManagerHelpContextIDs.databaseTable);
		Composite composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();

		layout.numColumns = 1;
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);

		createSashForm(composite);
		createComposite(composite);

		setTitle(Messages.insertInstanceMsgTitle);
		setMessage(Messages.insertInstanceMsg);
		return parentComp;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		String msg = Messages.bind(Messages.insertInstanceWindowTitle,
				this.tableName);
		getShell().setText(msg);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, BTN_INSERT_ID, Messages.insertButtonName, false);
		createButton(parent, BTN_CLEAR_ID, Messages.clearButtonName, false);
		createButton(parent, BTN_CLOSE_ID, Messages.closeButtonName, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == BTN_INSERT_ID) {
			if (!validate()) {
				return;
			}
			createInsertSQL();
			if (insertQuery.trim().length() > 0) {

				if (JDBCUtil.insertRecord(database, insertQuery)) {
					cntTotalInsertedRecord++;
					if (1 == cntTotalInsertedRecord) {
						lblTotalInsertedCount.setText(Messages.totalInsertedCountMsg1);
					} else {
						String msg = Messages.bind(
								Messages.totalInsertedCountMsg2,
								cntTotalInsertedRecord);
						lblTotalInsertedCount.setText(msg);
					}
				}
				txtHistory.append(insertQuery);
				txtHistory.append(NEW_LINE);
				txtHistory.append("// ");
				txtHistory.append(JDBCUtil.getResultMsg());
				txtHistory.append(NEW_LINE);
				txtHistory.append(NEW_LINE);
			}
		} else if (buttonId == BTN_CLEAR_ID) {
			clearInsert();
			clearHistory();
			lblTotalInsertedCount.setText("");
			editor.getEditor().dispose();
			//			keyText.dispose();
			//			keyEditor.dispose();
		} else if (buttonId == BTN_CLOSE_ID) {
			this.getShell().dispose();

			this.close();
		}
		setReturnCode(buttonId);
	}

	//set edit cell focus
	private void setFocus(TableItem item) {
		// Clean up any previous editor control
		Control oldEditor = editor.getEditor();
		if (oldEditor != null)
			oldEditor.dispose();

		Text newEditor = new Text(tableInsert, SWT.NONE);
		newEditor.setText(item.getText(editColumn));
		// copy input to table item
		newEditor.addModifyListener(new ModifyListener() {
			public void modifyText(ModifyEvent e) {
				Text text = (Text) editor.getEditor();
				editor.getItem().setText(editColumn, text.getText());
				validate();
			}
		});
		//add listener for Return key pressed
		newEditor.addTraverseListener(new TraverseListener() {
			public void keyTraversed(TraverseEvent e) {
				if (e.character == SWT.CR) {
					Text text = (Text) editor.getEditor();
					editor.getItem().setText(editColumn, text.getText());
					text.dispose();
					e.doit = true;
					int selItem = (tableInsert.getSelectionIndex() + 1)
							% tableInsert.getItemCount();
					if (selItem == 0) {
						getButton(BTN_INSERT_ID).setFocus();
					} else {
						tableInsert.setSelection(selItem);
						setFocus(tableInsert.getItem(selItem));
					}
				} else if (e.character == SWT.ESC) {
					//do nothing
				}
			}
		});
		newEditor.selectAll();
		newEditor.setFocus();
		editor.setEditor(newEditor, item, editColumn);
	}

	public boolean validate() {
		setErrorMessage(null);
		for (int i = 0; i < tableInsert.getItemCount(); i++) {
			TableItem item = tableInsert.getItem(i);
			String type = DataType.getType(item.getText(1));
			String value = item.getText(editColumn);
			if (value.length() > 0) {
				//				boolean result = com.cubrid.cubridmanager.core.CommonTool.validateAttributeValue(
				//						type, value);
				boolean result = DBAttribute.validateAttributeValue(type, value);
				if (!result) {
					String msg = Messages.bind(Messages.insertDataTypeErrorMsg,
							item.getText(1));
					setErrorMessage(msg);
					//					tableInsert.setSelection(item);
					//					setFocus(item);
					return false;
				}
			} else {
				String constaint = item.getText(editColumn - 1);
				if (-1 != constaint.indexOf("Not Null")
						&& -1 == constaint.indexOf("Default:")) {
					String msg = Messages.bind(Messages.insertNotNullErrorMsg,
							item.getText(0));
					setErrorMessage(msg);
					//					setFocus(item);
					return false;
				}
			}
		}
		return true;
	}

	/**
	 * This method initializes sashForm
	 * 
	 */
	private void createSashForm(Composite shlInsert) {
		GridData gridData = new GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessVerticalSpace = true;
		gridData.widthHint = 600;
		gridData.heightHint = 350;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		sashForm = new SashForm(shlInsert, SWT.NONE);
		sashForm.setOrientation(org.eclipse.swt.SWT.VERTICAL);
		sashForm.setLayoutData(gridData);
		createTblInsert();
		txtHistory = new Text(sashForm, SWT.BORDER | SWT.WRAP | SWT.V_SCROLL
				| SWT.READ_ONLY);
		sashForm.setWeights(new int[] { 60, 40 });
		sashForm.pack();
	}

	/**
	 * This method initializes composite
	 * 
	 */
	private void createComposite(Composite shlInsert) {
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

	}

	private String getConstaintString(DBAttribute dbattr) {
		StringBuffer bf = new StringBuffer();
		//add auto increment support
		if (dbattr.getAutoIncrement() != null) {
			bf.append(",Auto Increment");
		} else if (dbattr.isNotNull()) {
			bf.append(",Not Null");
		}
		if (dbattr.isShared()) {
			bf.append(",Shared");
		}
		if (dbattr.isUnique()) {
			bf.append(",Unique");
		} else {
			if (dbattr.getDefault() != null) {
				bf.append(",Default:" + dbattr.getDefault());
			}
		}
		if (bf.length() > 1) {
			return bf.substring(1);
		}
		return bf.toString();
	}

	/**
	 * This method initializes tblInsert
	 * 
	 */
	private void createTblInsert() {

		tableInsert = new Table(sashForm, SWT.BORDER | SWT.FULL_SELECTION);
		tableInsert.setHeaderVisible(true);
		tableInsert.setLinesVisible(true);

		TableLayout layout = new TableLayout();
		layout.addColumnData(new ColumnWeightData(25, 120));
		layout.addColumnData(new ColumnWeightData(55, 160));
		layout.addColumnData(new ColumnWeightData(30, 160));
		layout.addColumnData(new ColumnWeightData(40, 140));
		tableInsert.setLayout(layout);

		TableColumn[] tblColumns = new TableColumn[4];
		tblColumns[0] = new TableColumn(tableInsert, SWT.NONE);
		tblColumns[0].setText(Messages.metaAttribute);
		tblColumns[1] = new TableColumn(tableInsert, SWT.NONE);
		tblColumns[1].setText(Messages.metaDomain);
		tblColumns[2] = new TableColumn(tableInsert, SWT.NONE);
		tblColumns[2].setText(Messages.metaConstaints);
		tblColumns[3] = new TableColumn(tableInsert, SWT.NONE);
		tblColumns[3].setText(Messages.metaValue);

		TableItem item;
		for (int i = 0, n = objrec.getAttributes().size(); i < n; i++) {
			DBAttribute da = (DBAttribute) objrec.getAttributes().get(i);
			item = new TableItem(tableInsert, SWT.NONE);
			item.setText(0, da.getName());
			item.setText(1, DataType.getShownType(da.getType()));
			item.setText(2, getConstaintString(da));
		}
		tblColumns[0].pack();
		tblColumns[1].pack();
		tblColumns[2].pack();
		tblColumns[3].pack();
		tableInsert.pack();

		editor = new TableEditor(tableInsert);
		editor.horizontalAlignment = SWT.LEFT;
		editor.grabHorizontal = true;

		tableInsert.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				// Identify the selected row
				TableItem item = (TableItem) e.item;
				if (item == null)
					return;
				setFocus(item);
			}
		});
	}

	private void clearInsert() {
		for (int i = 0; i < tableInsert.getItemCount(); i++) {
			tableInsert.getItem(i).setText(editColumn, "");
		}
	}

	private void clearHistory() {
		txtHistory.setText("");
	}

	private void createInsertSQL() {
		StringBuffer sql = new StringBuffer("insert into ");
		sql.append("\"" + tableName + "\"");
		sql.append(" (");
		StringBuffer columns = new StringBuffer("");
		StringBuffer values = new StringBuffer("");

		for (int i = 0; i < tableInsert.getItemCount(); i++) {
			String type = DataType.getType(tableInsert.getItem(i).getText(1));
			String value = tableInsert.getItem(i).getText(3);

			if (value.length() > 0) {
				if (values.length() > 0) {
					columns.append(", ");
					values.append(", ");
				}
				columns.append("\"" + tableInsert.getItem(i).getText(0) + "\"");
				try {
					value = DBAttribute.formatValue(type, value);
				} catch (Exception e) {
					logger.error(e);
				}
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
		insertQuery = sql.toString();
	}

}