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
package com.cubrid.cubridmanager.ui.cubrid.database.dialog;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfoList;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * Delete database confirm
 * 
 * @author robin 2009-3-16
 */
public class DeleteDatabaseConfirmDialog extends
		CMTitleAreaDialog {

	private Label clabel1;
	private Composite parentComp;
	private CubridDatabase database = null;
	private DbSpaceInfoList dbSpaceInfo = null;
	public static int DELETE_ID = 103;
	public static int CONNECT_ID = 0;
	private Text text;

	public DeleteDatabaseConfirmDialog(Shell parentShell) {
		super(parentShell);
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see org.eclipse.jface.dialogs.TitleAreaDialog#createDialogArea(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	protected Control createDialogArea(Composite parent) {
		parentComp = (Composite) super.createDialogArea(parent);
		Composite composite = new Composite(parentComp, SWT.NONE);

		final GridLayout gl_sourceDBComposite = new GridLayout();
		gl_sourceDBComposite.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		gl_sourceDBComposite.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		gl_sourceDBComposite.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		gl_sourceDBComposite.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		gl_sourceDBComposite.numColumns = 2;
		composite.setLayout(gl_sourceDBComposite);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));

		createDirectoryList(composite);

		setTitle(Messages.titleDeleteDbConfirmDialog);
		setMessage(Messages.msgDeleteDbConfirmDialog);

		initial();
		return parentComp;
	}

	/**
	 * create the directory list
	 * 
	 * @param composite
	 */
	private void createDirectoryList(Composite composite) {

		clabel1 = new Label(composite, SWT.SHADOW_IN);
		final GridData gd_clabel1 = CommonTool.createGridData(1, 1, -1, -1);
		clabel1.setLayoutData(gd_clabel1);
		clabel1.setText(Messages.msgInputDbaPassword);
		text = new Text(composite, SWT.BORDER | SWT.PASSWORD);
		text.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

	}

	/**
	 * 
	 * Init the value of dialog field
	 * 
	 */
	private void initial() {
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		getShell().setSize(300, 200);
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleDeleteDbConfirmDialog);

	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			String pass = text.getText();
			if (pass == null)
				pass = "";
			if (!database.getDatabaseInfo().getAuthLoginedDbUserInfo().getName().equalsIgnoreCase(
					"DBA")) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.msgErrorAuth);
				return;
			}
			if (!database.getDatabaseInfo().getAuthLoginedDbUserInfo().getNoEncryptPassword().equals(
					pass)) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.msgErrorPassword);
				return;
			}
		}
		super.buttonPressed(buttonId);
	}

	public CubridDatabase getDatabase() {
		return database;
	}

	public void setDatabase(CubridDatabase database) {
		this.database = database;
	}

	public DbSpaceInfoList getDbSpaceInfo() {
		return dbSpaceInfo;
	}

	public void setDbSpaceInfo(DbSpaceInfoList dbSpaceInfo) {
		this.dbSpaceInfo = dbSpaceInfo;
	}

}
