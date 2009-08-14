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
import java.util.Map;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.query.format.SqlFormattingStrategy;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * The Dialog of add function parameter
 * 
 * @author robin 2009-3-11
 */
public class AddQueryDialog extends CMTitleAreaDialog {

	private Composite parentComp;
	private CubridDatabase database = null;
	private Text parameterNameText = null;
	private int index = 0;
	private boolean newFlag = true;
	private CreateViewDialog parentDialog;

	private static SqlFormattingStrategy formator = new SqlFormattingStrategy(); 
	public AddQueryDialog(Shell parentShell,boolean newFlag,int index,CreateViewDialog parentDialog) {
		super(parentShell);
		this.newFlag = newFlag;
		this.index = index;
		this.parentDialog = parentDialog;
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp, CubridManagerHelpContextIDs.databaseTable);
		final Composite composite = new Composite(parentComp, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));

		createdbNameGroup(composite);
		if (newFlag) {
			setTitle(Messages.titleAddQueryDialog);
			setMessage(Messages.titleAddQueryDialog);
		} else {
			setTitle(Messages.titleEditQueryDialog);
			setMessage(Messages.titleEditQueryDialog);
		}
		initial();
		return parentComp;
	}

	/**
	 * Create Database Name Group
	 * 
	 * @param composite
	 */
	private void createdbNameGroup(Composite composite) {

		final Composite dbnameGroup = new Composite(composite, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginWidth = 0;
		layout.marginHeight = 0;
		// layout.numColumns = 2;
		GridData gd_dbnameGroup = new GridData(GridData.FILL_BOTH);
		dbnameGroup.setLayoutData(gd_dbnameGroup);
		dbnameGroup.setLayout(layout);
		layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		gd_dbnameGroup = new GridData(GridData.FILL_BOTH);
		final Group group = new Group(dbnameGroup, SWT.NONE);
		group.setLayoutData(gd_dbnameGroup);
		group.setLayout(layout);
		group.setText(Messages.grpQuerySpecification);
		parameterNameText = new Text(group, SWT.BORDER | SWT.WRAP | SWT.MULTI|SWT.V_SCROLL);
		parameterNameText.setLayoutData(new GridData(GridData.FILL_BOTH));

	}

	private void initial() {
		if (!newFlag) {
			if (parentDialog.queryListData.size() > index)
				parameterNameText.setText(formatSql(parentDialog.queryListData.get(index).get("0")));
		}

	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		getShell().setSize(400, 480);
		CommonTool.centerShell(getShell());

		getShell().setText(Messages.msgAddQueryDialog);

	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, com.cubrid.cubridmanager.ui.common.Messages.btnOK, false);
		getButton(IDialogConstants.OK_ID).setEnabled(true);
		createButton(parent, IDialogConstants.CANCEL_ID, com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			if (!verify()) {
				return;
			} else {

				// sb.delete(0, sb.length());
				// sb.append(parameterNameText.getText());
			}
		}
		super.buttonPressed(buttonId);
	}

	private boolean verify() {
//		SqlFormattingStrategy formator = new SqlFormattingStrategy(); 
		String sql = parameterNameText.getText();
		if (parentDialog.validateResult(sql,newFlag,index)) {
			if (sql != null && !sql.equals(""))
				
				if (newFlag) {
					Map<String, String> map = new HashMap<String, String>();
					map.put("0", unFormatSql(sql));
					parentDialog.queryListData.add(map);
				} else {
					Map<String, String> map = parentDialog.queryListData.get(index);
					map.put("0", unFormatSql(sql));
					parentDialog.queryListData.set(index, map);
				}
			parentDialog.queryTableViewer.refresh();
			return true;
		}
		return false;
	}

	@Override
	public boolean isHelpAvailable() {
		return true;
	}

	@Override
	protected int getShellStyle() {
		return super.getShellStyle() | SWT.RESIZE;
	}

	public CubridDatabase getDatabase() {
		return database;
	}

	public void setDatabase(CubridDatabase database) {
		this.database = database;
	}
	/**
	 * Format the sql script
	 * 
	 * @param sql
	 * @return
	 */
	private String formatSql(String sql){
		sql=formator.format(sql+";");
		
		return sql.trim().endsWith(";")?sql.trim().substring(0, sql.trim().length()-1):"";
	}
	/**
	 * UnFormat the sql script
	 * 
	 * @param sql
	 * @return
	 */
	private String unFormatSql(String sql) {
		return  sql.replaceAll(System.getProperty("line.separator"), " ");
	}
}
