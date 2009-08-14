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

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.IOException;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Types;
import java.text.ParseException;
import java.util.ArrayList;

import jxl.Cell;
import jxl.Sheet;
import jxl.Workbook;
import jxl.biff.EmptyCell;
import jxl.read.biff.BiffException;

import org.apache.log4j.Logger;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.SashForm;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.List;
import org.eclipse.swt.widgets.MessageBox;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Spinner;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.ToolBar;
import org.eclipse.swt.widgets.ToolItem;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.CSVReader;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.model.DataType;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

import cubrid.jdbc.driver.CUBRIDConnectionKey;
import cubrid.jdbc.driver.CUBRIDDatabaseMetaData;
import cubrid.jdbc.driver.CUBRIDPreparedStatement;

public class ImportDataDialog extends
		CMTitleAreaDialog {
	private static final int AUTOCOMMIT_COUNT = 5000;
	private static final String DATETIME_FORMAT = "yyyy-MM-dd HH:mm:ss.SSS";
	private static final Logger logger = LogUtil.getLogger(ImportDataDialog.class);
	private static final String NEW_LINE = System.getProperty("line.separator"); //$NON-NLS-1$
	private static final String sessionImportKey = "Import-ImportFilePath"; //$NON-NLS-1$
	private Label lblTargetTable = null;
	private Label lblImportFile = null;
	private Label lblCommitLine = null;
	private Group grpTop = null;
	private Group grpMiddle = null;
	private Label lblMappingMessage = null;
	private Button checkBox = null;
	private Spinner spnCommitLine = null;
	private Text txtImportFile = null;
	private Combo cmbTargetTable = null;
	private Button btnOpen = null;
	private Connection conn = null;
	private SashForm sashColumns = null;
	private Composite fromColumn = null;
	private Composite toColumn = null;
	private Label lblFrom = null;
	private Label lblTo = null;
	private List listFrom = null;
	private List listTo = null;
	private ToolBar tbFrom = null;
	private ToolBar tbTo = null;
	private File file = null;
	private Workbook workbook = null;
	private String fileName = null;
	private ArrayList<String> colName;
	private ArrayList<String> colType;
	private int cntColumn = 0;
	private boolean prevAutocommit = false;
	public CUBRIDConnectionKey conKey = null;
	/* UI(WaitingMsgBox) related values makes global variable for using by thread. */
	private int listFromGetItemCount = 0;
	private String listToGetItem[];
	private int spnCommitLineValue = 0;
	private boolean firstRowAsHeader = false;
	private String sql = ""; //$NON-NLS-1$
	private boolean hasError = false;
	private String message = ""; //$NON-NLS-1$
	private int currentRow = 0;
	private int startRow = 0;
	private int style = SWT.NONE;
	private String columns[];
	private Composite composite;
	private String tableName;
	private DatabaseInfo database;
	private int commitedCount;
	private CUBRIDPreparedStatement stmt;

	public ImportDataDialog(Shell parentShell, CubridDatabase database,
			String tableName, Connection _conn) {
		super(parentShell);
		try {
			this.tableName = tableName;
			//load jdbc driver
			this.conn = _conn;
			this.database = database.getDatabaseInfo();

			//set configration of connection
			prevAutocommit = conn.getAutoCommit();
			conn.setAutoCommit(false);
		} catch (SQLException e) {
			CommonTool.openErrorBox(parentShell, e.getErrorCode() + NEW_LINE
					+ Messages.importErrorHead + e.getMessage());
			logger.error(e);
			parentShell.setFocus();
		}
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.importShellTitle);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, Messages.importButtonName,
				false);
		createButton(parent, IDialogConstants.CANCEL_ID,
				Messages.cancleImportButtonName, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			setErrorMessage(null);

			if (cmbTargetTable.getText().length() < 1) { //no target table
				setErrorMessage(Messages.importSelectTargetTableERRORMSG);
				cmbTargetTable.setFocus();
				return;
			}
			if (txtImportFile.getText().length() < 1) { //no import file
				setErrorMessage(Messages.importSelectFileERRORMSG);
				txtImportFile.setFocus();
				return;
			}

			if (listFrom.getItemCount() != listTo.getItemCount()) { //columns not matched
				setErrorMessage(Messages.importColumnCountMatchERRORMSG);
				listFrom.setFocus();
				return;
			}
			if (listFrom.getItemCount() < 1) { //no from column
				setErrorMessage(Messages.importNoExcelColumnERRORMSG);
				listFrom.setFocus();
				return;
			}
			if (listTo.getItemCount() < 1) { //no to column
				setErrorMessage(Messages.importNoTableColumnERRORMSG);
				listTo.setFocus();
				return;
			}
			getButton(IDialogConstants.OK_ID).setEnabled(false);
			importNow();
			if (hasError) {
				getButton(IDialogConstants.OK_ID).setEnabled(true);
			} else {
				try {
					if (stmt != null)
						stmt.close();
				} catch (SQLException e) {
					logger.error(e.getMessage());
				}
				this.getShell().dispose();
				this.close();
			}
			return;
		} else if (buttonId == IDialogConstants.CANCEL_ID) {
			super.buttonPressed(buttonId);
			this.getShell().dispose();
			this.close();
		}
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp,
				CubridManagerHelpContextIDs.databaseTable);
		composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));

		GridLayout layout = new GridLayout();
		layout.numColumns = 1;
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);

		createGroupTop(composite);
		createGroupMiddle(composite);

		try {
			addComboItem();
			if (tableName != null && tableName.length() > 0) {
				cmbTargetTable.setText(tableName);
				setToColumn(tableName);
			}
		} catch (SQLException e) {
			CommonTool.openErrorBox(parent.getShell(), e.getErrorCode()
					+ NEW_LINE + Messages.importErrorHead + e.getMessage());
			logger.error(e);
		}
		setTitle(Messages.importDataMsgTitle);
		setMessage(Messages.importDataMsg);
		return parent;
	}

	/**
	 * This method initializes grpTop
	 * 
	 */
	private void createGroupTop(Composite composite) {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		gridLayout.horizontalSpacing = 5;

		GridData gridData = new GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.verticalAlignment = GridData.FILL;
		gridData.horizontalAlignment = GridData.FILL;

		grpTop = new Group(composite, SWT.NONE);
		grpTop.setLayoutData(gridData);
		grpTop.setLayout(gridLayout);

		gridData = new GridData();
		gridData.widthHint = 90;
		gridData.horizontalAlignment = GridData.FILL;
		lblTargetTable = new Label(grpTop, SWT.NONE);
		lblTargetTable.setText(Messages.importTargetTable);
		lblTargetTable.setLayoutData(gridData);

		createCmbTargetTable();

		gridData = new GridData();
		gridData.widthHint = 66;
		gridData.verticalSpan = 3;
		btnOpen = new Button(grpTop, SWT.NONE);
		// TODO: image
		btnOpen.setText(Messages.importOpenFileBTN);
		btnOpen.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				try {
					openFileDialog();
					openFile();
					setFromColumn();
				} catch (IOException e1) {
					CommonTool.openErrorBox(e1.getMessage());
					logger.error(e1);
					txtImportFile.setText(""); //$NON-NLS-1$
					fileName = ""; //$NON-NLS-1$
				}
			}
		});
		btnOpen.setLayoutData(gridData);

		gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		lblImportFile = new Label(grpTop, SWT.NONE);
		lblImportFile.setText(Messages.importFileNameLBL);
		lblImportFile.setLayoutData(gridData);

		gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		gridData.widthHint = 400;
		txtImportFile = new Text(grpTop, SWT.BORDER);
		txtImportFile.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
			public void focusLost(org.eclipse.swt.events.FocusEvent e) {
				fileName = txtImportFile.getText();
				try {
					openFile();
					setFromColumn();
				} catch (IOException e1) {
					CommonTool.openErrorBox(getShell(), e1.getMessage());
					logger.error(e1);
					txtImportFile.setText(""); //$NON-NLS-1$
					fileName = ""; //$NON-NLS-1$
				}
			}
		});
		txtImportFile.setLayoutData(gridData);

		gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		lblCommitLine = new Label(grpTop, SWT.NONE);
		lblCommitLine.setText(Messages.importCommitLinesLBL);
		lblCommitLine.setLayoutData(gridData);

		gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		spnCommitLine = new Spinner(grpTop, SWT.BORDER);
		spnCommitLine.setMaximum(Integer.MAX_VALUE);
		spnCommitLine.setMinimum(0);
		spnCommitLine.setSelection(AUTOCOMMIT_COUNT);
		spnCommitLine.setLayoutData(gridData);
	}

	/**
	 * This method initializes grpBottom
	 * 
	 */
	private void createGroupMiddle(Composite composite) {
		GridData gridData = new GridData();
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = GridData.FILL;
		gridData.verticalAlignment = GridData.FILL;
		grpMiddle = new Group(composite, SWT.NONE);
		grpMiddle.setLayout(new GridLayout());

		grpMiddle.setLayoutData(gridData);

		gridData = new GridData();
		gridData.grabExcessHorizontalSpace = true;
		lblMappingMessage = new Label(grpMiddle, SWT.NONE);
		lblMappingMessage.setText(Messages.importMappingExcel);
		lblMappingMessage.setLayoutData(gridData);

		gridData = new GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalIndent = 10;
		checkBox = new Button(grpMiddle, SWT.CHECK);
		// TODO: image
		checkBox.setText(Messages.importFirstLineFLAG);
		checkBox.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				try {
					setFromColumn();
				} catch (IOException e1) {
					CommonTool.openErrorBox(getShell(), e1.getMessage());
					logger.error(e1);
				}
			}
		});
		checkBox.setLayoutData(gridData);

		createSashColumns();
	}

	/**
	 * This method initializes cmbTargetTable
	 * 
	 */
	private void createCmbTargetTable() {
		GridData gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		cmbTargetTable = new Combo(grpTop, SWT.SIMPLE | SWT.DROP_DOWN
				| SWT.READ_ONLY);
		cmbTargetTable.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				try {
					setToColumn(cmbTargetTable.getText());
				} catch (SQLException e1) {
					CommonTool.openErrorBox(getShell(), e1.getErrorCode()
							+ NEW_LINE + e1.getMessage());
					logger.error(e1);
				}
			}
		});
		cmbTargetTable.setLayoutData(gridData);
	}

	/**
	 * This method initializes sashColumns
	 * 
	 */
	private void createSashColumns() {
		GridData gridData = new GridData();
		gridData.grabExcessVerticalSpace = true;
		gridData.verticalAlignment = GridData.FILL;
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		sashColumns = new SashForm(grpMiddle, SWT.NONE);
		createFromColumn();
		createToColumn();
		sashColumns.setLayoutData(gridData);
	}

	/**
	 * This method initializes fromColumn
	 * 
	 */
	private void createFromColumn() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.horizontalSpacing = 0;
		gridLayout.marginWidth = 0;
		gridLayout.verticalSpacing = 5;
		gridLayout.marginHeight = 0;

		fromColumn = new Composite(sashColumns, SWT.NONE);

		lblFrom = new Label(fromColumn, SWT.NONE);
		lblFrom.setText(Messages.importExcelcolumns);

		GridData gridData = new GridData();
		gridData.verticalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = GridData.FILL;
		gridData.heightHint = 200;
		listFrom = new List(fromColumn, SWT.BORDER | SWT.V_SCROLL);
		listFrom.setLayoutData(gridData);

		createTbFrom();
		fromColumn.setLayout(gridLayout);
	}

	/**
	 * This method initializes toColumn
	 * 
	 */
	private void createToColumn() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.horizontalSpacing = 0;
		gridLayout.marginWidth = 0;
		gridLayout.marginHeight = 0;

		toColumn = new Composite(sashColumns, SWT.NONE);

		lblTo = new Label(toColumn, SWT.NONE);
		lblTo.setText(Messages.importTableColumns);

		GridData gridData = new GridData();
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = GridData.FILL;
		gridData.verticalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		listTo = new List(toColumn, SWT.BORDER | SWT.V_SCROLL);
		listTo.setLayoutData(gridData);

		createTbTo();

		toColumn.setLayout(gridLayout);
	}

	/**
	 * This method initializes tbFrom
	 * 
	 */
	private void createTbFrom() {
		GridData gridData = new GridData();
		gridData.horizontalAlignment = GridData.END;
		tbFrom = new ToolBar(fromColumn, SWT.FLAT);
		tbFrom.setLayoutData(gridData);

		ToolItem itemDel = new ToolItem(tbFrom, SWT.PUSH);
		// TODO: image
		itemDel.setText(Messages.importDeleteExcelColumnBTN);
		itemDel.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (listFrom.getItemCount() > 0
						&& listFrom.getSelectionCount() > 0) {
					int index = listFrom.getSelectionIndex();

					if (index == listFrom.getItemCount() - 1) {
						listFrom.setSelection(index - 1);
						listFrom.remove(index);
					} else {
						listFrom.remove(index);
						listFrom.setSelection(index);
					}
				}
			}
		});
	}

	/**
	 * This method initializes tbTo
	 * 
	 */
	private void createTbTo() {
		GridData gridData = new GridData();
		gridData.horizontalAlignment = GridData.END;
		tbTo = new ToolBar(toColumn, SWT.FLAT);
		tbTo.setLayoutData(gridData);

		ToolItem itemUp = new ToolItem(tbTo, SWT.PUSH);
		// TODO: image
		itemUp.setText(Messages.importUpTableColumnBTN);
		itemUp.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (listTo.getItemCount() > 0 && listTo.getSelectionCount() > 0) {
					int index = listTo.getSelectionIndex();
					String item = listTo.getItem(index);
					if (index == 0)
						return;

					listTo.remove(index);
					listTo.add(item, index - 1);
					listTo.setSelection(index - 1);
				}
			}
		});

		ToolItem itemDown = new ToolItem(tbTo, SWT.PUSH);
		// TODO: image
		itemDown.setText(Messages.importDownTableColumnBTN);
		itemDown.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (listTo.getItemCount() > 0 && listTo.getSelectionCount() > 0) {
					int index = listTo.getSelectionIndex();
					String item = listTo.getItem(index);
					if (index == listTo.getItemCount() - 1)
						return;

					listTo.remove(index);
					listTo.add(item, index + 1);
					listTo.setSelection(index + 1);
				}
			}
		});

		ToolItem itemDel = new ToolItem(tbTo, SWT.PUSH | SWT.BORDER);
		// TODO: image
		itemDel.setText(Messages.importDeleteTableColumnBTN);
		itemDel.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (listTo.getItemCount() > 0 && listTo.getSelectionCount() > 0) {
					int index = listTo.getSelectionIndex();

					if (index == listTo.getItemCount() - 1) {
						listTo.setSelection(index - 1);
						listTo.remove(index);
					} else {
						listTo.remove(index);
						listTo.setSelection(index);
					}
				}
			}
		});
	}

	private void addComboItem() throws SQLException {
		CUBRIDDatabaseMetaData dbmd = (CUBRIDDatabaseMetaData) conn.getMetaData();
		ResultSet rs = dbmd.getTables(null, null, null,
				new String[] { "table" }); //$NON-NLS-1$

		while (rs.next()) {
			cmbTargetTable.add(rs.getString("table_name")); //$NON-NLS-1$
		}
		rs.close();
	}

	private void openFileDialog() {
		FileDialog dialog = new FileDialog(getShell(), SWT.OPEN
				| SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(new String[] { "*.xls", "*.csv" }); //$NON-NLS-1$ //$NON-NLS-2$
		dialog.setFilterNames(new String[] { "Excel(xls)", //$NON-NLS-1$
				"Comma separated value(csv)" }); //$NON-NLS-1$

		String filepath = CubridManagerUIPlugin.getPreference(sessionImportKey);
		if (null != filepath) {
			dialog.setFilterPath(filepath);
		}

		fileName = dialog.open();
		txtImportFile.setText(fileName);
	}

	private void openFile() throws IOException {
		if (fileName == null)
			return;
		if (fileName.trim().length() < 1)
			return;

		file = new File(fileName);

		if (fileName.toLowerCase().endsWith(".xls")) { //$NON-NLS-1$
			try {
				workbook = Workbook.getWorkbook(file);
				cntColumn = workbook.getSheet(0).getColumns();
			} catch (BiffException e) {
				CommonTool.openErrorBox(getShell(), Messages.errInvalidFile);
				logger.error(e);
				txtImportFile.setText(""); //$NON-NLS-1$
				fileName = ""; //$NON-NLS-1$
				getShell().setFocus();
			}
		} else if (fileName.toLowerCase().endsWith(".csv")) { //$NON-NLS-1$
			FileReader fileReader = new FileReader(file);
			CSVReader csvReader = new CSVReader(fileReader);
			cntColumn = csvReader.readNext().length;
			csvReader.close();
		}
		CubridManagerUIPlugin.setPreference(sessionImportKey, file.getParent());
		getShell().setActive();
	}

	private void setFromColumn() throws IOException {
		if (fileName == null)
			return;
		if (fileName.trim().length() < 1)
			return;
		if (file == null)
			return;

		listFrom.removeAll();

		if (checkBox.getSelection()) {
			if (fileName.toLowerCase().endsWith(".xls")) { //$NON-NLS-1$
				for (int j = 0; j < cntColumn; j++) {
					Cell cell = workbook.getSheet(0).getCell(j, 0);
					listFrom.add(cell == null ? "" : cell.getContents()); //$NON-NLS-1$
				}
			} else if (fileName.toLowerCase().endsWith(".csv")) { //$NON-NLS-1$
				CSVReader csvReader = new CSVReader(new FileReader(file));
				String[] csvColumn = csvReader.readNext();
				for (int j = 0; j < cntColumn; j++) {
					listFrom.add(csvColumn[j] == null ? "" : csvColumn[j]); //$NON-NLS-1$
				}
				csvReader.close();
			}
		} else {
			for (int i = 0; i < cntColumn; i++) {
				listFrom.add("Column " + i); //$NON-NLS-1$
			}
		}
	}

	private void setToColumn(String table) throws SQLException {
		SchemaInfo schema = database.getSchemaInfo(table);
		colName = new ArrayList<String>();
		colType = new ArrayList<String>();

		listTo.removeAll();
		java.util.List<DBAttribute> attributes = schema.getAttributes();
		for (DBAttribute attr : attributes) {
			String column = attr.getName();
			String dataType = attr.getType();
			listTo.add(column);
			colName.add(column);
			colType.add(dataType);
		}
	}

	private void resetStatement() throws SQLException {
		try {
			stmt.close();
		} catch (SQLException e) {
			logger.error(e.getMessage());
		}
		stmt = (CUBRIDPreparedStatement) conn.prepareStatement(sql);
	}

	private void importNow() {

		sql = getInsertDML();

		listFromGetItemCount = listFrom.getItemCount();
		listToGetItem = new String[listFromGetItemCount];
		columns = new String[listFromGetItemCount];
		spnCommitLineValue = spnCommitLine.getSelection();
		firstRowAsHeader = checkBox.getSelection();

		for (int i = 0; i < listFromGetItemCount; i++)
			listToGetItem[i] = listTo.getItem(i);

		message = ""; //$NON-NLS-1$
		startRow = 0;
		if (firstRowAsHeader)
			startRow = 1;

		currentRow = startRow;
		columns = listFrom.getItems();

		final ITask execThread = new ITask() {
			public void execute() {
				try {
					stmt = (CUBRIDPreparedStatement) conn.prepareStatement(sql);
					if (fileName.toLowerCase().endsWith(".xls")) { //$NON-NLS-1$
						importFromExcel();
					} else {
						importFromCVS();
					}
					if (spnCommitLineValue == 0) {
						int[] counts = stmt.executeBatch();
						int insertCount = 0;
						for (int i = 0; i < counts.length; i++) {
							if (counts[i] > 0) {
								insertCount++;
							}
						}
					} else if ((currentRow - startRow) % spnCommitLineValue == 0) {
						//do not executeBatch
					} else {
						int[] counts = stmt.executeBatch();
						int insertCount = 0;
						for (int i = 0; i < counts.length; i++) {
							if (counts[i] > 0) {
								insertCount++;
							}
						}
					}
				} catch (SQLException e) {
					hasError = true;
					message = getMessage(e);
				} catch (NumberFormatException e) {
					hasError = true;
					message = getMessage(e);
				} catch (ParseException e) {
					hasError = true;
					message = getMessage(e);
				} catch (Exception e1) {
					hasError = true;
					style |= SWT.OK | SWT.ICON_ERROR;
					message = e1.getMessage();
					logger.error(e1);
				} catch (Error e1) {
					hasError = true;
					style |= SWT.OK | SWT.ICON_ERROR;
					message = e1.getMessage();
					logger.error(e1);
				}
				if (!hasError) {
					style |= SWT.OK | SWT.ICON_INFORMATION;
					int insertCount = currentRow - startRow;
					if (insertCount <= 1) {
						message += Messages.bind(
								Messages.importInsertCountMSG1, insertCount);
					} else {
						message += Messages.bind(
								Messages.importInsertCountMSG2, insertCount);
					}
				}
				Display.getDefault().syncExec(new Runnable() {
					public void run() {
						MessageBox mb = new MessageBox(getShell(), style);
						mb.setText(Messages.importShellTitle);
						mb.setMessage(message);
						int yn = mb.open();
						try {
							if (hasError) {
								if (spnCommitLineValue == 0) { //commit once
									if (yn == SWT.YES) {
										conn.commit();
									} else {
										conn.rollback();
									}
								} else { //commit multiple
									conn.rollback();
								}
							} else { //no error
								conn.commit();
							}
						} catch (SQLException e) {
							logger.error(e);
						} finally {
							try {
								conn.setAutoCommit(prevAutocommit);
							} catch (SQLException e) {
								logger.error(e);
							}
						}
					}
				});

			}

			public void cancel() {

			}

			public void finish() {

			}

			public String getErrorMsg() {
				return null;
			}

			public String getTaskname() {
				return null;
			}

			public String getWarningMsg() {
				return null;
			}

			public boolean isCancel() {
				return false;
			}

			public boolean isSuccess() {
				return true;
			}

			public void setErrorMsg(String errorMsg) {

			}

			public void setTaskname(String taskName) {

			}

			public void setWarningMsg(String waringMsg) {

			}
		};
		TaskExecutor taskExecutor = new CommonTaskExec();
		taskExecutor.addTask(execThread);
		new ExecTaskWithProgress(taskExecutor).exec();
	}

	/**
	 * return insert sql for prepared statement
	 * 
	 */
	private String getInsertDML() {
		String insert = "insert into \"" + cmbTargetTable.getText() + "\" ("; //$NON-NLS-1$ //$NON-NLS-2$
		String values = "values ("; //$NON-NLS-1$
		StringBuffer bfSQL = new StringBuffer();
		StringBuffer bfValue = new StringBuffer();
		bfSQL.append(insert);
		bfValue.append(values);
		for (int i = 0; i < listTo.getItemCount(); i++) {
			if (i > 0) {
				bfSQL.append(", "); //$NON-NLS-1$
				bfValue.append(", "); //$NON-NLS-1$
			}

			bfSQL.append("\"").append(listTo.getItem(i)).append("\""); //$NON-NLS-1$ //$NON-NLS-2$

			String type = colType.get(colName.indexOf(listTo.getItem(i))).toString();
			if (type.startsWith("BIT")) { //$NON-NLS-1$
				bfValue.append("cast(? as bit varying)"); //$NON-NLS-1$
			} else
				bfValue.append("?"); //$NON-NLS-1$
		}
		bfSQL.append(") "); //$NON-NLS-1$
		bfValue.append(")"); //$NON-NLS-1$
		bfSQL.append(bfValue.toString());
		return bfSQL.toString();
	}

	/**
	 * add a tuple to batch
	 * 
	 * @param colValues
	 * @throws SQLException
	 * @throws ParseException
	 * @throws NumberFormatException
	 */
	private void addTuple(java.util.List<String> colValues) throws SQLException,
			NumberFormatException,
			ParseException {
		for (int i = 0; i < listFromGetItemCount; i++) {
			String value = (String) colValues.get(i);
			if (value == null) {
				stmt.setNull(i + 1, Types.NULL);
			} else {

				String columnType = colType.get(
						colName.indexOf(listToGetItem[i])).toString();
				if (value.equals("")
						&& !(columnType.equals("VARCHAR") || columnType.equals("CHAR"))) {
					stmt.setNull(i + 1, Types.NULL);
					continue;
				}
				if (value.indexOf("GLO") > -1 //$NON-NLS-1$
						|| columnType.startsWith("national character")) {//$NON-NLS-1$
					stmt.setNull(i + 1, Types.NULL);
				} else if (columnType.startsWith("set_of")
						|| columnType.startsWith("multiset_of")
						|| columnType.startsWith("sequence_of")) {//$NON-NLS-1$
					Object[] values = DataType.getCollectionValues(columnType,
							value);
					stmt.setCollection(i + 1, values);
				} else if (columnType.startsWith("datetime")) {//$NON-NLS-1$
					String datetime = formatDatetime(value.trim());
					long time = com.cubrid.cubridmanager.core.CommonTool.getDatetime(datetime);
					java.sql.Timestamp timestamp = new java.sql.Timestamp(time);
					stmt.setTimestamp(i + 1, timestamp);
				} else {
					stmt.setString(i + 1, value);
				}
			}
		}
		stmt.addBatch();
		int rows = currentRow - startRow;
		if (spnCommitLineValue != 0 && rows > 0
				&& rows % spnCommitLineValue == 0) {
			int[] counts = stmt.executeBatch();
			int insertCount = 0;
			for (int i = 0; i < counts.length; i++) {
				if (counts[i] > 0) {
					insertCount++;
				}
			}
			conn.commit();
			resetStatement();
			commitedCount += insertCount;
		}
	}

	/**
	 * format datetime to padding "0" if needed
	 * eg: "2009/12/12 20:00:10.1" is needed to be "2009/12/12 20:00:10.100"
	 * 
	 * if failed to format, return the original value
	 * 
	 * @param value
	 * @return
	 */
	private String formatDatetime(String value) {
		String datetime = com.cubrid.cubridmanager.core.CommonTool.formatDateTime(
				value, DATETIME_FORMAT);
		if(datetime==null){
			datetime=value;
		}
		return datetime;
	}

	/**
	 * import data from an Excel file
	 * 
	 * @throws Exception
	 */
	private void importFromExcel() throws Exception {
		Sheet[] sheets = workbook.getSheets();
		Sheet sheet;
		int maxColCount = 0;

		for (int numSheet = 0; numSheet < sheets.length; numSheet++) {
			sheet = sheets[numSheet];
			if (sheet.getColumns() < cntColumn) {
				String msg = Messages.bind(Messages.errNotEnoughtColumns,
						new String[] { sheet.getName(),
								"" + sheet.getColumns(), "" + cntColumn });
				throw new Exception(msg);
			}
		}
		for (int numSheet = 0; numSheet < sheets.length; numSheet++) {
			sheet = sheets[numSheet];
			//no data or just a header-->continue,next sheet
			int rownum = sheet.getRows();
			if ((rownum == 0) || (firstRowAsHeader && rownum == 1)) {
				continue;
			}
			//select columns to import
			boolean[] colInclude = new boolean[cntColumn];
			for (int i = 0; i < colInclude.length; i++) {
				colInclude[i] = false;
			}
			if (firstRowAsHeader) {
				for (int i = 0, j = 0; i < cntColumn && j < columns.length; i++) {
					Cell cell = sheet.getCell(i, 0);
					String cellStr = cell.getContents();
					if (columns[j].equals(cellStr)) {
						colInclude[i] = true;
						j++;
						maxColCount++;
					}
				}
			} else {
				int[] colNumbers = new int[columns.length];
				for (int i = 0; i < colNumbers.length; i++) {
					colNumbers[i] = Integer.parseInt(columns[i].replaceFirst(
							"Column ", "")); //$NON-NLS-1$ //$NON-NLS-2$
					colInclude[colNumbers[i]] = true;
					maxColCount++;
				}
			}

			for (int i = startRow; i < rownum; i++) {
				java.util.List<String> tuple = new ArrayList<String>();
				for (int j = 0; j < cntColumn; j++) {
					Cell cell = sheet.getCell(j, i);
					if (colInclude[j]) {
						if (cell == null) {
							tuple.add(null);
						} else if (cell instanceof EmptyCell) {
							tuple.add(null);
						} else {
							tuple.add(cell.getContents());
						}
					}
				}
				currentRow++;
				if (currentRow == 141) {
					System.out.println();
					;
				}
				addTuple(tuple);
			}
		}
	}

	/**
	 * import data from CVS file
	 * 
	 * @throws FileNotFoundException
	 * @throws IOException
	 * @throws SQLException
	 * @throws ParseException
	 * @throws NumberFormatException
	 */
	private void importFromCVS() throws FileNotFoundException,
			IOException,
			SQLException,
			NumberFormatException,
			ParseException {
		//select columns to import
		boolean[] colInclude = new boolean[cntColumn];
		int maxColCount = 0;
		for (int i = 0; i < colInclude.length; i++) {
			colInclude[i] = false;
		}
		if (firstRowAsHeader) {
			CSVReader csvReader = new CSVReader(new FileReader(file));
			String[] firstRow = csvReader.readNext();
			for (int i = 0, j = 0; i < cntColumn && j < columns.length; i++) {
				if (columns[j].equals(firstRow[i])) {
					colInclude[i] = true;
					j++;
					maxColCount++;
				}
			}
			csvReader.close();
		} else {
			int[] colNumbers = new int[columns.length];
			for (int i = 0; i < colNumbers.length; i++) {
				colNumbers[i] = Integer.parseInt(columns[i].replaceFirst(
						"Column ", "")); //$NON-NLS-1$ //$NON-NLS-2$
				colInclude[colNumbers[i]] = true;
				maxColCount++;
			}
		}
		//set max column count		

		String[] cvsRow;
		CSVReader csvReader = new CSVReader(new FileReader(file));
		if (firstRowAsHeader) {
			csvReader.readNext();
		}
		while ((cvsRow = csvReader.readNext()) != null) {
			//check whether this row has enough column
			if (cvsRow.length < maxColCount) {

				message = getMessage(null);
				hasError = true;
				break;
			}

			java.util.List<String> tuple = new ArrayList<String>();
			for (int i = 0; i < cntColumn; i++) {
				if (colInclude[i]) {
					tuple.add(cvsRow[i]);
				}
			}
			currentRow++;
			addTuple(tuple);
		}
		csvReader.close();
	}

	private String getMessage(Exception e) {
		StringBuffer message = new StringBuffer();
		if (spnCommitLineValue != 0) { //commit
			style |= SWT.OK | SWT.ICON_ERROR;
			if (commitedCount <= 1) {
				message.append(Messages.bind(Messages.importSuccessCountMSG1,
						commitedCount));
			} else {
				message.append(Messages.bind(Messages.importSuccessCountMSG2,
						commitedCount));
			}
		} else {
			style |= SWT.YES | SWT.NO | SWT.ICON_ERROR;
			int insertCount = currentRow - startRow;
			if (insertCount <= 1) {
				message.append(Messages.bind(Messages.importInsertCountMSG1,
						insertCount));
			} else {
				message.append(Messages.bind(Messages.importInsertCountMSG2,
						insertCount));
			}
		}
		if (e == null) {
			int invalidline = (currentRow + 1) - startRow;
			message.append(Messages.bind(Messages.importInvalidCountMSG1,
					invalidline));
		}

		message.append(Messages.importErrorHead);
		if (null != e) {
			message.append(e.getMessage());
		} else {
			message.append(Messages.importInvalidColumnCountMSG);
		}
		if (spnCommitLineValue != 0) {
			message.append(Messages.importRollBackMSG);
		} else {
			message.append(Messages.importWarnRollback);
		}
		return message.toString();
	}

}
