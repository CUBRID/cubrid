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

package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.SWT;
import cubridmanager.Messages;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class CREATEDB_PAGE4Dialog extends WizardPage {
	public static final String PAGE_NAME = "CREATEDB_PAGE4Dialog";
	private Composite comparent = null;
	private Shell dlgShell = null; // @jve:decl-index=0:visual-constraint="10,61"
	private Composite sShell = null;
	private Label label1 = null;
	public Text EDIT_CREATEDB_SUMMARY = null;

	public CREATEDB_PAGE4Dialog() {
		super(PAGE_NAME, Messages.getString("PAGE.CREATEDBPAGE4"), null);
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
		dlgShell.setText(Messages.getString("TITLE.CREATEDB_PAGE4DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		sShell = new Composite(comparent, SWT.NONE);
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.heightHint = 200;
		gridData1.grabExcessHorizontalSpace = true;
		gridData1.grabExcessVerticalSpace = true;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.widthHint = 350;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.widthHint = 300;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessVerticalSpace = true;
		gridData.grabExcessHorizontalSpace = true;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(new GridLayout());
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.YOUHAVECOMPLETED"));
		label1.setLayoutData(gridData);
		EDIT_CREATEDB_SUMMARY = new Text(sShell, SWT.BORDER | SWT.MULTI
				| SWT.WRAP | SWT.V_SCROLL);
		EDIT_CREATEDB_SUMMARY.setEditable(false);
		EDIT_CREATEDB_SUMMARY.setLayoutData(gridData1);
		sShell.getParent().pack();
	}

	public void createControl(Composite parent) {
		comparent = parent;
		createComposite();
		sShell.setParent(parent);
		setControl(sShell);
		setPageComplete(false);
	}

	public boolean isActive() {
		return isCurrentPage();
	}
}
