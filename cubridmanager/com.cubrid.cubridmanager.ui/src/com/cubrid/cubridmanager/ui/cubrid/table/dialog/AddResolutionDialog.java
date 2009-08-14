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

import java.util.HashSet;
import java.util.List;
import java.util.Set;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.cubrid.table.model.DBResolution;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;

public class AddResolutionDialog extends
		CMTitleAreaDialog implements
		ModifyListener {

	private String[][] conflicts;
	private String[][] classConflicts;
	private Group group1;
	private Text aliasText;
	private Combo superCombo;
	private Combo columnCombo;
	//	private CubridDatabase database;
	private String[] supers;
	private String[] columns;
	private DBResolution resolution;
	String[][] currentConflicts = null;
	private Button instanceButton;
	private Button classButton;
	private boolean isClassResolution;
	String tableName;
	SchemaInfo schema;

	/**
	 * 
	 * @param parentShell
	 * @param newSchema
	 * @param database
	 */
	public AddResolutionDialog(Shell parentShell, String[][] conflicts,
			String[][] classConflicts, SchemaInfo schema) {
		super(parentShell);
		this.conflicts = conflicts;
		this.classConflicts = classConflicts;
		this.tableName = schema.getClassname();
		this.schema = schema;
	}

	private void init() {
		instanceButton.setSelection(true);
		fireResolutionTypeChanged();
		superCombo.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				fireSuperComboChanged();
			}
		});
		columnCombo.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				fireColumnComboChanged();
			}
		});
		superCombo.addModifyListener(this);
		columnCombo.addModifyListener(this);
		aliasText.addModifyListener(this);
	}

	private void fireSuperComboChanged() {
		String sup = superCombo.getText();
		if (sup.equals("")) { //$NON-NLS-1$
			fillColumnCombo();
		} else if (columnCombo.getText().equals("")) {
			columnCombo.removeAll();
			for (String[] str : currentConflicts) {
				if (sup.equals(str[2])) {
					columnCombo.add(str[0]);
				}
			}
		}
	}

	private void fireColumnComboChanged() {
		String column = columnCombo.getText();
		if (column.equals("")) { //$NON-NLS-1$
			//do nothing
		} else if (superCombo.getText().equals("")) { //$NON-NLS-1$
			superCombo.removeAll();
			for (String[] str : currentConflicts) {
				if (column.equals(str[0]) && !str[2].equals(tableName)) {
					superCombo.add(str[2]);
				}
			}
		}
	}

	private void fillSuperCombo() {
		superCombo.removeAll();
		superCombo.add(""); //$NON-NLS-1$
		for (String str : supers) {
			superCombo.add(str);
		}
	}

	private void fillColumnCombo() {
		columnCombo.removeAll();
		columnCombo.add(""); //$NON-NLS-1$
		for (String str : columns) {
			columnCombo.add(str);
		}
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp, CubridManagerHelpContextIDs.databaseTable);
		Composite composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();

		layout.numColumns = 1;
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);

		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.verticalSpan = 1;
		gridData6.horizontalSpan = 2;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.grabExcessHorizontalSpace = true;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 2;

		createComposite(composite);
		setTitle(Messages.msgTitleSetResolution);
		setMessage(Messages.msgSetResolution);
		getShell().setText(Messages.titleTitleSetResolution);

		init();
		return parentComp;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());

	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, Messages.btnOK, false);
		createButton(parent, IDialogConstants.CANCEL_ID, Messages.btnCancel,
				false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			setErrorMessage(null);
			String sup = superCombo.getText();
			String col = columnCombo.getText();
			String alias = aliasText.getText().trim();
			resolution = new DBResolution();
			resolution.setName(col);
			resolution.setClassName(sup);
			resolution.setAlias(alias);
			resolution.setClassResolution(isClassResolution);

			this.getShell().dispose();
			this.close();
		} else {
			resolution = null;
			super.buttonPressed(buttonId);
			this.getShell().dispose();
			this.close();
		}

	}

	private void createComposite(Composite sShell) {
		GridData gridData33 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.CENTER, false, false);
		gridData33.widthHint = 184;
		GridData gridData32 = new org.eclipse.swt.layout.GridData();
		gridData32.horizontalSpan = 3;
		GridData gridData28 = new org.eclipse.swt.layout.GridData();
		gridData28.grabExcessHorizontalSpace = true;

		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		sShell.setLayout(gridLayout);

		GridLayout gridLayout31 = new GridLayout();
		gridLayout31.numColumns = 2;
		GridData gridData1 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.FILL, true, false, 2, 1);
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayoutData(gridData1);
		group1.setLayout(gridLayout31);

		final Label resolutionTypeLabel = new Label(group1, SWT.NONE);
		resolutionTypeLabel.setText(Messages.lblResolutionType);

		final Composite composite_1 = new Composite(group1, SWT.NONE);
		composite_1.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		final GridLayout gridLayout_1 = new GridLayout();
		gridLayout_1.makeColumnsEqualWidth = true;
		gridLayout_1.numColumns = 2;
		composite_1.setLayout(gridLayout_1);

		classButton = new Button(composite_1, SWT.RADIO);
		classButton.setText(Messages.typeClass);
		classButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				fireResolutionTypeChanged();
			}
		});

		instanceButton = new Button(composite_1, SWT.RADIO);
		instanceButton.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		instanceButton.setText(Messages.typeInstance);
		instanceButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				fireResolutionTypeChanged();
			}
		});

		Label label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.lblSuperClass);

		GridData gridData2 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.CENTER, true, false);
		gridData2.widthHint = 165;
		superCombo = new Combo(group1, SWT.DROP_DOWN | SWT.READ_ONLY);
		superCombo.setLayoutData(gridData2);

		Label label6 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.lblTipSuperClass);
		label6.setLayoutData(gridData32);
		Label label4 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.lblColumnName);

		GridData gridData3 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.CENTER, false, false);
		gridData3.widthHint = 165;
		columnCombo = new Combo(group1, SWT.DROP_DOWN | SWT.READ_ONLY);
		columnCombo.setLayoutData(gridData3);

		Label label5 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.lblAlias);
		aliasText = new Text(group1, SWT.BORDER);
		aliasText.setLayoutData(gridData33);

		GridData gridData29 = new org.eclipse.swt.layout.GridData(
				GridData.GRAB_HORIZONTAL);
		gridData29.widthHint = 75;
		gridData29.horizontalAlignment = org.eclipse.swt.layout.GridData.END;

	}

	protected void fireResolutionTypeChanged() {
		if (instanceButton.getSelection()) {
			currentConflicts = conflicts;
			isClassResolution = false;
		} else {
			currentConflicts = classConflicts;
			isClassResolution = true;
		}
		Set<String> set = new HashSet<String>();
		for (String[] str : currentConflicts) {
			if (!str[2].equals(tableName)) {
				set.add(str[2]);
			}
		}
		supers = set.toArray(new String[set.size()]);
		set.clear();
		for (String[] str : currentConflicts) {
			set.add(str[0]);
		}
		columns = set.toArray(new String[set.size()]);
		fillSuperCombo();
		fillColumnCombo();
		aliasText.setText(""); //$NON-NLS-1$
	}

	public void modifyText(ModifyEvent e) {
		setErrorMessage(null);
		String sup = superCombo.getText();
		String col = columnCombo.getText();
		String alias = aliasText.getText();
		if (sup.equals("")) { //$NON-NLS-1$
			setErrorMessage(Messages.errNoSelectedSuperClass);
			return;
		}
		if (col.equals("")) { //$NON-NLS-1$
			setErrorMessage(Messages.errNoSelectedColumn);
			return;
		}
		if (alias.equals("")) {
			for (String[] str : currentConflicts) {
				if (str[0].equals(col) && str[2].equals(tableName)) {
					String msg = Messages.bind(Messages.errExistLocColumn, col);
					setErrorMessage(msg);
					aliasText.setFocus();
					return;
				}
			}
		}
		List<DBResolution> resolutions = null;
		if (isClassResolution) {
			resolutions = schema.getClassResolutions();
		} else {
			resolutions = schema.getResolutions();
		}
		for (DBResolution r : resolutions) {
			if (r.getName().equals(col) && r.getClassName().equals(sup)
					&& r.getAlias().equals(alias)) {
				setErrorMessage(Messages.errExistResolution);
				return;
			}
		}

		getButton(IDialogConstants.OK_ID).setEnabled(true);

	}

	public DBResolution getResolution() {
		return resolution;
	}

	public void setResolution(DBResolution resolution) {
		this.resolution = resolution;
	}

	public boolean isClassResolution() {
		return isClassResolution;
	}

}
