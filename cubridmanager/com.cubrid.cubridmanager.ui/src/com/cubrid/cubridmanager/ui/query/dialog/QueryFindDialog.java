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

import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.control.QuerySourceViewer;
import com.cubrid.cubridmanager.ui.spi.CommonTool;

/**
 * query editor find dialog
 * 
 * @author wangsl 2009-4-15
 */
public class QueryFindDialog extends
		Dialog {

	private Text replace;
	private Text find;
	protected Object result;
	protected Shell shell;

	private static QueryFindDialog dialog = null;

	private static QuerySourceViewer sourceViewer;

	//private static String findString;

	public static QueryFindDialog getFindDialog(QuerySourceViewer viewer,
			String text) {
		sourceViewer = viewer;
		//findString = text;
		if (dialog == null) {
			dialog = new QueryFindDialog(
					PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell(),
					SWT.NONE);
		}
		return dialog;
	}

	/**
	 * Create the dialog
	 * 
	 * @param parent
	 * @param style
	 */
	private QueryFindDialog(Shell parent, int style) {
		super(parent, style);
	}

	/**
	 * Open the dialog
	 * 
	 * @return the result
	 */
	public Object open() {
		createContents();
		shell.open();
		shell.layout();
		Display display = getParent().getDisplay();
		while (!shell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return result;
	}

	/**
	 * Create contents of the dialog
	 */
	protected void createContents() {
		shell = new Shell(getParent(), SWT.DIALOG_TRIM | SWT.MODELESS);
		final GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		shell.setLayout(gridLayout);
		shell.setSize(450, 214);
		shell.setText(Messages.findTip);

		final Composite composite = new Composite(shell, SWT.NONE);
		final GridData gd_composite = new GridData(SWT.FILL, SWT.FILL, true,
				true);
		gd_composite.heightHint = 174;
		gd_composite.widthHint = 296;
		composite.setLayoutData(gd_composite);
		composite.setLayout(new GridLayout());

		final Group group = new Group(composite, SWT.NONE);
		group.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
		final GridLayout gridLayout_1 = new GridLayout();
		gridLayout_1.marginHeight = 0;
		gridLayout_1.numColumns = 2;
		group.setLayout(gridLayout_1);

		final Label findWhatLabel = new Label(group, SWT.NONE);
		findWhatLabel.setText(Messages.findWhat);

		find = new Text(group, SWT.BORDER);
		final GridData gd_find = new GridData(SWT.FILL, SWT.CENTER, true, false);
		gd_find.widthHint = 236;
		find.setLayoutData(gd_find);

		final Label replaceWithLabel = new Label(group, SWT.NONE);
		replaceWithLabel.setText(Messages.replaceWith);

		replace = new Text(group, SWT.BORDER);
		final GridData gd_replace = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		replace.setLayoutData(gd_replace);

		final Composite composite_2 = new Composite(composite, SWT.NONE);
		composite_2.setLayoutData(new GridData(SWT.FILL, SWT.FILL, false, true));
		final GridLayout gridLayout_2 = new GridLayout();
		gridLayout_2.numColumns = 2;
		gridLayout_2.horizontalSpacing = 0;
		gridLayout_2.marginWidth = 0;
		gridLayout_2.marginHeight = 0;
		composite_2.setLayout(gridLayout_2);

		final Group group_1 = new Group(composite_2, SWT.NONE);
		group_1.setText(Messages.option);
		group_1.setLayoutData(new GridData(SWT.LEFT, SWT.FILL, false, true));
		group_1.setLayout(new GridLayout());

		final Button matchCaseBtn = new Button(group_1, SWT.CHECK);
		matchCaseBtn.setText(Messages.matchCase);

		final Button wrapSearchBtn = new Button(group_1, SWT.CHECK);
		wrapSearchBtn.setText(Messages.wrapSearch);

		final Button matchWholeBtn = new Button(group_1, SWT.CHECK);
		matchWholeBtn.setText(Messages.matchWholeWord);

		final Group group_2 = new Group(composite_2, SWT.NONE);
		group_2.setText(Messages.direction);
		group_2.setLayout(new FillLayout());
		group_2.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, true));

		final Button upBtn = new Button(group_2, SWT.RADIO);
		upBtn.setText(Messages.up);

		final Button downBtn = new Button(group_2, SWT.RADIO);
		downBtn.setText(Messages.down);

		final Composite composite_1 = new Composite(shell, SWT.NONE);
		final GridData gd_composite_1 = new GridData(SWT.RIGHT, SWT.FILL,
				false, false);
		gd_composite_1.widthHint = 100;
		composite_1.setLayoutData(gd_composite_1);
		composite_1.setLayout(new GridLayout());

		final Button findBtn = new Button(composite_1, SWT.NONE);
		final GridData gd_findBtn = new GridData(SWT.FILL, SWT.CENTER, false,
				false);
		findBtn.setLayoutData(gd_findBtn);
		findBtn.setText(Messages.findBtn);
		findBtn.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {

				if (find.getText().length() > 0) {
					sourceViewer.setFindOption(find.getText(),
							wrapSearchBtn.getSelection(), upBtn.getSelection(),
							matchCaseBtn.getSelection(),
							matchWholeBtn.getSelection());
					if (!sourceViewer.txtFind(find.getText(), -1,
							wrapSearchBtn.getSelection(), upBtn.getSelection(),
							matchCaseBtn.getSelection(),
							matchWholeBtn.getSelection()))
						CommonTool.openInformationBox(getParent().getShell(),
								Messages.TOOLTIP_QEDIT_FIND, Messages.QEDIT_NOTFOUND);
					shell.setFocus();
				}
			}
		});

		final Button replaceBtn = new Button(composite_1, SWT.NONE);
		final GridData gd_replaceBtn = new GridData(SWT.FILL, SWT.CENTER,
				false, false);
		replaceBtn.setLayoutData(gd_replaceBtn);
		replaceBtn.setText(Messages.replaceBtn);
		replaceBtn.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				sourceViewer.setFindOption(find.getText(),
						wrapSearchBtn.getSelection(), upBtn.getSelection(),
						matchCaseBtn.getSelection(),
						matchWholeBtn.getSelection());
				if (!sourceViewer.txtReplace(find.getText(), replace.getText(),
						wrapSearchBtn.getSelection(), upBtn.getSelection(),
						matchCaseBtn.getSelection(),
						matchWholeBtn.getSelection()))
					CommonTool.openInformationBox(shell,
							Messages.TOOLTIP_QEDIT_REPLACE, Messages.QEDIT_NOTFOUND);
				shell.setFocus();
			}
		});

		final Button replaceAllBtn = new Button(composite_1, SWT.NONE);
		final GridData gd_replaceAllBtn = new GridData(SWT.FILL, SWT.CENTER,
				false, false);
		replaceAllBtn.setLayoutData(gd_replaceAllBtn);
		replaceAllBtn.setText(Messages.replaceAllBtn);
		replaceAllBtn.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				CommonTool.openInformationBox(shell, Messages.QEDIT_REPLACEALL,
						sourceViewer.txtReplaceAll(find.getText(),
								replace.getText(), matchCaseBtn.getSelection())
								+ " " + Messages.QEDIT_REPLACECOMPLETE);
				shell.setFocus();
			}
		});

		final Button closeBtn = new Button(composite_1, SWT.NONE);
		final GridData gd_closeBtn = new GridData(SWT.FILL, SWT.CENTER, false,
				false);
		closeBtn.setLayoutData(gd_closeBtn);
		closeBtn.setText(Messages.close);
		closeBtn.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				shell.dispose();
			}
		});
	}

}
