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

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;

import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.TableViewerSorter;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;

/**
 * 
 * Unload database result dialog will show result information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class UnloadDatabaseResultDialog extends
		CMTitleAreaDialog {

	private List<String> unloadResultList = null;

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public UnloadDatabaseResultDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		getHelpSystem().setHelp(parent,
				CubridManagerHelpContextIDs.databaseUnload);

		Composite parentComp = (Composite) super.createDialogArea(parent);
		Composite composite = new Composite(parentComp, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);
		GridData gridData = new GridData(GridData.FILL_BOTH);
		composite.setLayoutData(gridData);

		createDirTableComp(composite);

		setTitle(Messages.titleUnloadDbResultDialog);
		setMessage(Messages.msgUnloadDbResultDialog);
		return parentComp;
	}

	/**
	 * 
	 * Create unlaod db result table
	 * 
	 * @param parent
	 */
	private void createDirTableComp(Composite parent) {
		Composite comp = new Composite(parent, SWT.NONE);
		GridData gridData = new GridData(GridData.FILL_BOTH);
		comp.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		comp.setLayout(layout);

		Label tipLabel = new Label(comp, SWT.NONE);
		tipLabel.setText(Messages.msgUnloadDbResultDialog);
		tipLabel.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));

		final String[] columnNameArr = new String[] { Messages.tblColumnTable,
				Messages.tblColumnRowCount, Messages.tblColumnProgress };
		TableViewerSorter sorter = new TableViewerSorter();
		TableViewer tableViewer = CommonTool.createCommonTableViewer(comp,
				sorter, columnNameArr, CommonTool.createGridData(
						GridData.FILL_BOTH, 1, 4, -1, 200));
		Table dirTable = tableViewer.getTable();
		tableViewer.setInput(getTableModel());
		sorter.doSort(2);
		tableViewer.refresh();
		for (int i = 0; i < dirTable.getColumnCount(); i++) {
			dirTable.getColumn(i).pack();
		}
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleUnloadDbResultDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);
	}

	/**
	 * 
	 * Get result information tableViewer input model
	 * 
	 * @return
	 */
	private List<Map<String, Object>> getTableModel() {
		List<Map<String, Object>> resultList = new ArrayList<Map<String, Object>>();
		if (unloadResultList == null) {
			return resultList;
		}
		for (int i = 0; unloadResultList != null && i < unloadResultList.size(); i++) {
			Map<String, Object> map = new HashMap<String, Object>();
			String str = unloadResultList.get(i);
			String[] values = str.split(":");
			if (values == null || values.length != 2) {
				continue;
			}
			String key = values[0];
			String value = values[1];
			map.put("0", key);
			if (value != null) {
				value = value.trim();
				Pattern pattern = Pattern.compile("^(\\d+)\\s*\\(\\d+%/(\\d+)%\\)$");
				Matcher m = pattern.matcher(value);
				if (m.matches() && m.groupCount() == 2) {
					String rowCount = m.group(1);
					String percent = m.group(2);
					map.put("1", new Integer(rowCount));
					map.put("2", new Integer(percent));
				}
			}
			resultList.add(map);
		}
		return resultList;
	}

	/**
	 * 
	 * Set unloaded result list
	 * 
	 * @param unloadResultList
	 */
	public void setUnloadResulList(List<String> unloadResultList) {
		this.unloadResultList = unloadResultList;
	}
}
