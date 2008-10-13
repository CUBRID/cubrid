package cubridmanager.query.dialog;

import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Types;
import java.util.ArrayList;

import jxl.Cell;
import jxl.Sheet;
import jxl.Workbook;
import jxl.read.biff.BiffException;

import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.SashForm;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
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

import au.com.bytecode.opencsv.CSVReader;
import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDConnectionKey;
import cubrid.jdbc.driver.CUBRIDDatabaseMetaData;
import cubrid.jdbc.driver.CUBRIDKeyTable;
import cubrid.jdbc.driver.CUBRIDPreparedStatement;
import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WaitingMsgBox;
import cubridmanager.query.view.QueryEditor;

public class Import extends Dialog {
	private static final String NEW_LINE = System.getProperty("line.separator");
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="10,10"
	private Label lblTargetTable = null;
	private Label lblImportFile = null;
	private Label lblCommitLine = null;
	private Group grpTop = null;
	private Group grpMiddle = null;
	private Button btnImport = null;
	private Button btnCancel = null;
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
	private ArrayList colName;
	private ArrayList colType;
	private int cntColumn = 0;
	private Composite cmpBottom;
	private boolean prevAutocommit = false;
	public CUBRIDConnectionKey conKey = null;
	/* UI(WaitingMsgBox) related values makes global variable for using by thread. */ 
	private int listFromGetItemCount = 0;
	private String listToGetItem[];
	private int spnCommitLineValue = 0;
	private boolean checkBoxChecked = false;
	private String sql = "";
	private boolean hasError = false;
	private String message = "";
	private int currentRow = 0;
	private int startRow = 0;
	private int style = SWT.NONE;
	private String columns[];
	private WaitingMsgBox waitDlg = null;

	public Import(Shell parent, Connection conn) {
		super(parent);
		try {
			this.conn = conn;
			prevAutocommit = this.conn.getAutoCommit();
			this.conn.setAutoCommit(false);
		} catch (SQLException e) {
			CommonTool.ErrorBox(sShell, e.getErrorCode() + NEW_LINE
					+ Messages.getString("QEDIT.ERRORHEAD") + e.getMessage());
			CommonTool.debugPrint(e);
			sShell.setFocus();
		}
	}

