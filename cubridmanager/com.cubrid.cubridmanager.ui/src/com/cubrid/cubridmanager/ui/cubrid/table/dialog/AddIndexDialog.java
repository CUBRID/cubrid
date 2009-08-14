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

import java.util.List;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CCombo;
import org.eclipse.swt.custom.TableEditor;
import org.eclipse.swt.graphics.Color;
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

import com.cubrid.cubridmanager.core.cubrid.table.model.Constraint;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.model.DataType;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.SWTResourceManager;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;

public class AddIndexDialog extends
		CMTitleAreaDialog {

	SchemaInfo schema;

	private String C_UNIQUE = "UNIQUE"; //$NON-NLS-1$
	private String C_INDEX = "INDEX"; //$NON-NLS-1$
	private String C_RUNIQUE = "REVERSE UNIQUE"; //$NON-NLS-1$
	private String C_RINDEX = "REVERSE INDEX"; //$NON-NLS-1$

	private CCombo oldCombo;
	private Composite g3_composite;
	private Button upBTN;
	private Button downBTN;

	private Table columnTable;
	private Combo IndexTypeCombo;
	private Color black = null;
	private Color gray = null;
	private Color white = null;
	private Group group3;
	private Text indexNameText;
	private Constraint retConstraint;

	private void setInfo() {
		indexNameText.setEnabled(true);
		int idx = 0;
		IndexTypeCombo.add(C_UNIQUE, idx++);
		IndexTypeCombo.add(C_INDEX, idx++);
		IndexTypeCombo.add(C_RUNIQUE, idx++);
		IndexTypeCombo.add(C_RINDEX, idx++);

		IndexTypeCombo.select(0);
		List<DBAttribute> attrList = schema.getLocalAttributes();
		for (int i = 0, n = attrList.size(); i < n; i++) {
			DBAttribute da = attrList.get(i);
			TableItem item = new TableItem(columnTable, SWT.NONE);
			item.setText(1, da.getName());
			item.setText(2, DataType.getShownType(da.getType()));
			item.setText(3, "ASC"); //$NON-NLS-1$
			item.setForeground(gray);
		}
		g3_setBtnEnable();
	}

	/**
	 * 
	 * @param parentShell
	 * @param newSchema
	 * @param database
	 */
	public AddIndexDialog(Shell parentShell, SchemaInfo newSchema) {
		super(parentShell);
		this.schema = newSchema;
		black = SWTResourceManager.getColor(0, 0, 0);
		gray = SWTResourceManager.getColor(128, 128, 128);
		white = SWTResourceManager.getColor(255, 255, 255);
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

		setInfo();
		setTitle(Messages.msgTitleAddIndex);
		setMessage(Messages.msgAddIndex);
		getShell().setText(Messages.titleAddIndex);

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
				false); //$NON-NLS-1$
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			setErrorMessage(null);
			int count = columnTable.getItemCount();

			retConstraint = new Constraint();
			String indexType = IndexTypeCombo.getText();
			retConstraint.setType(indexType);

			for (int i = 0; i < count; i++) {
				TableItem item = columnTable.getItem(i);
				if (item.getChecked()) {
					if (indexType.equals("INDEX") //$NON-NLS-1$
							|| indexType.equals("REVERSE INDEX")
							|| indexType.equals("UNIQUE")
							|| indexType.equals("REVERSE UNIQUE")) { //$NON-NLS-1$
						retConstraint.addAttribute(item.getText(1));
						retConstraint.addRule(item.getText(1) + " " //$NON-NLS-1$
								+ item.getText(3));
					} else {
						retConstraint.addAttribute(item.getText(1));
						retConstraint.addRule(item.getText(1));
					}
				}
			}
			if (retConstraint.getAttributes().size() == 0) {
				setErrorMessage(Messages.errSelectMoreColumns);
				return;
			}
			String indexName = indexNameText.getText().trim();
			String tableName = schema.getClassname();
			if (indexName.equals("")) { //$NON-NLS-1$
				indexName = retConstraint.getDefaultName(tableName);
			}
			retConstraint.setName(indexName);
			List<Constraint> constraintList = schema.getConstraints();
			for (Constraint c : constraintList) {
				if (c.getType().equals("INDEX") //$NON-NLS-1$
						&& c.getType().equals(indexType)) {
					List<String> rules = c.getRules();
					if (rules.equals(retConstraint.getRules())) {
						setErrorMessage(Messages.errExistIndex);
						return;
					}
				} else if (c.getType().equals("REVERSE INDEX") //$NON-NLS-1$
						&& c.getType().equals(indexType)) {
					List<String> attrs = c.getAttributes();
					if (attrs.equals(retConstraint.getAttributes())) {
						setErrorMessage(Messages.errExistReverseIndex);
						return;
					}
				} else if (c.getType().equals("UNIQUE") //$NON-NLS-1$
						&& c.getType().equals(indexType)) {
					if (c.getName().equals(retConstraint.getName())) {
						setErrorMessage(Messages.errExistUniqueIndex);
						return;
					}
				} else if (c.getType().equals("REVERSE UNIQUE") //$NON-NLS-1$
						&& c.getType().equals(indexType)) {
					if (c.getName().equals(retConstraint.getName())) {
						setErrorMessage(Messages.errExistReverseUniqueIndex);
						return;
					}
				}
			}

			this.getShell().dispose();
			this.close();
		} else {
			super.buttonPressed(buttonId);
			this.getShell().dispose();
			this.close();
		}

	}

	private void createComposite(Composite sShell) {
		GridData gridData13 = new org.eclipse.swt.layout.GridData();
		gridData13.horizontalSpan = 2;
		gridData13.verticalSpan = 1;
		GridData gd_constraintNameText = new org.eclipse.swt.layout.GridData();
		gd_constraintNameText.grabExcessHorizontalSpace = true;
		gd_constraintNameText.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gd_constraintNameText.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout4 = new GridLayout();
		gridLayout4.numColumns = 2;
		group3 = new Group(sShell, SWT.NONE);
		group3.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, true));
		group3.setLayout(gridLayout4);
		Label g3_name = new Label(group3, SWT.NONE);
		g3_name.setText(Messages.lblIndexName);
		indexNameText = new Text(group3, SWT.BORDER);
		indexNameText.setLayoutData(gd_constraintNameText);
		Label g3_text2 = new Label(group3, SWT.NONE);
		g3_text2.setLayoutData(new GridData(150, SWT.DEFAULT));
		g3_text2.setText(Messages.lblIndexType);
		g3_create_combo();

		Label g3_attr = new Label(group3, SWT.NONE);
		g3_attr.setText(Messages.lblSelectColumns);
		g3_attr.setLayoutData(gridData13);
		g3_create_table();

		GridData gd_upBTN = new org.eclipse.swt.layout.GridData();
		gd_upBTN.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gd_upBTN.grabExcessHorizontalSpace = true;
		gd_upBTN.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gd_downBTN = new org.eclipse.swt.layout.GridData();
		gd_downBTN.grabExcessHorizontalSpace = true;
		GridLayout gridLayout5 = new GridLayout();
		gridLayout5.numColumns = 2;
		GridData gridData15 = new org.eclipse.swt.layout.GridData();
		gridData15.horizontalSpan = 2;
		gridData15.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData15.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData15.grabExcessHorizontalSpace = true;
		g3_composite = new Composite(sShell, SWT.NONE);
		g3_composite.setLayoutData(gridData15);
		g3_composite.setLayout(gridLayout5);
		upBTN = new Button(g3_composite, SWT.NONE);
		upBTN.setText(Messages.btnUp); //$NON-NLS-1$
		upBTN.setLayoutData(gd_upBTN);
		upBTN.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (oldCombo != null) {
					oldCombo.dispose();
				}

				if (columnTable.getSelectionCount() < 1)
					return;

				int selectionIndex = columnTable.getSelectionIndex();
				if (selectionIndex == 0)
					return;

				boolean tmpCheck;
				String tmpName, tmpDomain, tmpOrder;
				Color tmpColor;
				TableItem selectedItem = columnTable.getSelection()[0];
				TableItem targetItem = columnTable.getItem(selectionIndex - 1);
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
				columnTable.setSelection(selectionIndex - 1);
				g3_setBtnEnable();
			}
		});
		downBTN = new Button(g3_composite, SWT.NONE);
		downBTN.setText(Messages.btnDown); //$NON-NLS-1$
		downBTN.setLayoutData(gd_downBTN);
		downBTN.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (oldCombo != null) {
					oldCombo.dispose();
				}

				if (columnTable.getSelectionCount() < 1)
					return;

				int selectionIndex = columnTable.getSelectionIndex();
				if (selectionIndex == columnTable.getItemCount() - 1)
					return;

				boolean tmpCheck;
				String tmpName, tmpDomain, tmpOrder;
				Color tmpColor;
				TableItem selectedItem = columnTable.getSelection()[0];
				TableItem targetItem = columnTable.getItem(selectionIndex + 1);
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
				columnTable.setSelection(selectionIndex + 1);
				g3_setBtnEnable();
			}
		});

	}

	/**
	 * This method initializes g3_combo
	 * 
	 */
	private void g3_create_combo() {
		GridData gd_indexTypeCombo = new org.eclipse.swt.layout.GridData();
		gd_indexTypeCombo.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gd_indexTypeCombo.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		IndexTypeCombo = new Combo(group3, SWT.NONE | SWT.READ_ONLY);
		IndexTypeCombo.setLayoutData(gd_indexTypeCombo);
		IndexTypeCombo.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				int count = columnTable.getItemCount();
				for (int i = 0; i < count; i++) {
					String indexType = IndexTypeCombo.getText();
					TableItem item = columnTable.getItem(i);
					
					if (indexType.equals(C_INDEX) || indexType.equals(C_UNIQUE)) {
						item.setText(3, "ASC");
						if (oldCombo != null) {
							oldCombo.setText("ASC");
						}
					} else {
						if (oldCombo != null) {
							oldCombo.setText("DESC");
						}
						item.setText(3, "DESC");
					}
				}
			}
		});
	}

	/**
	 * This method initializes g3_table
	 * 
	 */
	private void g3_create_table() {
		GridData gd_columnTable = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.FILL, true, true, 2, 1);
		gd_columnTable.heightHint = 200;

		columnTable = new Table(group3, SWT.FULL_SELECTION | SWT.BORDER
				| SWT.SINGLE | SWT.CHECK);
		columnTable.setHeaderVisible(true);
		columnTable.setLayoutData(gd_columnTable);
		columnTable.setLinesVisible(true);
		columnTable.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				final TableItem item = (TableItem) e.item;
				if (e.detail == SWT.CHECK) {
					if (item.getChecked())
						item.setForeground(black);
					else
						item.setForeground(gray);
					columnTable.setSelection(new TableItem[] { item });
				}
				if (oldCombo != null) {
					oldCombo.dispose();
				}
				String indexType = IndexTypeCombo.getText();
				if (indexType.equals(C_INDEX) || indexType.equals(C_UNIQUE)) {
					final CCombo combo = new CCombo(columnTable, SWT.NONE);
					TableEditor editor = new TableEditor(columnTable);
					combo.setEditable(false);
					combo.setBackground(white);
					combo.add("ASC"); //$NON-NLS-1$
					combo.add("DESC"); //$NON-NLS-1$
					editor.grabHorizontal = true;
					editor.setEditor(combo, item, 3);
					if (item.getText(3).equals("ASC")) { //$NON-NLS-1$
						combo.select(0);
					} else {
						combo.select(1);
					}
					combo.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
						public void widgetSelected(
								org.eclipse.swt.events.SelectionEvent event) {
							item.setText(3, combo.getText());
							//combo.dispose();
						}
					});
					oldCombo = combo;
				}
				g3_setBtnEnable();
			}
		});
		TableColumn tblcol = new TableColumn(columnTable, SWT.LEFT);
		tblcol.setWidth(83);
		tblcol.setText(Messages.colUseColumn);
		tblcol = new TableColumn(columnTable, SWT.LEFT);
		tblcol.setWidth(123);
		tblcol.setText(Messages.colColumnName);
		tblcol = new TableColumn(columnTable, SWT.LEFT);
		tblcol.setWidth(196);
		tblcol.setText(Messages.colDataType);
		TableColumn tblcombocol = new TableColumn(columnTable, SWT.LEFT);
		tblcombocol.setWidth(86);
		tblcombocol.setText(Messages.colOrder);
	}

	private void g3_setBtnEnable() {
		if (columnTable.getSelectionCount() > 0) {
			downBTN.setEnabled(true);
			upBTN.setEnabled(true);
		} else {
			downBTN.setEnabled(false);
			upBTN.setEnabled(false);
		}

		if (columnTable.getSelectionIndex() <= 0)
			upBTN.setEnabled(false);

		if (columnTable.getSelectionIndex() >= columnTable.getItemCount() - 1)
			downBTN.setEnabled(false);
	}

	public Constraint getRetConstraint() {
		return retConstraint;
	}

}