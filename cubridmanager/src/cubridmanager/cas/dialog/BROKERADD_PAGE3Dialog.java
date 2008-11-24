/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

package cubridmanager.cas.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.SWT;
import cubridmanager.Messages;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class BROKERADD_PAGE3Dialog extends WizardPage {
	public static final String PAGE_NAME = "BROKERADD_PAGE3Dialog";
	private Composite comparent = null;
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Label label1 = null;
	private Text EDIT_BROKER_ADD_SUM = null;

	public BROKERADD_PAGE3Dialog() {
		super(PAGE_NAME, Messages.getString("TITLE.BROKERADD_PAGE3DIALOG"),
				null);
	}

	public void createControl(Composite parent) {
		comparent = parent;
		createComposite();
		sShell.setParent(parent);
		setControl(sShell);
		setPageComplete(false); // <-last page is false, others true. 
	}

	public int doModal() {
		createSShell();
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.BROKERADD_PAGE3DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData26 = new org.eclipse.swt.layout.GridData();
		gridData26.grabExcessHorizontalSpace = true;
		gridData26.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData26.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData26.heightHint = 250;
		gridData26.widthHint = 400;
		gridData26.grabExcessVerticalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		gridData.grabExcessVerticalSpace = true;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
		sShell.setLayout(new GridLayout());
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.REPORT"));
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData);
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.YOUCOMPLETED"));
		EDIT_BROKER_ADD_SUM = new Text(group1, SWT.BORDER | SWT.MULTI
				| SWT.WRAP);
		EDIT_BROKER_ADD_SUM.setEditable(false);
		EDIT_BROKER_ADD_SUM.setLayoutData(gridData26);
	}

	public void setinfo() {
		String summary = "================"
				+ Messages.getString("MSG.ADDBROKERBASICOPTION")
				+ "====================\r\n\r\n";
		String chkstr = null;
		chkstr = BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_BNAME.getText().trim();
		summary += Messages.getString("MSG.ADDBROKERBROKERNAMEIS") + " ";
		if (chkstr.length() > 0)
			summary += chkstr + "\r\n";
		else
			summary += Messages.getString("MSG.NOTSPECIFIED") + "\r\n";

		chkstr = BROKERADD_PAGE1Dialog.COMBO_BROKER_ADD_ASTYPE.getText();
		summary += Messages.getString("MSG.ADDBROKERBROKERASTYPEIS") + chkstr
				+ "\r\n";

		if (!chkstr.equals("CAS")) {
			chkstr = BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_APPL_ROOT.getText()
					.trim();
			summary += Messages.getString("MSG.ADDBROKERASROOTDIRECTORYIS")
					+ " ";
			if (chkstr.length() > 0)
				summary += chkstr + "\r\n";
			else
				summary += Messages.getString("MSG.NOTSPECIFIED") + "\r\n";
		}

		chkstr = BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_ASMIN.getText().trim();
		summary += Messages.getString("MSG.ADDBROKERMINIMUMASNUMBERIS") + " ";
		if (chkstr.length() > 0)
			summary += chkstr + "\r\n";
		else
			summary += Messages.getString("MSG.NOTSPECIFIED") + "\r\n";

		chkstr = BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_ASMAX.getText().trim();
		summary += Messages.getString("MSG.ADDBROKERMAXIMUMASNUMBERIS") + " ";
		if (chkstr.length() > 0)
			summary += chkstr + "\r\n";
		else
			summary += Messages.getString("MSG.NOTSPECIFIED") + "\r\n";

		chkstr = BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_PORT.getText().trim();
		summary += Messages.getString("MSG.ADDBROKERBROKERPORTNUMBERIS") + " ";
		if (chkstr.length() > 0)
			summary += chkstr + "\r\n";
		else
			summary += Messages.getString("MSG.NOTSPECIFIED") + "\r\n";

		summary += "================"
				+ Messages.getString("MSG.ADDBROKERADVANCEDOPTION")
				+ "====================\r\n\r\n";
		for (int i = 0; i < BROKERADD_PAGE2Dialog.LIST_BROKERADD_LIST1
				.getItemCount(); i++) {
			TableItem ti = BROKERADD_PAGE2Dialog.LIST_BROKERADD_LIST1
					.getItem(i);
			summary += ti.getText(0) + ":" + ti.getText(1) + "\r\n";
		}
		EDIT_BROKER_ADD_SUM.setText(summary);
	}
}
