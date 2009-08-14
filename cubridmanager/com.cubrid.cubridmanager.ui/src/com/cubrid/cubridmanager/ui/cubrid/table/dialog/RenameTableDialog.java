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
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;

public class RenameTableDialog extends
		CMTitleAreaDialog {

	String tableName;
	List<String> tableList;
	boolean isTable;

	Text newTableText = null;
	String newSchemaName;

	private String tableOrView;
	private Composite composite;
	private Label label1 = null;

	public RenameTableDialog(Shell parentShell, String table, boolean isTable,
			List<String> tableList) {
		super(parentShell);
		this.tableName = table;
		this.tableList = tableList;
		this.isTable = isTable;
		if (isTable) {
			tableOrView = Messages.renameTable;
		} else {
			tableOrView = Messages.renameView;
		}
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(
				Messages.bind(Messages.renameShellTitle, tableOrView, tableName));
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, Messages.renameOKBTN,
				false);
		createButton(parent, IDialogConstants.CANCEL_ID,
				Messages.renameCancelBTN, false);
		getButton(IDialogConstants.OK_ID).setEnabled(false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			newSchemaName = newTableText.getText().trim();
			this.getShell().dispose();
			this.close();
		} else if (buttonId == IDialogConstants.CANCEL_ID) {
			super.buttonPressed(buttonId);
			this.getShell().dispose();
			this.close();
		}
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);
		if (isTable) {
			getHelpSystem().setHelp(parentComp,
					CubridManagerHelpContextIDs.databaseTable);
		} else {
			getHelpSystem().setHelp(parentComp,
					CubridManagerHelpContextIDs.databaseView);
		}
		composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));

		GridLayout layout = new GridLayout();
		layout.numColumns = 3;
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);

		label1 = new Label(composite, SWT.LEFT);
		label1.setText(Messages.bind(Messages.renameNewTableName, tableOrView));
		GridData data = new GridData();
		data.widthHint = 120;
		data.horizontalSpan = 1;
		data.verticalSpan = 1;
		label1.setLayoutData(data);

		newTableText = new Text(composite, SWT.BORDER);
		data = new GridData();
		data.horizontalSpan = 2;
		data.verticalSpan = 1;
		data.grabExcessHorizontalSpace = true;
		data.verticalAlignment = GridData.FILL;
		data.horizontalAlignment = GridData.FILL;

		newTableText.setLayoutData(data);
		newTableText.setText(tableName);
		newTableText.selectAll();
		newTableText.setFocus();
		newTableText.addModifyListener(new ModifyListener() {
			public void modifyText(ModifyEvent e) {
				setErrorMessage(null);
				getButton(IDialogConstants.OK_ID).setEnabled(false);
				String newTable = newTableText.getText();
				String retstr = CommonTool.validateCheckInIdentifier(newTable);
				if (retstr.length() > 0) {
					setErrorMessage(Messages.bind(
							Messages.renameInvalidTableNameMSG, tableOrView,
							newTable));
					return;
				}
				if (-1 != tableList.indexOf(newTable.toLowerCase())) {
					if (isTable) {
						setErrorMessage(Messages.bind(Messages.errExistTable,
								newTable));
					} else {
						setErrorMessage(Messages.bind(Messages.errExistView,
								newTable));
					}
					return;
				}
				getButton(IDialogConstants.OK_ID).setEnabled(true);
			}
		});

		setTitle(Messages.bind(Messages.renameMSGTitle, tableOrView));
		setMessage(Messages.bind(Messages.renameDialogMSG, tableOrView));
		return parent;
	}

	public String getNewSchemaName() {
		return newSchemaName;

	}
}
