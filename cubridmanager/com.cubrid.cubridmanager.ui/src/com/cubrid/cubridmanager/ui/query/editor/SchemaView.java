package com.cubrid.cubridmanager.ui.query.editor;

import java.util.List;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.TableEditor;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IViewSite;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;
import org.eclipse.ui.part.ViewPart;

import com.cubrid.cubridmanager.core.CommonTool;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.cubrid.table.control.AttributeTableViewerContentProvider;
import com.cubrid.cubridmanager.ui.cubrid.table.control.AttributeTableViewerLabelProvider;
import com.cubrid.cubridmanager.ui.cubrid.table.control.FKTableViewerContentProvider;
import com.cubrid.cubridmanager.ui.cubrid.table.control.FKTableViewerLabelProvider;
import com.cubrid.cubridmanager.ui.cubrid.table.control.IndexTableViewerContentProvider;
import com.cubrid.cubridmanager.ui.cubrid.table.control.IndexTableViewerLabelProvider;
import com.cubrid.cubridmanager.ui.spi.SWTResourceManager;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * 
 * Schema Info View for a query editor and a query explain tool in a query
 * editor
 * 
 * SchemaView Description
 * 
 * @author pcraft
 * @version 1.0 - 2009. 06. 06 created by pcraft
 */
public class SchemaView extends
		ViewPart {
	public static final String ID = "com.cubrid.cubridmanager.ui.query.editor.SchemaView";

	private Composite top;
	private Table table = null;
	private Label label1 = null;
	private Label label2 = null;
	private Text txtViewSpec = null;
	private TableViewer columnTableView;
	private AttributeTableViewerContentProvider attrContentProvider;
	private SchemaInfo schemaInfo;
	private AttributeTableViewerLabelProvider attrLabelProvider;
	private TableViewer fkTableView;
	private FKTableViewerContentProvider fkContentProvider;
	private FKTableViewerLabelProvider fkLabelProvider;
	private TableViewer indexTableView;
	private IndexTableViewerContentProvider indexContentProvider;
	private IndexTableViewerLabelProvider indexLabelProvider;

	private static final int Width_uniqueColumn = 70;
	private static final int Width_notNullColumn = 70;
	private static final int Width_dataTypeColumn = 120;
	private static final int Width_nameColumn = 90;
	private static final int Width_pkColumn = 30;
	private static final int Width_sharedColumn = 70;

	private DatabaseInfo database = null;

	private Table columnsTable;

	@Override
	public void init(IViewSite site) throws PartInitException {
		super.init(site);

		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (null == window) {
			return;
		}

		IEditorPart editor = window.getActivePage().getActiveEditor();
		if (editor == null) {
			return;
		}

		if (!(editor instanceof QueryEditorPart)) {
			return;
		}

		QueryEditorPart queryEditorPart = (QueryEditorPart) editor;

		String tableName = queryEditorPart.getCurrentSchemaName();
		if (tableName == null) {
			return;
		}

		CubridDatabase db = queryEditorPart.getSelectedDatabase();
		if (db == null || !db.isLogined()) {
			return;
		}

		database = db.getDatabaseInfo();
		if (database == null) {
			return;
		}

		setPartName(com.cubrid.cubridmanager.ui.query.Messages.schemaInfoViewTitle);

		try {
			schemaInfo = database.getSchemaInfo(tableName);
		} catch (Exception ignored) {
			schemaInfo = null;
		}

	}

	@Override
	public void createPartControl(Composite parent) {
		GridLayout gridLayout = new GridLayout();
		gridLayout.marginWidth = 0;
		gridLayout.marginHeight = 0;
		top = new Composite(parent, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		top.setLayout(gridLayout);

		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		whs.setHelp(top, CubridManagerHelpContextIDs.schemaView);

		if (schemaInfo != null) {
			label1 = new Label(top, SWT.LEFT | SWT.WRAP);
			label1.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, false,
					false));
			label1.setText(schemaInfo.getClassname());
			label1.setFont(SWTResourceManager.getFont(
					label1.getFont().toString(), 14, SWT.BOLD));
			label1.setBackground(Display.getCurrent().getSystemColor(
					SWT.COLOR_WHITE));
			createTable();
			Label label = new Label(top, SWT.LEFT | SWT.WRAP);
			label.setText(Messages.lblColumns);
			label.setBackground(Display.getCurrent().getSystemColor(
					SWT.COLOR_WHITE));
			createTable2();

			label2 = new Label(top, SWT.LEFT | SWT.WRAP);
			label2.setBackground(Display.getCurrent().getSystemColor(
					SWT.COLOR_WHITE));
			if (schemaInfo.getVirtual().equals("normal")) { //$NON-NLS-1$
				label2.setText(Messages.lblFK);
				createTable3();
			} else if (schemaInfo.getVirtual().equals("view")) { //$NON-NLS-1$
				label2.setText(Messages.lblQuerySpec);
				createTextViewSpec();
			}
		}

	}

	private void createTable() {
		GridData gridData = new GridData(SWT.FILL, SWT.CENTER, true, false);
		gridData.heightHint = 70;
		gridData.widthHint = 200;
		table = new Table(top, SWT.FULL_SELECTION);
		table.setLayoutData(gridData);
		new TableColumn(table, SWT.LEFT);
		new TableColumn(table, SWT.LEFT);

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(40, true));
		tlayout.addColumnData(new ColumnWeightData(60, true));
		table.setLayout(tlayout);
		fillTable();
	}

	private void fillTable() {
		if (schemaInfo == null || table == null || table.isDisposed())
			return;
		table.removeAll();
		TableItem item;
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.colSchemaType);
		item.setText(
				1,
				schemaInfo.getType().equals("system") ? Messages.infoSystemSchema //$NON-NLS-1$
						: Messages.infoUserSchema);

		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.infoOwner);
		item.setText(1, schemaInfo.getOwner());

		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.infoSuperClasses);
		String superstr = ""; //$NON-NLS-1$
		List<String> superClasses = schemaInfo.getSuperClasses();
		for (int si = 0; si < superClasses.size(); si++) {
			if (si > 0) {
				superstr = superstr.concat(", "); //$NON-NLS-1$
			}
			superstr = superstr.concat(superClasses.get(si));
		}
		item.setText(1, superstr);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.infoType);
		item.setText(1,
				schemaInfo.getVirtual().equals("normal") ? Messages.typeTable //$NON-NLS-1$
						: Messages.typeView);
	}

	private void createTable2() {
		columnTableView = new TableViewer(top, SWT.FULL_SELECTION | SWT.SINGLE
				| SWT.BORDER);
		columnTableView.setColumnViewerEditor(null);
		columnsTable = columnTableView.getTable();

		final GridData gd_columnsTable = new GridData(SWT.FILL, SWT.FILL, true,
				true);
		gd_columnsTable.heightHint = 189;
		columnsTable.setLayoutData(gd_columnsTable);

		final TableColumn pkColumn = new TableColumn(columnsTable, SWT.NONE);
		pkColumn.setAlignment(SWT.CENTER);
		pkColumn.setWidth(Width_pkColumn);
		pkColumn.setText(Messages.tblColumnPK);

		final TableColumn nameColumn = new TableColumn(columnsTable, SWT.NONE);
		nameColumn.setWidth(Width_nameColumn);
		nameColumn.setText(Messages.tblColumnName);

		final TableColumn dataTypeColumn = new TableColumn(columnsTable,
				SWT.NONE);
		dataTypeColumn.setWidth(Width_dataTypeColumn);
		dataTypeColumn.setText(Messages.tblColumnDataType);

		final TableColumn newColumnTableColumn = new TableColumn(columnsTable,
				SWT.NONE);
		newColumnTableColumn.setAlignment(SWT.CENTER);
		newColumnTableColumn.setWidth(100);
		newColumnTableColumn.setText(Messages.tblColumnAutoIncr);

		final TableColumn defaultColumn = new TableColumn(columnsTable,
				SWT.NONE);
		defaultColumn.setWidth(98);
		defaultColumn.setText(Messages.tblColumnDefault);

		final TableColumn notNullColumn = new TableColumn(columnsTable,
				SWT.NONE);
		notNullColumn.setWidth(Width_notNullColumn);
		notNullColumn.setText(Messages.tblColumnNotNull);
		notNullColumn.setAlignment(SWT.CENTER);

		final TableColumn uniqueColumn = new TableColumn(columnsTable, SWT.NONE);
		uniqueColumn.setWidth(Width_uniqueColumn);
		uniqueColumn.setText(Messages.tblColumnUnique);
		uniqueColumn.setAlignment(SWT.CENTER);

		final TableColumn sharedColumn = new TableColumn(columnsTable, SWT.NONE);
		sharedColumn.setWidth(0);
		sharedColumn.setResizable(false);
		sharedColumn.setText(Messages.tblColumnShared);
		sharedColumn.setAlignment(SWT.CENTER);

		final TableColumn inheritColumn = new TableColumn(columnsTable,
				SWT.NONE);
		inheritColumn.setAlignment(SWT.CENTER);
		inheritColumn.setWidth(0);
		inheritColumn.setResizable(false);
		inheritColumn.setText(Messages.tblColumnInherit);

		final TableColumn classColumn = new TableColumn(columnsTable, SWT.NONE);
		classColumn.setWidth(0);
		classColumn.setResizable(false);
		classColumn.setText(Messages.tblColumnClass);
		classColumn.setAlignment(SWT.CENTER);

		sharedColumn.setWidth(Width_sharedColumn);
		sharedColumn.setResizable(true);

		inheritColumn.setWidth(90);
		inheritColumn.setResizable(true);

		classColumn.setWidth(90);
		classColumn.setResizable(true);

		attrContentProvider = new AttributeTableViewerContentProvider();

		attrLabelProvider = new AttributeTableViewerLabelProvider(database,
				schemaInfo);

		columnTableView.setContentProvider(attrContentProvider);
		columnTableView.setLabelProvider(attrLabelProvider);

		columnsTable.setLinesVisible(true);
		columnsTable.setHeaderVisible(true);

		columnTableView.setInput(schemaInfo);
		columnTableView.refresh();

		editor = new TableEditor(columnsTable);
		editor.horizontalAlignment = SWT.LEFT;
		editor.grabHorizontal = true;

		columnsTable.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				// Identify the selected row
				TableItem item = (TableItem) e.item;
				if (item == null)
					return;
				setFocus(item);
			}
		});
	}

	private void createTable3() {
		fkTableView = new TableViewer(top, SWT.FULL_SELECTION | SWT.MULTI
				| SWT.BORDER);
		fkTableView.setColumnViewerEditor(null);
		Table fkTable = fkTableView.getTable();

		final GridData gd_fkTable = new GridData(SWT.FILL, SWT.FILL, true,
				true, 4, 1);
		fkTable.setLayoutData(gd_fkTable);
		fkTable.setLinesVisible(true);
		fkTable.setHeaderVisible(true);

		final TableColumn newColumnTableColumn_1 = new TableColumn(fkTable,
				SWT.NONE);
		newColumnTableColumn_1.setWidth(100);
		newColumnTableColumn_1.setText(Messages.tblColumnFK);

		final TableColumn newColumnTableColumn_2 = new TableColumn(fkTable,
				SWT.NONE);
		newColumnTableColumn_2.setWidth(119);
		newColumnTableColumn_2.setText(Messages.tblColumnColumnName);

		final TableColumn newColumnTableColumn_3 = new TableColumn(fkTable,
				SWT.NONE);
		newColumnTableColumn_3.setWidth(93);
		newColumnTableColumn_3.setText(Messages.tblColumnForeignTable);

		final TableColumn newColumnTableColumn_4 = new TableColumn(fkTable,
				SWT.NONE);
		newColumnTableColumn_4.setWidth(143);
		newColumnTableColumn_4.setText(Messages.tblColumnForeignColumnName);

		final TableColumn newColumnTableColumn_5 = new TableColumn(fkTable,
				SWT.NONE);
		newColumnTableColumn_5.setWidth(84);
		newColumnTableColumn_5.setText(Messages.tblColumnUpdateRule);

		final TableColumn newColumnTableColumn_6 = new TableColumn(fkTable,
				SWT.NONE);
		newColumnTableColumn_6.setWidth(86);
		newColumnTableColumn_6.setText(Messages.tblColumnDeleteRule);

		final TableColumn newColumnTableColumn_7 = new TableColumn(fkTable,
				SWT.NONE);
		newColumnTableColumn_7.setWidth(100);
		newColumnTableColumn_7.setText(Messages.tblColumnCacheColumn);

		fkContentProvider = new FKTableViewerContentProvider();
		fkLabelProvider = new FKTableViewerLabelProvider(schemaInfo, database);

		fkTableView.setContentProvider(fkContentProvider);
		fkTableView.setLabelProvider(fkLabelProvider);
		fkTableView.setInput(schemaInfo);

		Label label = new Label(top, SWT.LEFT | SWT.WRAP);
		label.setText(Messages.lblIndexes);
		label.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));

		indexTableView = new TableViewer(top, SWT.FULL_SELECTION | SWT.MULTI
				| SWT.BORDER);
		indexTableView.setColumnViewerEditor(null);
		Table indexTable = indexTableView.getTable();

		indexTable.setLinesVisible(true);
		indexTable.setHeaderVisible(true);
		final GridData gd_indexTable = new GridData(SWT.FILL, SWT.FILL, true,
				true, 4, 1);
		indexTable.setLayoutData(gd_indexTable);

		final TableColumn newColumnTableColumn_1_1 = new TableColumn(
				indexTable, SWT.NONE);
		newColumnTableColumn_1_1.setWidth(100);
		newColumnTableColumn_1_1.setText(Messages.tblColumnIndexName);

		final TableColumn newColumnTableColumn_2_1 = new TableColumn(
				indexTable, SWT.NONE);
		newColumnTableColumn_2_1.setWidth(78);
		newColumnTableColumn_2_1.setText(Messages.tblColumnIndexType);

		final TableColumn newColumnTableColumn_3_1 = new TableColumn(
				indexTable, SWT.NONE);
		newColumnTableColumn_3_1.setWidth(218);
		newColumnTableColumn_3_1.setText(Messages.tblColumnOnColumns);

		final TableColumn newColumnTableColumn_4_1 = new TableColumn(
				indexTable, SWT.NONE);
		newColumnTableColumn_4_1.setWidth(332);
		newColumnTableColumn_4_1.setText(Messages.tblColumnIndexRule);

		indexContentProvider = new IndexTableViewerContentProvider();
		indexLabelProvider = new IndexTableViewerLabelProvider(schemaInfo);

		indexTableView.setContentProvider(indexContentProvider);
		indexTableView.setLabelProvider(indexLabelProvider);
		indexTableView.setInput(schemaInfo);
	}

	private void createTextViewSpec() {
		txtViewSpec = new Text(top, SWT.WRAP | SWT.BORDER);
		txtViewSpec.setLayoutData(new GridData(GridData.FILL_BOTH));
		txtViewSpec.setEditable(false);
		txtViewSpec.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		fillTextViewSpec();
	}

	private void fillTextViewSpec() {
		if (txtViewSpec == null || txtViewSpec.isDisposed())
			return;
		txtViewSpec.setText(""); //$NON-NLS-1$
		List<String> querySpecs = schemaInfo.getQuerySpecs();
		for (int i = 0, n = querySpecs.size(); i < n; i++) {
			txtViewSpec.append(querySpecs.get(i));
			txtViewSpec.append(CommonTool.newLine);
		}
	}

	@Override
	public void setFocus() {
	}

	public void nodeChanged(CubridNodeChangedEvent e) {
	}

	private TableEditor editor = null;

	//set edit cell focus
	private void setFocus(TableItem item) {
		// Clean up any previous editor control
		int editColumn = 1;
		Control oldEditor = editor.getEditor();
		if (oldEditor != null)
			oldEditor.dispose();

		Text newEditor = new Text(columnsTable, SWT.READ_ONLY);
		newEditor.setText(item.getText(editColumn));
		newEditor.selectAll();
		newEditor.setFocus();
		editor.setEditor(newEditor, item, editColumn);
	}

}
