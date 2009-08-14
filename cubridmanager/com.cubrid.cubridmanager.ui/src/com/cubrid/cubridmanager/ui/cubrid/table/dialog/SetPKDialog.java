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

import java.util.ArrayList;
import java.util.List;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
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
import com.cubrid.cubridmanager.core.cubrid.table.model.SuperClassUtil;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.SWTResourceManager;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

public class SetPKDialog extends
		CMTitleAreaDialog {

	private Group group1 = null;
	private Label g1_name = null;
	private Text g1_text = null;
	private Label g1_attr = null;
	private Table g1_table = null;
	private Composite g1_composite = null;
	private Button g1_upbtn = null;
	private Button g1_dnbtn = null;
	private Color black = null;
	private Color gray = null;

	SchemaInfo schema;
	CubridDatabase database;
	private Constraint oldPK;
	private Constraint newPK = null;
	private String operation = null;

	/**
	 * 
	 * @param parentShell
	 * @param newSchema
	 * @param database
	 */
	public SetPKDialog(Shell parentShell, CubridDatabase database,
			SchemaInfo schema) {
		super(parentShell);
		this.schema = schema;
		this.database = database;
		List<SchemaInfo> supers = SuperClassUtil.getSuperClasses(
				database.getDatabaseInfo(), schema);
		oldPK = schema.getPK(supers);
		black = SWTResourceManager.getColor(0, 0, 0);
		gray = SWTResourceManager.getColor(128, 128, 128);
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

		group1 = new Group(composite, SWT.NONE);
		group1.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, false));
		group1.setLayout(gridLayout2);
		g1_name = new Label(group1, SWT.NONE);
		g1_name.setText(Messages.lblPKName);
		g1_text = new Text(group1, SWT.BORDER);
		g1_text.setLayoutData(gridData5);
		g1_attr = new Label(group1, SWT.NONE);
		g1_attr.setText(Messages.lblSelectColumns);
		g1_attr.setLayoutData(gridData6);
		g1_createtable();
		g1_createcomposite();

		g1_setinfo();
		setTitle(Messages.msgTitleSetPK);
		setMessage(Messages.msgSetPK);
		String msg = Messages.bind(Messages.titleSetPK, schema.getClassname());
		getShell().setText(msg);
		return parentComp;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());

	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, Messages.btnOK, false);
		createButton(parent, IDialogConstants.CANCEL_ID, Messages.btnCancel,
				false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			List<String> selected = new ArrayList<String>();
			for (int i = 0, n = g1_table.getItemCount(); i < n; i++) {
				if (g1_table.getItem(i).getChecked())
					selected.add(g1_table.getItem(i).getText(1));
			}
			boolean isNew = false;
			if (oldPK == null && selected.size() > 0) {
				isNew = true;
				operation = "ADD"; //$NON-NLS-1$
			} else if (oldPK != null && selected.size() == 0) {
				operation = "DEL"; //$NON-NLS-1$
				isNew = false;
			} else if (oldPK != null && selected.size() > 0) {
				isNew = oldPK.getAttributes().equals(selected) ? false : true;
				if (isNew) {
					operation = "MODIFY"; //$NON-NLS-1$
				} else {
					operation = "NO Change"; //$NON-NLS-1$
				}
			}
			if (isNew) {
				newPK = new Constraint();
				newPK.setName(""); //$NON-NLS-1$
				newPK.setType("PRIMARY KEY"); //$NON-NLS-1$
				for (String s : selected) {
					newPK.addAttribute(s);
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

	/**
	 * This method initializes g1_table
	 * 
	 */
	private void g1_createtable() {
		GridData gridData7 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.FILL, true, true, 2, 2);
		gridData7.heightHint = 140;
		g1_table = new Table(group1, SWT.FULL_SELECTION | SWT.BORDER
				| SWT.SINGLE | SWT.CHECK);
		g1_table.setHeaderVisible(true);
		g1_table.setLayoutData(gridData7);
		g1_table.setLinesVisible(true);

		TableColumn tblcol = new TableColumn(g1_table, SWT.LEFT);
		tblcol.setText(Messages.tblcolUseColumn);
		tblcol = new TableColumn(g1_table, SWT.LEFT);
		tblcol.setText(Messages.tblcolColumnName);
		tblcol = new TableColumn(g1_table, SWT.LEFT);
		tblcol.setText(Messages.tblcolDataType);

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
		g1_upbtn.setText(Messages.btnUp);
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
		g1_dnbtn.setText(Messages.btnDown);
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

	private void g1_setinfo() {
		List<String> pkColumns = new ArrayList<String>();
		if (oldPK != null) {
			g1_text.setText(oldPK.getName());
			pkColumns.addAll(oldPK.getAttributes());
			List<TableItem> pkTableItem = new ArrayList<TableItem>();
			for (String s : pkColumns) {
				DBAttribute da = schema.getDBAttributeByName(s, false);
				TableItem item = new TableItem(g1_table, SWT.NONE);
				item.setText(1, da.getName());
				item.setText(2, da.getType());
				item.setForeground(this.black);
				item.setChecked(true);
				pkTableItem.add(item);
			}
			g1_table.setSelection(pkTableItem.toArray(new TableItem[pkTableItem.size()]));
		}
		g1_text.setEnabled(false);
		List<DBAttribute> list = schema.getAttributes();
		for (int i = 0, n = list.size(); i < n; i++) {
			DBAttribute da = list.get(i);
			if (!pkColumns.contains(da.getName())) {
				if (da.getInherit().equals(schema.getClassname())) {
					TableItem item = new TableItem(g1_table, SWT.NONE);
					item.setText(1, da.getName());
					item.setText(2, DataType.getShownType(da.getType()));
					item.setForeground(gray);
				}
			}
		}

		for (int i = 0, n = g1_table.getColumnCount(); i < n; i++) {
			g1_table.getColumn(i).pack();
		}

		g1_setBtnEnable();
	}

	public Constraint getOldPK() {
		return oldPK;
	}

	public Constraint getNewPK() {
		return newPK;
	}

	public String getOperation() {
		return operation;
	}

}
