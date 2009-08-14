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
package com.cubrid.cubridmanager.ui.query.dialog;

import java.sql.SQLException;
import java.util.List;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TableItem;

import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.control.ColumnInfo;
import com.cubrid.cubridmanager.ui.query.control.QueryExecuter;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTrayDialog;

/**
 * View and update the column detail
 * 
 * @author robin 2009-7-6
 */
public class RowDetailDialog extends
		CMTrayDialog {

	private Composite parentComp;
	private List<ColumnInfo> columns;
	private TableItem dataItem;
	private final QueryExecuter qe;
	private StyledText columnValueText;
	private Combo columnCombo;
	private String columnName;
	private int selIndex = -1;
	private boolean isNull;
	private final static String dataNullValueFlag = "NULL";

	public RowDetailDialog(Shell parentShell, List<ColumnInfo> allColumnList,
			TableItem tableItem, String columnName, QueryExecuter qe) {
		super(parentShell);
		this.columnName = columnName;
		this.columns = allColumnList;
		this.dataItem = tableItem;
		this.qe = qe;
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		parentComp = (Composite) super.createDialogArea(parent);

		final Composite composite = new Composite(parentComp, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginWidth = 7;
		layout.marginHeight = 7;
		layout.numColumns = 2;
		composite.setLayout(layout);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		Label columnLabel = new Label(composite, SWT.NONE);
		columnLabel.setText(Messages.lblColumnName);
		columnCombo = new Combo(composite, SWT.SINGLE | SWT.READ_ONLY);
		columnCombo.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		columnCombo.setVisibleItemCount(10);
		columnCombo.addModifyListener(new ModifyListener() {
			public void modifyText(ModifyEvent e) {
				int index = columnCombo.getSelectionIndex();
				if (selIndex >= 0 && !verifyValueChange()) {
					if (CommonTool.openConfirmBox(Messages.cfmUpdateChangedValue)) {
						update();
						setUpdateButtonEnable();
					}
				}
				selIndex = index;
				isNull = dataNullValueFlag.equals(dataItem.getData(""
						+ (selIndex + 1)));

				if (!isNull)
					columnValueText.setText(dataItem.getText(index + 1));
				else
					columnValueText.setText("");
				ColumnInfo column = columns.get(selIndex);
				if (column.getType().equals("OID")
						|| column.getType().equals("CLASS")) {
					columnValueText.setEnabled(false);
				} else {
					columnValueText.setEnabled(true);
				}
				setUpdateButtonEnable();
			}
		});
		final Label descLabel = new Label(composite, SWT.NONE);
		GridData gd = new GridData(GridData.FILL_HORIZONTAL);
		gd.horizontalSpan = 2;
		descLabel.setText(Messages.lblColumnValue);
		descLabel.setLayoutData(gd);
		if (containsOID()) {
			columnValueText = new StyledText(composite, SWT.WRAP | SWT.MULTI
					| SWT.BORDER | SWT.V_SCROLL);
			CommonTool.registerContextMenu(columnValueText, true);
		} else {
			columnValueText = new StyledText(composite, SWT.WRAP | SWT.MULTI
					| SWT.BORDER | SWT.V_SCROLL | SWT.READ_ONLY);
			CommonTool.registerContextMenu(columnValueText, false);
		}
		final GridData columnGroup = new GridData(GridData.FILL_BOTH);
		columnGroup.horizontalSpan = 2;
		columnValueText.setLayoutData(columnGroup);
		columnValueText.addModifyListener(new ModifyListener() {
			public void modifyText(ModifyEvent e) {
				setUpdateButtonEnable();
			}
		});
		initial();
		return parentComp;
	}

	private void initial() {
		int index = 0;
		columnValueText.setEnabled(false);
		for (int i = 0; i < columns.size(); i++) {
			ColumnInfo c = columns.get(i);
			columnCombo.add(c.getName());

			if (c.getName().equals(columnName)) {
				index = i;
			}
			if (columns.get(i).getType().equals("OID")) {
				columnValueText.setEnabled(false);
			} else {
				columnValueText.setEnabled(true);
			}
		}
		columnCombo.select(index);
		isNull = dataNullValueFlag.equals(dataItem.getData("" + (index + 1)));
		if (!isNull)
			columnValueText.setText(dataItem.getText(index + 1));
		else
			columnValueText.setText("");
		selIndex = index;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		getShell().setSize(450, 400);
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleRowDetailDialog);
	}

	private void update() {
		try {
			ColumnInfo columnBean = columns.get(selIndex);
			String[] cols = new String[1];
			String[] vals = new String[1];
			cols[0] = columnBean.getName();
			vals[0] = columnValueText.getText();
			qe.updateValue(dataItem.getText(1), vals, cols);
			if (qe.getQueryEditor().isTransaction()) {
				qe.getQueryEditor().commit();
			}

			dataItem.setText(selIndex + 1, columnValueText.getText());

			if (isNull) {
				dataItem.setData("" + (selIndex + 1), "");
			}
		} catch (SQLException ex) {
			CommonTool.openErrorBox(ex.getErrorCode()
					+ CommonTool.getLineSeparator() + ex.getMessage());
		}
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, Messages.updateBtn, false);
		getButton(IDialogConstants.OK_ID).setEnabled(false);
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			int index = columnCombo.getSelectionIndex();
			if (selIndex >= 0 && !verifyValueChange()) {
				if (CommonTool.openConfirmBox(Messages.cfmUpdateChangedValue)) {
					update();
				} else {
					columnValueText.setText(dataItem.getText(index + 1));
				}
			} else {
				CommonTool.openInformationBox(Messages.msgValueNoChangedTitle,
						Messages.msgValueNoChanged);
			}
			setUpdateButtonEnable();
			return;
		} else if (buttonId == IDialogConstants.CANCEL_ID) {
			if (selIndex >= 0 && !verifyValueChange()) {
			}
		}
		super.buttonPressed(buttonId);
	}

	@Override
	public boolean isHelpAvailable() {
		return false;
	}

	@Override
	protected int getShellStyle() {
		return super.getShellStyle() | SWT.RESIZE;
	}

	private boolean verifyValueChange() {
		String newStr = columnValueText.getText();
		String oldStr = dataItem.getText(selIndex + 1);
		boolean isNull = dataNullValueFlag.equals(dataItem.getData(""
				+ (selIndex + 1)));
		if (isNull)
			return newStr == null || "".equals(newStr);
		else
			return oldStr.equals(newStr);
	}

	private boolean containsOID() {
		for (ColumnInfo column : columns) {
			if (column.getType().equals("OID")) {
				return true;
			}
		}
		return false;
	}

	private void setUpdateButtonEnable() {
		String value = columnValueText.getText();
		String oldValue = dataItem.getText(selIndex + 1);
		if (getButton(IDialogConstants.OK_ID) == null)
			return;
		if (dataNullValueFlag.equals(dataItem.getData((1 + selIndex) + ""))) {
			if (value == null || value.equals(""))
				getButton(IDialogConstants.OK_ID).setEnabled(false);
			else
				getButton(IDialogConstants.OK_ID).setEnabled(true);
		} else {
			if (value == null || !value.equals(oldValue))
				getButton(IDialogConstants.OK_ID).setEnabled(true);
			else
				getButton(IDialogConstants.OK_ID).setEnabled(false);
		}
	}
}
