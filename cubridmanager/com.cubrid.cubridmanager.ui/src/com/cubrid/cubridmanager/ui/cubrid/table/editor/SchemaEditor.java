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
package com.cubrid.cubridmanager.ui.cubrid.table.editor;

import java.util.List;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.IEditorInput;
import org.eclipse.ui.IEditorSite;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;
import org.eclipse.ui.part.EditorPart;

import com.cubrid.cubridmanager.core.CommonTool;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
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
import com.cubrid.cubridmanager.ui.spi.CubridEditorPart;
import com.cubrid.cubridmanager.ui.spi.SWTResourceManager;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

public class SchemaEditor extends
		CubridEditorPart {
	public static final String ID = "com.cubrid.cubridmanager.ui.cubrid.table.editor.SchemaEditor"; //$NON-NLS-1$
	private static final Logger logger = LogUtil.getLogger(SchemaEditor.class);
	private Composite top = null;
	private Table table = null;
	private Label label1 = null;
	private Label label2 = null;
	private Text txtViewSpec = null;
	private TableViewer columnTableView;
	private AttributeTableViewerContentProvider attrContentProvider;
	private SchemaInfo newSchema;
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

	private String nodeName;
	private DatabaseInfo database;
	@SuppressWarnings("unused")
	private ServerInfo server;
	private ICubridNode node;
	private DefaultSchemaNode schemaNode;
	private boolean success;

	@Override
	public void init(IEditorSite site, IEditorInput input) throws PartInitException {
		super.init(site, input);
		if (input instanceof DefaultSchemaNode) {
			node = (DefaultSchemaNode) input;
			if (null == node
					|| node.getType() != CubridNodeType.USER_PARTITIONED_TABLE_FOLDER
					&& node.getType() != CubridNodeType.USER_TABLE
					&& node.getType() != CubridNodeType.USER_VIEW
					&& node.getType() != CubridNodeType.SYSTEM_TABLE
					&& node.getType() != CubridNodeType.SYSTEM_VIEW
					&& node.getType() != CubridNodeType.USER_PARTITIONED_TABLE) {
				return;
			}

			nodeName = node.getLabel().trim();
			schemaNode = (DefaultSchemaNode) node;
			server = schemaNode.getServer().getServerInfo();
			database = schemaNode.getDatabase().getDatabaseInfo();
			success = getSchema();
			this.setTitleImage(node.getImageDescriptor().createImage());
		}
	}

	/**
	 * 
	 * @param server
	 */
	private boolean getSchema() {
		newSchema = database.getSchemaInfo(nodeName);
		if (null != newSchema) {
			return true;
		} else {
			com.cubrid.cubridmanager.ui.spi.CommonTool.openErrorBox(database.getErrorMessage());
			logger.debug(database.getErrorMessage());
			return false;
		}
	}

	public boolean loadData() {
		newSchema = database.getSchemaInfo(nodeName);
		if (newSchema != null) {
			fillTable();
			columnTableView.setInput(newSchema);
			fkTableView.setInput(newSchema);
			indexTableView.setInput(newSchema);
			fillTextViewSpec();
			return true;
		} else {
			com.cubrid.cubridmanager.ui.spi.CommonTool.openErrorBox(database.getErrorMessage());
			logger.debug(database.getErrorMessage());
			return false;
		}

	}

	public void nodeChanged(CubridNodeChangedEvent e) {
		ICubridNode eventNode = e.getCubridNode();
		if (eventNode == null
				|| e.getType() != CubridNodeChangedEventType.CONTAINER_NODE_REFRESH) {
			return;
		}
		if (eventNode.getChild(node != null ? node.getId() : "") == null) { //$NON-NLS-1$
			return;
		}
		database.clearSchemas();
		loadData();
	}

	public void createPartControl(Composite parent) {
		if (!success) {
			IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
			IWorkbenchPage page = window.getActivePage();
			EditorPart editor = (EditorPart) page.getActiveEditor();
			if (editor != null) {
				//				page.activate(editor);
				page.closeEditor(this, false);
			}
			return;
		}

		GridLayout gridLayout = new GridLayout();
		gridLayout.marginWidth = 0;
		gridLayout.marginHeight = 0;
		top = new Composite(parent, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		top.setLayout(gridLayout);

		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		whs.setHelp(top, CubridManagerHelpContextIDs.schemaEditor);

		if (newSchema != null) {
			label1 = new Label(top, SWT.LEFT | SWT.WRAP);
			label1.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, false,
					false));
			label1.setText(newSchema.getClassname());
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
			if (newSchema.getVirtual().equals("normal")) { //$NON-NLS-1$
				label2.setText(Messages.lblFK);
				createTable3();
			} else if (newSchema.getVirtual().equals("view")) { //$NON-NLS-1$
				label2.setText(Messages.lblQuerySpec);
				createTextViewSpec();
			}
		}
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		GridData gridData = new GridData(SWT.FILL, SWT.CENTER, true, false);
		gridData.heightHint = 70;
		gridData.widthHint = 200;
		table = new Table(top, SWT.FULL_SELECTION);
		table.setLayoutData(gridData);
		new TableColumn(table, SWT.LEFT);
		new TableColumn(table, SWT.LEFT);

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(10, true));
		tlayout.addColumnData(new ColumnWeightData(65, true));
		table.setLayout(tlayout);
		fillTable();
	}

	private void fillTable() {
		if (newSchema == null || table == null || table.isDisposed())
			return;
		table.removeAll();
		TableItem item;
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.colSchemaType);
		item.setText(
				1,
				newSchema.getType().equals("system") ? Messages.infoSystemSchema //$NON-NLS-1$
						: Messages.infoUserSchema);
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.infoOwner);
		item.setText(1, newSchema.getOwner());
		item = new TableItem(table, SWT.NONE);
		item.setText(0, Messages.infoSuperClasses);
		String superstr = ""; //$NON-NLS-1$
		List<String> superClasses = newSchema.getSuperClasses();
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
				newSchema.getVirtual().equals("normal") ? Messages.typeTable //$NON-NLS-1$
						: Messages.typeView);
		//		setPartName(item.getText(1).concat(" information"));
		String msg = Messages.bind(Messages.titleSchemEditPart, nodeName);
		setPartName(msg);

	}

	private void createTable2() {
		columnTableView = new TableViewer(top, SWT.FULL_SELECTION | SWT.MULTI
				| SWT.BORDER);
		columnTableView.setColumnViewerEditor(null);
		Table columnsTable = columnTableView.getTable();

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
				newSchema);

		columnTableView.setContentProvider(attrContentProvider);
		columnTableView.setLabelProvider(attrLabelProvider);

		columnsTable.setLinesVisible(true);
		columnsTable.setHeaderVisible(true);

		columnTableView.setInput(newSchema);
		columnTableView.refresh();
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
		fkLabelProvider = new FKTableViewerLabelProvider(newSchema, database);

		fkTableView.setContentProvider(fkContentProvider);
		fkTableView.setLabelProvider(fkLabelProvider);
		fkTableView.setInput(newSchema);

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
		indexLabelProvider = new IndexTableViewerLabelProvider(newSchema);

		indexTableView.setContentProvider(indexContentProvider);
		indexTableView.setLabelProvider(indexLabelProvider);
		indexTableView.setInput(newSchema);
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
		List<String> querySpecs = newSchema.getQuerySpecs();
		for (int i = 0, n = querySpecs.size(); i < n; i++) {
			txtViewSpec.append(querySpecs.get(i));
			txtViewSpec.append(CommonTool.newLine);
		}
	}

	@Override
	public void doSave(IProgressMonitor monitor) {
		firePropertyChange(PROP_DIRTY);
	}

	@Override
	public void doSaveAs() {

	}

	@Override
	public boolean isDirty() {
		return false;
	}

	@Override
	public boolean isSaveAsAllowed() {
		return false;
	}

	@Override
	public void setFocus() {

	}
}
