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

package com.cubrid.cubridmanager.ui.logs.dialog;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.logs.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * 
 * The dialog is used to show Sql log execution result.
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-3-18 created by wuyingshi
 */
public class CasRunnerResultDialog extends
		CMTitleAreaDialog {

	private Text textResult = null;
	private Composite composite;
	private CubridDatabase database = null;
	private StringBuffer result = new StringBuffer("");

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public CasRunnerResultDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);

		composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		layout.numColumns = 5;
		composite.setLayout(layout);

		//dynamicHelp start
		getHelpSystem().setHelp(parentComp,
				CubridManagerHelpContextIDs.brokerSqlLog);
		//dynamicHelp end		

		GridData gridData = new GridData(GridData.FILL_BOTH);
		gridData.horizontalSpan = 5;
		gridData.heightHint = 150;
		textResult = new Text(composite, SWT.BORDER | SWT.MULTI | SWT.V_SCROLL
				| SWT.H_SCROLL);
		textResult.setLayoutData(gridData);
		textResult.setText(result.toString());
		setTitle(Messages.title_casRunnerResultDialog);
		setMessage(Messages.msg_casRunnerResultDialog);
		return parentComp;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.title_casRunnerResultDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, Messages.button_ok, true);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			setReturnCode(buttonId);
			close();
		}
		super.buttonPressed(buttonId);
	}

	/**
	 * 
	 * Get the database.
	 * 
	 * @return
	 */
	public CubridDatabase getDatabase() {
		return database;
	}

	/**
	 * set the database.
	 * 
	 * @param database
	 */
	public void setDatabase(CubridDatabase database) {
		this.database = database;
	}

	/**
	 * get the result.
	 * 
	 * @return
	 */

	public StringBuffer getResult() {
		return result;
	}

	/**
	 * set the result.
	 * 
	 * @param result
	 */
	public void setResult(StringBuffer result) {
		this.result = result;
	}

}
