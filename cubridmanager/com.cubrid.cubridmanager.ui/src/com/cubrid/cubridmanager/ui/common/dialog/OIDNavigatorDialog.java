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
package com.cubrid.cubridmanager.ui.common.dialog;

import java.sql.Connection;
import java.sql.ResultSetMetaData;
import java.sql.SQLException;
import java.sql.Statement;

import org.apache.log4j.Logger;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.TreeEvent;
import org.eclipse.swt.events.TreeListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Tree;
import org.eclipse.swt.widgets.TreeItem;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDResultSet;
import cubrid.sql.CUBRIDOID;

/**
 * 
 * OID navigator will use this dialog to navigator data
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class OIDNavigatorDialog extends
		CMTitleAreaDialog implements
		ModifyListener {
	private static final Logger logger = LogUtil.getLogger(OIDNavigatorDialog.class);

	private Text oidValueText = null;
	private Composite findResultComp;
	private Button findButton;
	private Tree resultTree;
	private Connection conn = null;
	private String oidStr = null;
	private static final String dumyItemFlag = "dumyItemFlag";
	private static final String oidItemFlag = "oidItemFlag";
	private static final String expandedItemFlag = "expandedItemFlag";

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public OIDNavigatorDialog(Shell parentShell, Connection conn, String oidStr) {
		super(parentShell);
		this.conn = conn;
		this.oidStr = oidStr;
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		getHelpSystem().setHelp(parent, CubridManagerHelpContextIDs.databaseOid);

		Composite parentComp = (Composite) super.createDialogArea(parent);
		Composite composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.numColumns = 5;
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);

		Label oidValueLabel = new Label(composite, SWT.LEFT);
		oidValueLabel.setText(Messages.lblOIDValue);
		oidValueLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		oidValueText = new Text(composite, SWT.LEFT | SWT.BORDER);
		oidValueText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 3, 1, -1, -1));
		if (oidStr != null) {
			oidValueText.setText(oidStr);
		}
		oidValueText.addModifyListener(this);

		findButton = new Button(composite, SWT.CENTER);
		findButton.setText(Messages.btnFind);
		findButton.setLayoutData(CommonTool.createGridData(1, 1, 60, -1));
		if (oidStr == null || oidStr.trim().length() <= 0)
			findButton.setEnabled(false);
		findButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				find();
			}
		});

		findResultComp = new Composite(composite, SWT.BORDER);
		findResultComp.setLayoutData(CommonTool.createGridData(
				GridData.FILL_BOTH, 5, 1, -1, 200));
		findResultComp.setLayout(new GridLayout());
		resultTree = new Tree(findResultComp, SWT.NONE | SWT.VIRTUAL);
		GridData data = new GridData(GridData.FILL_BOTH);
		resultTree.setLayoutData(data);
		resultTree.addTreeListener(new TreeListener() {
			public void treeCollapsed(TreeEvent e) {
			}

			public void treeExpanded(TreeEvent e) {
				TreeItem item = (TreeItem) e.item;
				if (item.getData(oidItemFlag) == null
						|| (item.getData(expandedItemFlag) != null)) {
					return;
				}
				try {
					if (item.getItemCount() > 0) {
						Object obj = item.getItem(0).getData(dumyItemFlag);
						if (obj != null && obj instanceof String) {
							String dumyItemFlag = (String) obj;
							if (dumyItemFlag.equals(dumyItemFlag)) {
								item.removeAll();
							}
						}
					}
					searchOID((String) (item.getData(oidItemFlag)), item);
				} catch (SQLException e1) {
					logger.error(e1);
				} finally {
					item.setData(expandedItemFlag, "true");
				}
			}
		});
		setTitle(Messages.titleOIDNavigatorDialog);
		setMessage(Messages.msgOIDNavigatorDialog);
		return parentComp;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.titleOIDNavigatorDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, Messages.btnOK, true);
	}

	/**
	 * When modify the page content and check the validation
	 */
	public void modifyText(ModifyEvent e) {
		String oidValue = oidValueText.getText();
		if (oidValue.length() > 0) {
			findButton.setEnabled(true);
			setErrorMessage(null);
		} else {
			findButton.setEnabled(false);
			setErrorMessage(Messages.errOIDValue1);
		}
	}

	/**
	 * 
	 * Execute to find result
	 * 
	 * @return
	 */
	public boolean find() {
		boolean isOk = true;
		resultTree.removeAll();
		TreeItem root = new TreeItem(resultTree, SWT.NONE);
		try {
			isOk = searchOID(oidValueText.getText(), root);
		} catch (SQLException e) {
			CommonTool.openErrorBox(getShell(), e.getMessage());
			logger.error(e);
		}
		if (!root.isDisposed())
			root.setExpanded(true);
		if (!resultTree.isDisposed())
			resultTree.layout(true);
		return isOk;
	}

	/**
	 * 
	 * Get tree constructor by the result of execute oid
	 * 
	 * @param strOid
	 * @param parent
	 * @return
	 * @throws SQLException
	 */
	private boolean searchOID(String strOid, TreeItem parent) throws SQLException {
		CUBRIDOID oid = null;
		String tblName = null;
		String[] columnName;
		String[] typeName;
		String[] value;
		String[] oidSet = null;
		boolean[] isOid = null;
		int cntColumn = 0;

		try {
			oid = CUBRIDOID.getNewInstance((CUBRIDConnection) conn, strOid);
			if (oid != null)
				tblName = oid.getTableName();
			else
				return false;
		} catch (Exception e) {
			CommonTool.openErrorBox(getShell(), Messages.errOIDValue2);
			return false;
		}

		if (tblName == null) {
			CommonTool.openErrorBox(getShell(), Messages.errOIDValue2);
			return false;
		}

		parent.setText(strOid);
		TreeItem item = new TreeItem(parent, SWT.NONE);
		item.setText("table name: " + tblName);

		String sql = "select * from " + tblName + " where rownum = 1";

		Statement stmt = conn.createStatement();
		CUBRIDResultSet rs = (CUBRIDResultSet) stmt.executeQuery(sql);
		ResultSetMetaData rsmt = rs.getMetaData();

		cntColumn = rsmt.getColumnCount();
		columnName = new String[cntColumn];
		typeName = new String[cntColumn];
		value = new String[cntColumn];

		for (int i = 0; i < cntColumn; i++) {
			columnName[i] = rsmt.getColumnName(i + 1);
			typeName[i] = rsmt.getColumnTypeName(i + 1);
		}
		rs.close();

		rs = (CUBRIDResultSet) oid.getValues(columnName);
		while (rs.next()) {
			for (int i = 0; i < columnName.length; i++) {
				if (rs.getObject(columnName[i]) != null) {
					if (typeName[i].equals("SET")
							|| typeName[i].equals("MULTISET")
							|| typeName[i].equals("SEQUENCE")) {
						Object[] set = (Object[]) rs.getCollection(columnName[i]);
						oidSet = new String[set.length];
						isOid = new boolean[set.length];
						value[i] = "{";
						if (set.length > 0) {
							for (int j = 0; j < set.length; j++) {
								if (set[j] instanceof CUBRIDOID) {
									value[i] += ((CUBRIDOID) set[j]).getOidString();
									oidSet[j] = ((CUBRIDOID) set[j]).getOidString();
									isOid[j] = true;
								} else {
									value[i] += set[j];
									oidSet[j] = null;
									isOid[j] = false;
								}

								if (i < set.length - 1)
									value[i] += ", ";
							}
						}
						value[i] += "}";
					} else
						value[i] = rs.getString(columnName[i]);
				} else
					value[i] = "NULL";
			}
		}
		rs.close();
		stmt.close();
		for (int i = 0; i < value.length; i++) {
			if (typeName[i].equals("CLASS") && !value[i].equals("NULL")) {
				item = new TreeItem(parent, SWT.NONE);
				item.setText(columnName[i] + ": " + value[i]);
				TreeItem treeItem = new TreeItem(item, SWT.NONE);
				treeItem.setText(value[i]);
				treeItem.setData(oidItemFlag, value[i]);
				TreeItem dumyItem = new TreeItem(treeItem, SWT.NONE);
				dumyItem.setData(dumyItemFlag, dumyItemFlag);
			} else if (typeName[i].equals("SET")
					|| typeName[i].equals("MULTISET")
					|| typeName[i].equals("SEQUENCE")) {
				item = new TreeItem(parent, SWT.NONE);
				item.setText(columnName[i] + ": " + value[i]);
				if (isOid != null) {
					for (int j = 0; j < oidSet.length; j++) {
						if (isOid[j]) {
							TreeItem treeItem = new TreeItem(item, SWT.NONE);
							treeItem.setData(oidItemFlag, oidSet[j]);
							treeItem.setText(oidSet[j]);
							TreeItem dumyItem = new TreeItem(treeItem, SWT.NONE);
							dumyItem.setData(dumyItemFlag, dumyItemFlag);
						}
					}
				}
			} else
				(new TreeItem(parent, SWT.NONE)).setText(columnName[i] + ": "
						+ value[i]);
		}
		return true;
	}
}
