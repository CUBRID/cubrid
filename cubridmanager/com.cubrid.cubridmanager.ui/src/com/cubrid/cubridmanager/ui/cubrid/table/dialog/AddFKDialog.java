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

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CCombo;
import org.eclipse.swt.custom.TableEditor;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.Constraint;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.model.DataType;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.SuperClassUtil;
import com.cubrid.cubridmanager.core.cubrid.table.model.SystemNamingUtil;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetTablesTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ConstraintType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

public class AddFKDialog extends
		CMTitleAreaDialog {

	private Text newColumnNameText;
	private Composite composite_FK;
	private Combo foreignTableCombo;
	private Table pkForeignTable;
	private Table fkTable;
	CubridDatabase database;
	SchemaInfo schema;
	private List<String> tableList;
	private Constraint retFK;

	/**
	 * 
	 * @param parentShell
	 * @param newSchema
	 * @param database
	 */
	public AddFKDialog(Shell parentShell, CubridDatabase database,
			SchemaInfo newSchema) {
		super(parentShell);
		this.database = database;
		this.schema = newSchema;
	}

	private void init() {
		List<String> list = getTableList();
		//allow a table has one or more FK referencing to another table		
		//		List<String> foreignTablelist = schema.getForeignTables();
		for (String table : list) {
			//			if (!foreignTablelist.contains(table)) {
			foreignTableCombo.add(table);
			//			}
		}
		foreignTableCombo.select(0);
		getPKTableData();
		List<DBAttribute> attrList = schema.getLocalAttributes();
		for (int i = 0, n = attrList.size(); i < n; i++) {
			DBAttribute da = attrList.get(i);
			TableItem item = new TableItem(fkTable, SWT.NONE);
			item.setText(0, da.getName());
			item.setText(1, DataType.getShownType(da.getType()));
			item.setText(2, ""); //$NON-NLS-1$
		}
	}

	private List<String> getTableList() {
		if (null == tableList) {
			CubridDatabase db = database;
			DatabaseInfo dbInfo = db.getDatabaseInfo();
			GetTablesTask task = new GetTablesTask(dbInfo);
			tableList = task.getUserTables();
		}
		return tableList;
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

		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.verticalSpan = 1;
		gridData6.horizontalSpan = 2;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.grabExcessHorizontalSpace = true;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 2;

		createComposite(composite);
		setTitle(Messages.msgTitleAddFK);
		setMessage(Messages.msgAddFK);
		getShell().setText(Messages.titleAddFK);

		init();
		this.getShell().pack();
		return parentComp;
	}

	@Override
	protected void constrainShellSize() {
		getShell().pack();
		super.constrainShellSize();
		CommonTool.centerShell(getShell());

	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, Messages.btnOK, true);
		createButton(parent, IDialogConstants.CANCEL_ID, Messages.btnCancel,
				false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			if (checkFields() == 1)
				return;
			retFK = new Constraint();
			retFK.setType(ConstraintType.FOREIGNKEY.getText());

			int itemCount = fkTable.getItemCount();
			Map<String, String> map = new HashMap<String, String>();
			for (int i = 0; i < itemCount; i++) {
				TableItem item = fkTable.getItem(i);
				String pkColumn = item.getText(2).trim();
				if (!pkColumn.equals("")) { //$NON-NLS-1$
					map.put(pkColumn, item.getText(0));
				}
			}
			itemCount = pkForeignTable.getItemCount();
			for (int i = 0; i < itemCount; i++) {
				TableItem item = pkForeignTable.getItem(i);
				String pkColumn = item.getText(0).trim();
				assert (pkColumn != null && !pkColumn.equals(""));
				String refColumn = map.get(pkColumn);
				assert (refColumn != null);
				retFK.addAttribute(refColumn);
			}

			String fkName = fkNameText.getText().trim();
			if (fkName.equals("")) { //$NON-NLS-1$
				fkName = SystemNamingUtil.getFKName(schema.getClassname(),
						retFK.getAttributes());
			}
			retFK.setName(fkName);

			String foreignTable = foreignTableCombo.getText();
			retFK.addRule("REFERENCES " + foreignTable); //$NON-NLS-1$

			for (Constraint fk : schema.getFKConstraints()) {
				if (fk.getAttributes().equals(retFK.getAttributes())) {
					setErrorMessage(Messages.errColumnExistInFK);
					return;
				}
			}

			for (int i = 0; i < deleteBTNs.length; i++) {
				if (deleteBTNs[i].getSelection()) {
					retFK.addRule("ON DELETE " + buttonMap.get(deleteBTNs[i])); //$NON-NLS-1$
					break;
				}
			}

			for (int i = 0; i < updateBTNs.length; i++) {
				if (updateBTNs[i].getSelection()) {
					retFK.addRule("ON UPDATE " + buttonMap.get(updateBTNs[i])); //$NON-NLS-1$
					break;
				}
			}
			if (onCacheObjectButton.getSelection()) {
				retFK.addRule("ON CACHE OBJECT " //$NON-NLS-1$
						+ newColumnNameText.getText().trim());
			}

			this.getShell().dispose();
			this.close();
		} else {
			super.buttonPressed(buttonId);
			this.getShell().dispose();
			this.close();
		}

	}

	private CCombo OldCombo;
	private Button onCacheObjectButton;
	private Text fkNameText;
	private Button[] updateBTNs;
	private Button[] deleteBTNs;
	private SchemaInfo refSchema;
	private HashMap<Button, String> buttonMap;

	private void createComposite(Composite sShell) {
		composite_FK = new Composite(sShell, SWT.NONE);
		composite_FK.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, true));
		final GridLayout gridLayout_4 = new GridLayout();
		gridLayout_4.numColumns = 2;
		composite_FK.setLayout(gridLayout_4);

		Label g2_name = new Label(composite_FK, SWT.NONE);
		g2_name.setLayoutData(new GridData(170, SWT.DEFAULT));
		g2_name.setText(Messages.lblFKName);
		fkNameText = new Text(composite_FK, SWT.BORDER);
		fkNameText.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));

		Label g2_target = new Label(composite_FK, SWT.NONE);
		g2_target.setText(Messages.lblFTableName);

		foreignTableCombo = new Combo(composite_FK, SWT.NONE | SWT.READ_ONLY);
		foreignTableCombo.setLayoutData(new GridData(SWT.FILL, SWT.CENTER,
				false, false));
		foreignTableCombo.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				getPKTableData();
			}
		});

		Label g2_targetdef = new Label(composite_FK, SWT.NONE);
		g2_targetdef.setLayoutData(new GridData(SWT.LEFT, SWT.CENTER, false,
				false, 2, 1));
		g2_targetdef.setText(Messages.lblFTablePK);

		pkForeignTable = new Table(composite_FK, SWT.FULL_SELECTION
				| SWT.BORDER | SWT.SINGLE);
		final GridData gd_pkForeignTable = new GridData(SWT.FILL, SWT.CENTER,
				true, false, 2, 1);
		gd_pkForeignTable.heightHint = 70;
		pkForeignTable.setLayoutData(gd_pkForeignTable);
		pkForeignTable.setHeaderVisible(true);
		pkForeignTable.setLinesVisible(true);

		TableColumn tblcol = new TableColumn(pkForeignTable, SWT.LEFT);
		tblcol.setWidth(120);
		tblcol.setText(Messages.colName);
		tblcol = new TableColumn(pkForeignTable, SWT.LEFT);
		tblcol.setWidth(221);
		tblcol.setText(Messages.colDataType);

		Label g2_attr = new Label(composite_FK, SWT.NONE);
		g2_attr.setLayoutData(new GridData(SWT.LEFT, SWT.CENTER, false, false,
				2, 1));
		g2_attr.setText(Messages.lblSelectColumns);
		fkTable = new Table(composite_FK, SWT.FULL_SELECTION | SWT.BORDER
				| SWT.SINGLE);
		final GridData gd_fkTable = new GridData(SWT.FILL, SWT.FILL, false,
				true, 2, 1);
		fkTable.setLayoutData(gd_fkTable);
		fkTable.setHeaderVisible(true);
		fkTable.setLinesVisible(true);
		fkTable.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				final TableItem item = (TableItem) e.item;
				setErrorMessage(null);
				if (OldCombo != null) {
					OldCombo.dispose();
				}
				final CCombo combo = new CCombo(fkTable, SWT.NONE);
				TableEditor editor = new TableEditor(fkTable);
				combo.setEditable(false);
				combo.add(""); //$NON-NLS-1$
				if (pkForeignTable.getItemCount() != 0) {
					for (int i = 0, n = pkForeignTable.getItemCount(); i < n; i++)
						combo.add(pkForeignTable.getItem(i).getText(0));
				}

				editor.grabHorizontal = true;
				editor.setEditor(combo, item, 2);
				combo.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent event) {
						String pkColumn = combo.getText();
						if (pkColumn.equals("")) {
							item.setText(2, pkColumn);
							return;
						}
						String dataType = DataType.getShownType(refSchema.getDBAttributeByName(
								pkColumn, false).getType());

						if (dataType.equals(item.getText(1))) {
							item.setText(2, pkColumn);
						} else {
							setErrorMessage(Messages.errDataTypeInCompatible);
							combo.setFocus();
						}
					}
				});
				OldCombo = combo;
			}
		});
		tblcol = new TableColumn(fkTable, SWT.LEFT);
		tblcol.setWidth(122);
		tblcol.setText(Messages.colName);
		tblcol = new TableColumn(fkTable, SWT.LEFT);
		tblcol.setWidth(220);
		tblcol.setText(Messages.colDataType);
		TableColumn tblcombocol = new TableColumn(fkTable, SWT.LEFT);
		tblcombocol.setWidth(171);
		tblcombocol.setText(Messages.colRefColumn);

		final Composite composite_1 = new Composite(composite_FK, SWT.NONE);
		composite_1.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false, 2, 1));
		final GridLayout gridLayout = new GridLayout();
		gridLayout.makeColumnsEqualWidth = true;
		gridLayout.numColumns = 2;
		composite_1.setLayout(gridLayout);

		final Group onUpdateGroup = new Group(composite_1, SWT.NONE);
		onUpdateGroup.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, false,
				false));
		onUpdateGroup.setText(Messages.grpOnUpdate);
		onUpdateGroup.setLayout(new GridLayout());

		updateBTNs = new Button[3];
		buttonMap = new HashMap<Button, String>();

		updateBTNs[0] = new Button(onUpdateGroup, SWT.RADIO);
		updateBTNs[0].setLayoutData(new GridData(100, SWT.DEFAULT));
		updateBTNs[0].setText("CASCADE"); //$NON-NLS-1$
		updateBTNs[0].setEnabled(false);

		updateBTNs[1] = new Button(onUpdateGroup, SWT.RADIO);
		updateBTNs[1].setSelection(true);
		updateBTNs[1].setText("RESTRICT"); //$NON-NLS-1$

		updateBTNs[2] = new Button(onUpdateGroup, SWT.RADIO);
		updateBTNs[2].setText("NO ACTION"); //$NON-NLS-1$
		buttonMap.put(updateBTNs[0], "CASCADE"); //$NON-NLS-1$
		buttonMap.put(updateBTNs[1], "RESTRICT"); //$NON-NLS-1$
		buttonMap.put(updateBTNs[2], "NO ACTION"); //$NON-NLS-1$

		final Group onUpdateGroup_1 = new Group(composite_1, SWT.NONE);
		final GridData gd_onUpdateGroup_1 = new GridData(SWT.FILL, SWT.CENTER,
				false, false);
		onUpdateGroup_1.setLayoutData(gd_onUpdateGroup_1);
		onUpdateGroup_1.setLayout(new GridLayout());
		onUpdateGroup_1.setText(Messages.grpOnDelete);

		deleteBTNs = new Button[3];
		deleteBTNs[0] = new Button(onUpdateGroup_1, SWT.RADIO);
		deleteBTNs[0].setLayoutData(new GridData(100, SWT.DEFAULT));
		deleteBTNs[0].setText("CASCADE"); //$NON-NLS-1$

		deleteBTNs[1] = new Button(onUpdateGroup_1, SWT.RADIO);
		deleteBTNs[1].setSelection(true);
		deleteBTNs[1].setText("RESTRICT"); //$NON-NLS-1$

		deleteBTNs[2] = new Button(onUpdateGroup_1, SWT.RADIO);
		deleteBTNs[2].setText("NO ACTION"); //$NON-NLS-1$

		buttonMap.put(deleteBTNs[0], "CASCADE"); //$NON-NLS-1$
		buttonMap.put(deleteBTNs[1], "RESTRICT"); //$NON-NLS-1$
		buttonMap.put(deleteBTNs[2], "NO ACTION"); //$NON-NLS-1$

		final Group group = new Group(composite_1, SWT.NONE);
		final GridData gd_group = new GridData(SWT.FILL, SWT.TOP, true, false,
				2, 1);
		group.setLayoutData(gd_group);
		final GridLayout gridLayout_1 = new GridLayout();
		gridLayout_1.numColumns = 3;
		group.setLayout(gridLayout_1);

		onCacheObjectButton = new Button(group, SWT.CHECK);

		onCacheObjectButton.setLayoutData(new GridData());
		onCacheObjectButton.setText(Messages.btnOnCacheObject);

		final Label cacheObjectColumnLabel = new Label(group, SWT.NONE);
		final GridData gd_cacheObjectColumnLabel = new GridData();
		gd_cacheObjectColumnLabel.horizontalIndent = 20;
		cacheObjectColumnLabel.setLayoutData(gd_cacheObjectColumnLabel);
		cacheObjectColumnLabel.setText(Messages.lblCacheColumnName);

		newColumnNameText = new Text(group, SWT.BORDER);
		newColumnNameText.setEnabled(false);
		final GridData gd_newColumnNameText = new GridData(SWT.FILL,
				SWT.CENTER, true, false);
		newColumnNameText.setLayoutData(gd_newColumnNameText);

		onCacheObjectButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(final SelectionEvent e) {
				if (onCacheObjectButton.getSelection()) {
					newColumnNameText.setEnabled(true);
				} else {
					newColumnNameText.setText(""); //$NON-NLS-1$
					newColumnNameText.setEnabled(false);
				}
			}
		});

	}

	public int checkFields() {
		int ret = 0;
		setErrorMessage(null);
		int pkItemCount = pkForeignTable.getItemCount();
		if (pkItemCount == 0) {
			setErrorMessage(Messages.errSelectTableWithPK);
			return 1;
		}
		int fkItemCount = fkTable.getItemCount();
		if (fkItemCount == 0) {
			setErrorMessage(Messages.errNoColumnInTable + schema.getClassname());
			return 1;
		}
		Map<String, String> pk2fkMap = new HashMap<String, String>();
		Map<String, String> fk2pkMap = new HashMap<String, String>();
		for (int i = 0; i < fkItemCount; i++) {
			TableItem item = fkTable.getItem(i);
			if (!item.getText(2).equals("")) { //$NON-NLS-1$
				pk2fkMap.put(item.getText(2), item.getText(0));
				fk2pkMap.put(item.getText(0), item.getText(2));
			}
		}
		if (pk2fkMap.size() < pkItemCount) {
			int diff = pkItemCount - pk2fkMap.size();
			if (diff == 1) {
				setErrorMessage(Messages.errOneColumnNotSet);
			} else {
				String msg = Messages.bind(Messages.errMultColumnsNotSet, diff);
				setErrorMessage(msg);
				//				Messages.errMultColumnsNotSet + diff
				//						+ " columns having not been set!"); //$NON-NLS-1$
			}
			return 1;
		}
		if (fk2pkMap.size() > pkItemCount) {
			setErrorMessage(Messages.errSelectMoreColumn);
			return 1;
		}
		if (onCacheObjectButton.getSelection()) {
			if (newColumnNameText.getText().trim().equals("")) { //$NON-NLS-1$
				setErrorMessage(Messages.errNoNameForCacheColumn);
				return 1;
			}
			;
		}
		return ret;

	}

	private void getPKTableData() {
		pkForeignTable.removeAll();

		if (OldCombo != null) {
			OldCombo.dispose();
		}

		String refTable = foreignTableCombo.getText();
		refSchema = database.getDatabaseInfo().getSchemaInfo(refTable);
		List<SchemaInfo> supers = SuperClassUtil.getSuperClasses(
				database.getDatabaseInfo(), refSchema);
		Constraint pk = refSchema.getPK(supers);
		if (pk != null) {
			List<String> pkAttrs = pk.getAttributes();
			for (String attr : pkAttrs) {
				DBAttribute da = (DBAttribute) refSchema.getDBAttributeByName(
						attr, false);
				TableItem item = new TableItem(pkForeignTable, SWT.NONE);
				item.setText(0, da.getName());
				item.setText(1, DataType.getShownType(da.getType()));
			}
		}

		if (fkTable.getItemCount() > 0) {
			TableItem items[] = fkTable.getItems();
			for (int i = 0, n = items.length; i < n; i++)
				items[i].setText(2, ""); //$NON-NLS-1$
		}

	}

	public Constraint getRetFK() {
		return retFK;
	}

}