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

package cubridmanager.query.dialog;

import org.eclipse.swt.SWT;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.query.dialog.QueryPlan;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.GridData;

public class QueryPlanViewer extends Dialog {
	private QueryPlan qe;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="10,43"
	private Composite cmpBtnArea = null;
	private Text txtPlan = null;
	private Button btnClose = null;

	public QueryPlanViewer(QueryPlan qe, Shell parent) {
		super(parent, SWT.DIALOG_TRIM | SWT.MODELESS);
		this.qe = qe;
	}

	public void open(String str) {
		createSShell();

		sShell.setText(Messages.getString("TITLE.QUERYPLAN1"));
		txtPlan.setText(str);

		sShell.pack();
		CommonTool.centerShell(sShell);
		sShell.open();

		Display display = getParent().getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch()) {
				display.sleep();
			}
		}
	}

	/**
	 * This method initializes sShell
	 */
	private void createSShell() {
		sShell = new Shell(getParent(), SWT.SHELL_TRIM);
		createSashForm();
		createCmpBtnArea();
		sShell.setSize(new org.eclipse.swt.graphics.Point(400, 300));
		GridLayout gridLayout = new GridLayout();
		sShell.setLayout(gridLayout);

		sShell.addDisposeListener(new DisposeListener() {
			public void widgetDisposed(DisposeEvent e) {
				qe.isQueryPlanViewerDlgOpen = false;
			}
		});
	}

	/**
	 * This method initializes sashForm
	 * 
	 */
	private void createSashForm() {

		GridData gridData = new GridData(GridData.FILL_BOTH);
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessVerticalSpace = true;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		gridData.heightHint = 500;
		gridData.widthHint = 500;
		txtPlan = new Text(sShell, SWT.BORDER | SWT.MULTI | SWT.WRAP
				| SWT.V_SCROLL);
		txtPlan.setLayoutData(gridData);
		txtPlan.setEditable(false);
		txtPlan.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
	}

	/**
	 * This method initializes cmpBtnArea
	 * 
	 */
	private void createCmpBtnArea() {
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 1;
		GridData gridData1 = new GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		cmpBtnArea = new Composite(sShell, SWT.NONE);
		cmpBtnArea.setLayout(gridLayout1);
		cmpBtnArea.setLayoutData(gridData1);

		GridData gridData4 = new GridData();
		gridData4.widthHint = 75;
		btnClose = new Button(cmpBtnArea, SWT.NONE);
		btnClose.setLayoutData(gridData4);
		btnClose.setText(Messages.getString("BUTTON.CLOSE"));
		btnClose
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
	}

} // @jve:decl-index=0:visual-constraint="12,9"
