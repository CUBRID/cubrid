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
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;

import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetTablesTask;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.SWTResourceManager;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

public class AddSuperDialog extends
		CMTitleAreaDialog {

	private Group group1 = null;
	private Label g1_attr = null;
	private Table g1_table = null;
	private Color black = null;
	private Color gray = null;

	SchemaInfo schema;
	CubridDatabase database;
	private List<String> oldSuperclass;
	private List<String> newSuperclass = null;
	private List<String> tableList;

	/**
	 * 
	 * @param parentShell
	 * @param newSchema
	 * @param database
	 */
	public AddSuperDialog(Shell parentShell, CubridDatabase database,
			SchemaInfo schema) {
		super(parentShell);
		this.schema = schema;
		this.database = database;
		oldSuperclass = schema.getSuperClasses();
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

		GridData gridData6 = new GridData(SWT.FILL, SWT.CENTER, false, false);
		GridLayout gridLayout2 = new GridLayout();

		group1 = new Group(composite, SWT.NONE);
		group1.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, true));
		group1.setLayout(gridLayout2);
		g1_attr = new Label(group1, SWT.NONE);
		g1_attr.setText(Messages.lblSelectTables);
		g1_attr.setLayoutData(gridData6);
		g1_createtable();
		g1_createcomposite();

		g1_setinfo();
		setTitle(Messages.msgTitleSetSupers);
		setMessage(Messages.msgSelectSupers);
		getShell().setText(Messages.titleSetSuperTables);
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
			newSuperclass = selected;
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
				SWT.FILL, true, true, 1, 2);
		gridData7.heightHint = 140;
		g1_table = new Table(group1, SWT.FULL_SELECTION | SWT.BORDER
				| SWT.SINGLE | SWT.CHECK);
		g1_table.setHeaderVisible(true);
		g1_table.setLayoutData(gridData7);
		g1_table.setLinesVisible(true);

		TableColumn tblcol = new TableColumn(g1_table, SWT.LEFT);
		tblcol.setWidth(95);
		tblcol.setText(Messages.tblcolUseTable);
		tblcol = new TableColumn(g1_table, SWT.LEFT);
		tblcol.setWidth(201);
		tblcol.setText(Messages.tblcolTableName);

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

			}
		});
	}

	/**
	 * This method initializes g1_composite
	 * 
	 */
	private void g1_createcomposite() {
	}

	private List<String> getTableList() {
		if (null == tableList) {
			CubridDatabase db = database;
			DatabaseInfo dbInfo = db.getDatabaseInfo();
			GetTablesTask task = new GetTablesTask(dbInfo);
			tableList = task.getUserTables();
			tableList.remove(schema.getClassname());
		}
		return tableList;
	}

	private void g1_setinfo() {
		if (oldSuperclass.size() > 0) {
			for (String s : oldSuperclass) {
				TableItem item = new TableItem(g1_table, SWT.NONE);
				item.setText(1, s);
				item.setForeground(this.black);
				item.setChecked(true);
				g1_table.setSelection(new TableItem[] { item });
			}
		}
		List<String> tableList = getTableList();
		for (String s : tableList) {
			if (!oldSuperclass.contains(s)) {
				TableItem item = new TableItem(g1_table, SWT.NONE);
				item.setText(1, s);
				item.setForeground(gray);
			}
		}
		//		for (int i = 0, n = g1_table.getColumnCount(); i < n; i++) {
		//			g1_table.getColumn(i).pack();
		//		}

	}

	public List<String> getNewSuperclass() {
		return newSuperclass;
	}

}
