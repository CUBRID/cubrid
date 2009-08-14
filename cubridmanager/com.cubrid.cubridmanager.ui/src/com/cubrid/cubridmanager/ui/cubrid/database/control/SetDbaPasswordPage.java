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
package com.cubrid.cubridmanager.ui.cubrid.database.control;

import org.eclipse.jface.dialogs.IPageChangedListener;
import org.eclipse.jface.dialogs.PageChangedEvent;
import org.eclipse.jface.wizard.IWizardPage;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;

/**
 * 
 * Set dba password page
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-18 created by pangqiren
 */
public class SetDbaPasswordPage extends
		WizardPage implements
		ModifyListener,
		IPageChangedListener {

	public static String PAGENAME = "CreateDatabaseWizard/SetDbaPasswodPage";
	private Text passwordText;
	private Text passwordConfirmText;

	/**
	 * The constructor
	 */
	public SetDbaPasswordPage() {
		super(PAGENAME);
		setPageComplete(false);
	}

	/**
	 * Creates the controls for this page
	 */
	public void createControl(Composite parent) {
		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		whs.setHelp(parent, CubridManagerHelpContextIDs.databaseCreate);

		Composite composite = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = 10;
		layout.marginWidth = 10;
		composite.setLayout(layout);
		GridData gridData = new GridData(GridData.FILL_BOTH);
		composite.setLayoutData(gridData);

		createPasswordGroup(composite);

		setTitle(Messages.titleWizardPageSetDbaPass);
		setMessage(Messages.msgWizardPageSetDbaPass);
		setControl(composite);

	}

	/**
	 * 
	 * Create volume group area
	 * 
	 * @param parent
	 */
	private void createPasswordGroup(Composite parent) {
		Group passwordGroup = new Group(parent, SWT.NONE);
		passwordGroup.setText(Messages.grpSetPassword);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		passwordGroup.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		layout.numColumns = 4;
		passwordGroup.setLayout(layout);

		Label passwordLabel = new Label(passwordGroup, SWT.LEFT | SWT.WRAP);
		passwordLabel.setText(Messages.lblPassword);
		passwordLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		passwordText = new Text(passwordGroup, SWT.BORDER | SWT.PASSWORD);
		passwordText.setTextLimit(ValidateUtil.MAX_PASSWORD_LENGTH);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 3;
		passwordText.setLayoutData(gridData);
		passwordText.addModifyListener(this);

		Label passwordConfirmLabel = new Label(passwordGroup, SWT.LEFT
				| SWT.WRAP);
		passwordConfirmLabel.setText(Messages.lblPasswordConfirm);
		passwordConfirmLabel.setLayoutData(CommonTool.createGridData(1, 1, -1,
				-1));

		passwordConfirmText = new Text(passwordGroup, SWT.BORDER | SWT.PASSWORD);
		passwordConfirmText.setTextLimit(ValidateUtil.MAX_PASSWORD_LENGTH);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 3;
		passwordConfirmText.setLayoutData(gridData);
		passwordConfirmText.addModifyListener(this);
	}

	public void modifyText(ModifyEvent e) {
		String password = passwordText.getText();
		String passwordConfirm = passwordConfirmText.getText();
		boolean isValidPassword = password.trim().length() > 0
				&& password.indexOf(" ") < 0 && password.length() >= 4
				&& password.length() <= ValidateUtil.MAX_PASSWORD_LENGTH;
		boolean isValidPasswordConfirm = passwordConfirm.trim().length() > 0
				&& passwordConfirm.indexOf(" ") < 0
				&& passwordConfirm.length() >= 4
				&& passwordConfirm.length() <= ValidateUtil.MAX_PASSWORD_LENGTH;
		boolean isEqualPassword = password.equals(passwordConfirm);
		if (!isValidPassword) {
			setErrorMessage(Messages.errPassword);
		} else if (!isValidPasswordConfirm) {
			setErrorMessage(Messages.errPasswordConfirm);
		} else if (!isEqualPassword) {
			setErrorMessage(Messages.errNotEqualPass);
		}
		boolean isEnabled = isValidPassword && isValidPasswordConfirm
				&& isEqualPassword;
		if (isEnabled) {
			setErrorMessage(null);
		}
		setPageComplete(isEnabled);
	}

	public void pageChanged(PageChangedEvent event) {
		IWizardPage page = (IWizardPage) event.getSelectedPage();
		if (page.getName().equals(PAGENAME)) {
			if (passwordText != null && !passwordText.isDisposed()) {
				passwordText.selectAll();
				passwordText.setFocus();
			}
		}
	}

	public String getPassword() {
		return passwordText.getText();
	}
}