	public int doModal(String tableName) {
		createSShell();
		try {
			addComboItem();
			if (tableName != null && tableName.length() > 0) {
				cmbTargetTable.setText(tableName);
				setToColumn(tableName);
			}
		} catch (SQLException e) {
			CommonTool.ErrorBox(sShell, e.getErrorCode() + NEW_LINE
					+ Messages.getString("QEDIT.ERRORHEAD") + e.getMessage());
			CommonTool.debugPrint(e);
			sShell.setFocus();
		}
		CommonTool.centerShell(sShell);
		sShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	/**
	 * This method initializes sShell
	 */
	private void createSShell() {
		sShell = new Shell(getParent(), SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setSize(new Point(380, 550));
		sShell.setText(Messages.getString("QEDIT.IMPORT"));
		sShell.setLayout(new GridLayout());
		createGrpTop();
		createGrpMiddle();
		createCmpBottom();
	}

	/**
	 * This method initializes grpTop
	 * 
	 */
	private void createGrpTop() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		gridLayout.horizontalSpacing = 0;

		GridData gridData = new GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.verticalAlignment = GridData.FILL;
		gridData.horizontalAlignment = GridData.FILL;

		grpTop = new Group(sShell, SWT.NONE);
		grpTop.setLayoutData(gridData);
		grpTop.setLayout(gridLayout);

		gridData = new GridData();
		gridData.widthHint = 75;
		gridData.horizontalAlignment = GridData.FILL;
		lblTargetTable = new Label(grpTop, SWT.NONE);
		lblTargetTable.setText(Messages.getString("QEDIT.TARGETTABLE"));
		lblTargetTable.setLayoutData(gridData);

		createCmbTargetTable();

		gridData = new GridData();
		gridData.widthHint = 66;
		gridData.verticalSpan = 3;
		btnOpen = new Button(grpTop, SWT.NONE);
		// TODO: image
		btnOpen.setText(Messages.getString("BUTTON.OPENFILE"));
		btnOpen.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						try {
							openFileDialog();
							openFile();
							setFromColumn();
						} catch (IOException e1) {
							CommonTool.ErrorBox(sShell, e1.getMessage());
							CommonTool.debugPrint(e1);
							txtImportFile.setText("");
							fileName = "";
							sShell.setFocus();
						}
					}
				});
		btnOpen.setLayoutData(gridData);

		gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		lblImportFile = new Label(grpTop, SWT.NONE);
		lblImportFile.setText(Messages.getString("QEDIT.IMPORTFILE"));
		lblImportFile.setLayoutData(gridData);

		gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		txtImportFile = new Text(grpTop, SWT.BORDER);
		txtImportFile.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusLost(org.eclipse.swt.events.FocusEvent e) {
						fileName = txtImportFile.getText();
						try {
							openFile();
							setFromColumn();
						} catch (IOException e1) {
							CommonTool.ErrorBox(sShell, e1.getMessage());
							CommonTool.debugPrint(e1);
							txtImportFile.setText("");
							fileName = "";
							sShell.setFocus();
						}
					}
				});
		txtImportFile.setLayoutData(gridData);

		gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		lblCommitLine = new Label(grpTop, SWT.NONE);
		lblCommitLine.setText(Messages.getString("QEDIT.COMMITLINE"));
		lblCommitLine.setLayoutData(gridData);

		gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		spnCommitLine = new Spinner(grpTop, SWT.BORDER);
		spnCommitLine.setMaximum(Integer.MAX_VALUE);
		spnCommitLine.setMinimum(0);
		spnCommitLine.setSelection(5000);
		spnCommitLine.setLayoutData(gridData);
	}

	/**
	 * This method initializes grpBottom
	 * 
	 */
	private void createGrpMiddle() {
		GridData gridData = new GridData();
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = GridData.FILL;
		gridData.verticalAlignment = GridData.FILL;
		grpMiddle = new Group(sShell, SWT.NONE);
		grpMiddle.setLayout(new GridLayout());

		grpMiddle.setLayoutData(gridData);

		gridData = new GridData();
		gridData.grabExcessHorizontalSpace = true;
		lblMappingMessage = new Label(grpMiddle, SWT.NONE);
		lblMappingMessage.setText(Messages.getString("QEDIT.MAPPINGEXCEL"));
		lblMappingMessage.setLayoutData(gridData);

		gridData = new GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalIndent = 10;
		checkBox = new Button(grpMiddle, SWT.CHECK);
		// TODO: image
		checkBox.setText(Messages.getString("QEDIT.USETHE"));
		checkBox.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						try {
							setFromColumn();
						} catch (IOException e1) {
							CommonTool.ErrorBox(sShell, e1.getMessage());
							CommonTool.debugPrint(e1);
							sShell.setFocus();
						}
					}
				});
		checkBox.setLayoutData(gridData);

		createSashColumns();
	}

	/**
	 * This method initializes cmpBottom
	 * 
	 */
	private void createCmpBottom() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		gridLayout.makeColumnsEqualWidth = true;
		gridLayout.horizontalSpacing = 0;
		GridData gridData = new GridData();
		gridData.horizontalAlignment = GridData.END;
		gridData.verticalAlignment = GridData.FILL;
		cmpBottom = new Composite(sShell, SWT.NONE);
		cmpBottom.setLayoutData(gridData);
		cmpBottom.setLayout(gridLayout);

		gridData = new GridData();
		btnImport = new Button(cmpBottom, SWT.NONE);
		// TODO: image
		btnImport.setText(Messages.getString("QEDIT.IMPORT2"));
		btnImport.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						try {
							if (importNow())
								return;

							sShell.dispose();
						} catch (Exception e1) {
							CommonTool.ErrorBox(sShell, e1.getMessage());
							CommonTool.debugPrint(e1);
							sShell.setFocus();
						} catch (Error e1) {
							CommonTool.ErrorBox(sShell, e1.getMessage());
							CommonTool.debugPrint(e1);
							sShell.setFocus();
						}
					}
				});
		btnImport.setLayoutData(gridData);

		btnCancel = new Button(cmpBottom, SWT.NONE);
		// TODO: image
		btnCancel.setText(Messages.getString("QEDIT.CANCEL"));
		btnCancel.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
		btnCancel.setLayoutData(gridData);
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
		cmbTargetTable
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						try {
							setToColumn(cmbTargetTable.getText());
						} catch (SQLException e1) {
							CommonTool.ErrorBox(sShell, e1.getErrorCode()
									+ NEW_LINE + e1.getMessage());
							CommonTool.debugPrint(e1);
							sShell.setFocus();
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
		lblFrom.setText(Messages.getString("QEDIT.FROMCOLUMN"));

		GridData gridData = new GridData();
		gridData.verticalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = GridData.FILL;
		listFrom = new List(fromColumn, SWT.BORDER);
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
		lblTo.setText(Messages.getString("QEDIT.TOCOLUMN"));

		GridData gridData = new GridData();
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = GridData.FILL;
		gridData.verticalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		listTo = new List(toColumn, SWT.BORDER);
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
		itemDel.setText(Messages.getString("QEDIT.DELETE"));
		itemDel.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
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
		itemUp.setText(Messages.getString("QEDIT.UP"));
		itemUp.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (listTo.getItemCount() > 0
								&& listTo.getSelectionCount() > 0) {
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
		itemDown.setText(Messages.getString("QEDIT.DOWN"));
		itemDown.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (listTo.getItemCount() > 0
								&& listTo.getSelectionCount() > 0) {
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

		ToolItem itemDel = new ToolItem(tbTo, SWT.PUSH);
		// TODO: image
		itemDel.setText(Messages.getString("QEDIT.DELETE"));
		itemDel.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (listTo.getItemCount() > 0
								&& listTo.getSelectionCount() > 0) {
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
		CUBRIDDatabaseMetaData dbmd = (CUBRIDDatabaseMetaData) conn
				.getMetaData();
		ResultSet rs = dbmd.getTables(null, null, null,
				new String[] { "table" });

		while (rs.next()) {
			cmbTargetTable.add(rs.getString("table_name"));
		}
		rs.close();
	}

	private void openFileDialog() {
		FileDialog dialog = new FileDialog(sShell, SWT.OPEN
				| SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(new String[] { "*.xls", "*.csv" });
		dialog.setFilterNames(new String[] { "Excel(xls)",
				"Comma separated value(csv)" });
		File curdir = new File(".");
		try {
			dialog.setFilterPath(curdir.getCanonicalPath());
		} catch (Exception e) {
			dialog.setFilterPath(".");
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
		if (fileName.toLowerCase().endsWith(".xls")) {
			try {
				workbook = Workbook.getWorkbook(file);
				cntColumn = workbook.getSheet(0).getColumns();
			} catch (BiffException e) {
				CommonTool.ErrorBox(sShell, Messages
						.getString("QEDIT.FILEERROR"));
				CommonTool.debugPrint(e);
				txtImportFile.setText("");
				fileName = "";
				sShell.setFocus();
			}
		} else if (fileName.toLowerCase().endsWith(".csv")) {
			CSVReader csvReader = new CSVReader(new FileReader(file));
			cntColumn = csvReader.readNext().length;
			csvReader.close();
		}

		sShell.setActive();
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
			if (fileName.toLowerCase().endsWith(".xls")) {
				for (int j = 0; j < cntColumn; j++) {
					Cell cell = workbook.getSheet(0).getCell(j, 0);
					listFrom.add(cell == null ? "" : cell.getContents());
				}
			} else if (fileName.toLowerCase().endsWith(".csv")) {
				CSVReader csvReader = new CSVReader(new FileReader(file));
				String[] csvColumn = csvReader.readNext();
				for (int j = 0; j < cntColumn; j++) {
					listFrom.add(csvColumn[j] == null ? "" : csvColumn[j]);
				}
				csvReader.close();
			}
		} else {
			for (int i = 0; i < cntColumn; i++) {
				listFrom.add("Column " + i);
			}
		}
	}

	private void setToColumn(String table) throws SQLException {
		try {
			CUBRIDDatabaseMetaData dbmd = (CUBRIDDatabaseMetaData) conn
					.getMetaData();
			ResultSet rs = dbmd.getColumns(null, null, table, null);
			colName = new ArrayList();
			colType = new ArrayList();

			listTo.removeAll();

			while (rs.next()) {
				listTo.add(rs.getString("column_name"));
				colName.add(rs.getString("column_name"));
				colType.add(rs.getString("type_name"));
			}
			rs.close();

		} catch (IllegalArgumentException e1) {
			CommonTool.ErrorBox(sShell, e1.getMessage());
			CommonTool.debugPrint(e1);
			sShell.setFocus();
		}
	}

	private boolean importNow() throws IOException, Error {
		MessageBox mb = new MessageBox(sShell, SWT.ICON_WARNING | SWT.OK);
		mb.setText(Messages.getString("QEDIT.WARNING"));

		if (cmbTargetTable.getText().length() < 1) {
			mb.setMessage(Messages.getString("QEDIT.TABLEWARN"));
			mb.open();
			cmbTargetTable.setFocus();
			return true;
		}
		if (txtImportFile.getText().length() < 1) {
			mb.setMessage(Messages.getString("QEDIT.FILEWARN"));
			mb.open();
			txtImportFile.setFocus();
			return true;
		}
		// if (rows.size() < 1) {
		// mb.setMessage(Messages.getString("QEDIT.READWARN"));
		// mb.open();
		// return true;
		// }
		if (listFrom.getItemCount() != listTo.getItemCount()) {
			mb.setMessage(Messages.getString("QEDIT.CNTWARN"));
			mb.open();
			listFrom.setFocus();
			return true;
		}
		if (listFrom.getItemCount() < 1) {
			mb.setMessage(Messages.getString("QEDIT.COLWARN"));
			mb.open();
			listFrom.setFocus();
			return true;
		}
		if (listTo.getItemCount() < 1) {
			mb.setMessage(Messages.getString("QEDIT.COLWARN"));
			mb.open();
			listTo.setFocus();
			return true;
		}

		String insert = "insert into " + cmbTargetTable.getText() + " (";
		String values = "values (";

		for (int i = 0; i < listTo.getItemCount(); i++) {
			if (i > 0) {
				insert += ", ";
				values += ", ";
			}

			insert += listTo.getItem(i);

			String type = colType.get(colName.indexOf(listTo.getItem(i)))
					.toString();
			if (type.startsWith("BIT")) {
				values += "cast(? as bit varying)";
			}
			// else if (type.startsWith("NCHAR")) {
			// }
			else
				values += "?";
		}

		insert += ") ";
		values += ");";

		sql = insert + values;

		listFromGetItemCount = listFrom.getItemCount();
		listToGetItem = new String[listFromGetItemCount];
		columns = new String[listFromGetItemCount];
		spnCommitLineValue = spnCommitLine.getSelection();
		checkBoxChecked = checkBox.getSelection();

		for (int i = 0; i < listFromGetItemCount; i++)
			listToGetItem[i] = listTo.getItem(i);

		message = "";
		startRow = 0;
		if (checkBoxChecked)
			startRow = 1;

		currentRow = startRow;
		columns = listFrom.getItems();

		waitDlg = new WaitingMsgBox(Application.mainwindow.getShell());
		waitDlg.setJobEndState(false);

		Thread execThread = new Thread() {
			public void run() {
				int commitedCount = 0;
				try {
					if (MainRegistry.isProtegoBuild()) {
						CUBRIDKeyTable.putValue(conKey);
					}
					try {
						if (fileName.toLowerCase().endsWith(".xls")) {
							Sheet[] sheets = workbook.getSheets();
							Sheet sheet;
							for (int numSheet = 0, n = sheets.length; numSheet < n; numSheet++) {
								sheet = sheets[numSheet];
								if ((sheet.getRows() == 0)
										|| (checkBoxChecked && sheet.getRows() == 1))
									continue;

								boolean[] colInclude = new boolean[cntColumn];
								for (int i = 0; i < colInclude.length; i++)
									colInclude[i] = false;

								if (checkBoxChecked) {
									for (int i = 0, j = 0; i < cntColumn
											&& j < columns.length; i++) {
										Cell cell = sheet.getCell(i, 0);
										String cellStr = cell.getContents();
										if (columns[j].equals(cellStr)) {
											colInclude[i] = true;
											j++;
										}
									}
								} else {
									int[] colNumbers = new int[columns.length];
									for (int i = 0; i < colNumbers.length; i++) {
										colNumbers[i] = Integer
												.parseInt(columns[i]
														.replaceFirst(
																"Column ", ""));
										colInclude[colNumbers[i]] = true;
									}
								}

								for (int i = startRow; i < sheet.getRows(); i++) {
									ArrayList tuple = new ArrayList();
									for (int j = 0; j < cntColumn; j++) {
										Cell cell = sheet.getCell(j, i);
										String str = cell.getContents();
										if (colInclude[j])
											tuple.add(cell == null ? "" : str);
									}
									insertToDatabase(sql, tuple);
									currentRow++;

									if (spnCommitLineValue != 0
											&& (currentRow - startRow)
													% spnCommitLineValue == 0) {
										conn.commit();
										commitedCount += spnCommitLineValue;
									}
								}
							}
						} else {
							boolean[] colInclude = new boolean[cntColumn];
							int maxColCount = 0;
							for (int i = 0; i < colInclude.length; i++)
								colInclude[i] = false;
							if (checkBoxChecked) {
								CSVReader csvReader = new CSVReader(
										new FileReader(file));
								String[] csvColumn = csvReader.readNext();
								for (int i = 0, j = 0; i < cntColumn
										&& j < columns.length; i++) {
									if (columns[j].equals(csvColumn[i])) {
										colInclude[i] = true;
										j++;
									}
								}
								csvReader.close();
							} else {
								int[] colNumbers = new int[columns.length];
								for (int i = 0; i < colNumbers.length; i++) {
									colNumbers[i] = Integer.parseInt(columns[i]
											.replaceFirst("Column ", ""));
									colInclude[colNumbers[i]] = true;
								}
							}
							for (int i = 0; i < cntColumn; i++) {
								if (colInclude[i])
									maxColCount = i + 1;
							}

							String[] csvColumn;
							CSVReader csvReader = new CSVReader(new FileReader(
									file));
							if (checkBoxChecked)
								csvReader.readNext();
							while ((csvColumn = csvReader.readNext()) != null) {
								ArrayList tuple = new ArrayList();
								if (csvColumn.length < maxColCount) {
									style |= SWT.OK | SWT.ICON_ERROR;
									message = commitedCount
											+ Messages
													.getString("QEDIT.IMPORTCOMMITED")
											+ +((currentRow + 1) - startRow)
											+ Messages
													.getString("QEDIT.IMPORTERROR")
											+ Messages
													.getString("QEDIT.ERRORHEAD")
											+ Messages
													.getString("QEDIT.IMPORTERROR_INVALIDCOLUMNCOUNT")
											+ NEW_LINE
											+ Messages
													.getString("QEDIT.IMPORTROLLBACK");
									hasError = true;
									break;
								}

								for (int i = 0; i < cntColumn; i++) {
									if (colInclude[i])
										tuple.add(csvColumn[i]);
								}

								insertToDatabase(sql, tuple);
								currentRow++;

								if (spnCommitLineValue != 0
										&& (currentRow - startRow)
												% spnCommitLineValue == 0) {
									conn.commit();
									commitedCount += spnCommitLineValue;
								}
							}
							csvReader.close();
						}
					} catch (SQLException e) {
						hasError = true;
						if (spnCommitLineValue != 0) {
							style |= SWT.OK | SWT.ICON_ERROR;
							message = commitedCount
									+ Messages
											.getString("QEDIT.IMPORTCOMMITED")
									+ ((currentRow + 1) - startRow)
									+ Messages.getString("QEDIT.IMPORTERROR")
									+ Messages.getString("QEDIT.ERRORHEAD")
									+ e.getMessage()
									+ NEW_LINE
									+ Messages
											.getString("QEDIT.IMPORTROLLBACK");
						} else {
							style |= SWT.YES | SWT.NO | SWT.ICON_ERROR;
							message = (currentRow - startRow)
									+ Messages.getString("QEDIT.INSERTOK")
									+ ((currentRow + 1) - startRow)
									+ Messages.getString("QEDIT.IMPORTERROR")
									+ Messages.getString("QEDIT.ERRORHEAD")
									+ e.getMessage()
									+ NEW_LINE
									+ Messages
											.getString("QEDIT.QUESTIONCOMMIT")
									+ Messages
											.getString("QEDIT.IMPORTWARNROLLBACK");
						}
					} catch (Exception e1) {
						CommonTool.ErrorBox(sShell, e1.getMessage());
						CommonTool.debugPrint(e1);
					} catch (Error e1) {
						CommonTool.ErrorBox(sShell, e1.getMessage());
						CommonTool.debugPrint(e1);
					}
					waitDlg.setJobEndState(true);
				} catch (Exception e) {
					waitDlg.setJobEndState(true);
				}

			}
		};

		execThread.start();

		try {
			execThread.join(1000);
			if (!waitDlg.getJobEndState()) {
				MainRegistry.WaitDlg = true;
				waitDlg.run(Messages.getString("WAITING.IMPORT"));
			}

			if (execThread.isAlive())
				execThread.join();
		} catch (Exception e) {
			CommonTool.debugPrint("Exception!!!" + e.toString());
			CommonTool.debugPrint(e);
		}

		if (!hasError) {
			style |= SWT.OK | SWT.ICON_INFORMATION;
			message = (currentRow - startRow)
					+ Messages.getString("QEDIT.IMPORTOK");
		}

		mb = new MessageBox(sShell, style);
		mb.setText(Messages.getString("QEDIT.IMPORT"));
		mb.setMessage(message);
		try {
			if (hasError) {
				if (spnCommitLineValue == 0) {
					if (mb.open() == SWT.YES)
						conn.commit();
					else
						conn.rollback();
				} else {
					mb.open();
					conn.rollback();
				}
			} else {
				mb.open();
				conn.commit();
			}
		} catch (SQLException e) {
			CommonTool.debugPrint(e);
		} finally {
			try {
				conn.setAutoCommit(prevAutocommit);
			} catch (SQLException e) {
				CommonTool.debugPrint(e);
			}
		}

		try {
			if (MainRegistry.isProtegoBuild()) {
				((CUBRIDConnection) conn).Logout();
			}
			conn.close();
		} catch (Exception e) {
			CommonTool.debugPrint("Exception!!!" + e.toString());
			CommonTool.debugPrint(e);
		}
		return hasError;
	}

	private void insertToDatabase(String sql, ArrayList colValues)
			throws SQLException {
		CUBRIDPreparedStatement stmt = (CUBRIDPreparedStatement) conn
				.prepareStatement(sql);
		String value;
		for (int i = 0; i < listFromGetItemCount; i++) {
			value = (String) colValues.get(i);
			if (value.equals(QueryEditor.STR_NULL)
					|| value.indexOf("GLO") > -1
					|| colType.get(colName.indexOf(listToGetItem[i]))
							.toString().startsWith("NCHAR"))
				stmt.setNull(i + 1, Types.NULL);
			/*
			 * //set type 
			 * else if
			 * (colType.get(colName.indexOf(listTo.getItem(i))).toString().equals("SET") ||
			 * colType.get(colName.indexOf(listTo.getItem(i))).toString().equals("MULTISET") ||
			 * colType.get(colName.indexOf(listTo.getItem(i))).toString().equals("SEQUENCE")) {
			 * Object[] setItems = value.substring(1, value.length() -
			 * 1).split(","); for (int idx = 0; idx <
			 * setItems.length; idx++) { setItems[idx] =
			 * setItems[idx].toString().trim(); } stmt.setCollection(i+1,
			 * setItems); if (value.indexOf("'") < 0) {
			 * Integer [] tmp = new Integer[setItems.length]; // for (int x = 0;
			 * x < tmp.length; x++) { tmp[x] = new
			 * Integer(Integer.parseInt(((String) setItems[x]).trim())); }
			 * stmt.setCollection(i+1, tmp); }  }
			 */
			else
				stmt.setString(i + 1, value);
		}
		stmt.executeUpdate();
		stmt.close();
	}
}
