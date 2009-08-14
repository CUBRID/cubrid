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

import java.util.ArrayList;
import java.util.List;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.sp.task.CommonSQLExcuterTask;
import com.cubrid.cubridmanager.core.cubrid.table.model.Constraint;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBResolution;
import com.cubrid.cubridmanager.core.cubrid.table.model.DataType;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaChangeManager;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaDDL;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemeChangeLog;
import com.cubrid.cubridmanager.core.cubrid.table.model.SuperClassUtil;
import com.cubrid.cubridmanager.core.cubrid.table.model.SystemNamingUtil;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemeChangeLog.SchemeInnerType;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.AttributeCategory;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ConstraintType;
import com.cubrid.cubridmanager.core.cubrid.table.task.attribute.UpdateAttributeTask;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfoList;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.cubrid.table.control.AttributeTableViewerContentProvider;
import com.cubrid.cubridmanager.ui.cubrid.table.control.AttributeTableViewerLabelProvider;
import com.cubrid.cubridmanager.ui.cubrid.table.control.FKTableViewerContentProvider;
import com.cubrid.cubridmanager.ui.cubrid.table.control.FKTableViewerLabelProvider;
import com.cubrid.cubridmanager.ui.cubrid.table.control.IndexTableViewerContentProvider;
import com.cubrid.cubridmanager.ui.cubrid.table.control.IndexTableViewerLabelProvider;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.SWTResourceManager;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;

public class CreateTableDialog extends
		CMTitleAreaDialog {
	private Table indexTable;
	private Table fkTable;
	private Table conflictsTable;
	private TableColumn classColumn_1;
	private TableColumn inheritColumn_1;
	private TableColumn sharedColumn_1;
	private TableViewer tvSuperClass;
	private Table columnsTableSuperClass;
	private Combo ownerCombo;
	// private static final int Width_defaultColumn = 100;
	private static final int Width_uniqueColumn = 70;
	private static final int Width_notNullColumn = 70;
	private static final int Width_dataTypeColumn = 120;
	private static final int Width_nameColumn = 90;
	private static final int Width_pkColumn = 30;
	private static final int Width_sharedColumn = 70;
	private StyledText sqlText;
	private Button upBTN;
	private Table columnsTable;
	private Text schemaTypeText;
	private Text tableTypeText;
	private Text tableText;
	private CubridDatabase database;
	private Composite composite;

	private TableColumn sharedColumn;
	private TableColumn inheritColumn;
	private TableColumn classColumn;
	private Button inheritanceBTN;
	private TableViewer columnTableView;
	private TabItem inheritanceTabItem;
	private Composite inheritContainer;
	private AttributeTableViewerContentProvider attrContentProvider;
	private AttributeTableViewerLabelProvider attrLabelProvider;
	private FKTableViewerContentProvider fkContentProvider;
	private FKTableViewerLabelProvider fkLabelProvider;
	private IndexTableViewerContentProvider indexContentProvider;
	private IndexTableViewerLabelProvider indexLabelProvider;
	boolean isNewTableFlag;
	public SchemaInfo oldSchema = null;
	private SchemaInfo newSchema;
	SchemaChangeManager changeList = null;
	private SchemaDDL ddl;
	private Table superclassTable;
	private Table resolutinTable;
	private Button addResolutinBTN;
	private TableViewer fkTableView;
	private TableViewer indexTableView;
	Color white = SWTResourceManager.getColor(SWT.COLOR_WHITE);

	public CreateTableDialog(Shell parentShell, CubridDatabase database,
			SchemaInfo schema) {
		super(parentShell);
		this.database = database;
		this.oldSchema = schema;
		newSchema = schema.clone();
		this.isNewTableFlag = false;
		changeList = new SchemaChangeManager(database.getDatabaseInfo(),
				isNewTableFlag);
		ddl = new SchemaDDL(changeList, database.getDatabaseInfo());
	}

	public CreateTableDialog(Shell parentShell, CubridDatabase database) {
		super(parentShell);
		this.database = database;
		newSchema = new SchemaInfo();
		newSchema.setClassname(""); //$NON-NLS-1$
		newSchema.setOwner(database.getUserName());
		newSchema.setDbname(database.getName());
		newSchema.setType(Messages.userSchema);
		newSchema.setVirtual(Messages.schemaTypeClass);
		this.isNewTableFlag = true;
		changeList = new SchemaChangeManager(database.getDatabaseInfo(),
				isNewTableFlag);
		ddl = new SchemaDDL(changeList, database.getDatabaseInfo());
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());

	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, Messages.btnOK, true);
		createButton(parent, IDialogConstants.CANCEL_ID, Messages.btnCancel,
				false);
	}

	public String getChangeOwnerDDL() {
		String tableName = tableText.getText();
		String oldOwner = null;
		if (isNewTableFlag) {
			oldOwner = database.getUserName();
		} else {
			oldOwner = oldSchema.getOwner();
		}
		String newOwner = newSchema.getOwner();
		if (!oldOwner.equalsIgnoreCase(newOwner)) {
			return ddl.changeOwner(tableName, newOwner);
		} else
			return ""; //$NON-NLS-1$
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			boolean valid = verifyTableName();
			if (valid) {
				boolean isdirty = false;
				String tableName = tableText.getText();
				newSchema.setClassname(tableName);

				String owner = ownerCombo.getText();
				newSchema.setOwner(owner);

				ddl.setEndLineChar("$$$$"); //$NON-NLS-1$
				String ddls = null;
				if (isNewTableFlag) {
					ddls = ddl.getDDL(newSchema);
				} else {
					ddls = ddl.getDDL(oldSchema, newSchema);
				}
				String[] sqls = ddls.split("\\$\\$\\$\\$"); //$NON-NLS-1$				

				CommonSQLExcuterTask task = new CommonSQLExcuterTask(
						database.getDatabaseInfo());
				for (String sql : sqls) {
					if (!sql.trim().equals("") && !sql.trim().startsWith("--")) {//$NON-NLS-1$
						task.addSqls(sql);
						isdirty = true;
					}
				}
				String changeOwnerDDL = getChangeOwnerDDL();
				if (!changeOwnerDDL.equals("")) { //$NON-NLS-1$
					task.addCallSqls(changeOwnerDDL);
					isdirty = true;
				}
				ddl.setEndLineChar(";"); //$NON-NLS-1$
				List<String> columns = ddl.getNotNullChangedColumn();
				if (columns.size() > 0) {
					isdirty = true;
				}
				if (isdirty) {
					CommonTaskExec taskExec = new CommonTaskExec();
					taskExec.addTask(task);
					for (String column : columns) {
						DBAttribute attr = newSchema.getDBAttributeByName(
								column, false);
						UpdateAttributeTask cmTask = new UpdateAttributeTask(
								database.getServer().getServerInfo());
						cmTask.setDbName(database.getDatabaseInfo().getDbName());
						cmTask.setClassName(newSchema.getClassname());
						cmTask.setOldAttributeName(column);
						cmTask.setNewAttributeName(column);
						cmTask.setCategory(AttributeCategory.INSTANCE);
						cmTask.setNotNull(attr.isNotNull());
						cmTask.setUnique(attr.isUnique());
						cmTask.setDefault(attr.getDefault());
						System.out.println(cmTask.getAppendSendMsg());
						System.out.println();
						taskExec.addTask(cmTask);
					}
					new ExecTaskWithProgress(taskExec).exec();

					if (task.getErrorMsg() != null) {
						return;
					} else {
						//						CommonTool.openInformationBox(Messages.infoSuccess,
						//								Messages.infoSuccess);
						database.getDatabaseInfo().clearSchemas();
					}
				}

			} else {
				return;
			}
			this.getShell().dispose();
			this.close();
		} else if (buttonId == IDialogConstants.CANCEL_ID) {
			this.getShell().dispose();
			this.close();
		}
		super.buttonPressed(buttonId);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp,
				CubridManagerHelpContextIDs.databaseTable);
		composite = new Composite(parentComp, SWT.NONE);
		final GridData gd_composite = new GridData(SWT.FILL, SWT.FILL, true,
				true);
		gd_composite.heightHint = 500;
		gd_composite.widthHint = 800;
		composite.setLayoutData(gd_composite);

		GridLayout layout = new GridLayout();
		layout.numColumns = 1;
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		layout.numColumns = 1;
		composite.setLayout(layout);

		final TabFolder tabFolder = new TabFolder(composite, SWT.NONE);
		tabFolder.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(final SelectionEvent e) {
				if (tabFolder.getSelection()[0].getText().equals(
						Messages.infoSQLScriptTab)) {
					String tableName = tableText.getText();
					newSchema.setClassname(tableName);
					String owner = ownerCombo.getText();

					newSchema.setOwner(owner);
					String sql = ""; //$NON-NLS-1$
					if (oldSchema != null) {
						sql = ddl.getDDL(oldSchema)
								+ CommonTool.getLineSeparator()
								+ CommonTool.getLineSeparator()
								+ CommonTool.getLineSeparator();
					}
					sql = sql + ddl.getDDL(oldSchema, newSchema);

					sql = sql + getChangeOwnerDDL();
					sqlText.setText(sql);
				}
			}
		});
		final GridData gd_tabFolder = new GridData(SWT.FILL, SWT.FILL, true,
				true);
		gd_tabFolder.heightHint = 469;
		gd_tabFolder.widthHint = 621;
		tabFolder.setLayoutData(gd_tabFolder);

		final TabItem generalTabItem = new TabItem(tabFolder, SWT.NONE);
		generalTabItem.setText(Messages.infoGeneralTab);

		final Composite composite_Genaral = new Composite(tabFolder, SWT.NONE);
		final GridLayout gridLayout = new GridLayout();
		composite_Genaral.setLayout(gridLayout);
		generalTabItem.setControl(composite_Genaral);

		final Group group = new Group(composite_Genaral, SWT.NONE);
		group.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
		final GridLayout gridLayout_1 = new GridLayout();
		gridLayout_1.numColumns = 2;
		group.setLayout(gridLayout_1);

		final Label tableNameLabel = new Label(group, SWT.SHADOW_IN);
		tableNameLabel.setData(Messages.dataNewKey, null);
		tableNameLabel.setText(Messages.lblTableName);

		tableText = new Text(group, SWT.BORDER);
		final GridData gd_tableText = new GridData(SWT.FILL, SWT.CENTER, false,
				false);
		gd_tableText.horizontalIndent = 30;
		tableText.setLayoutData(gd_tableText);
		tableText.addModifyListener(new ModifyListener() {
			public void modifyText(ModifyEvent e) {
				boolean valid = verifyTableName();
				if (valid) {
					String tableName = tableText.getText();
					newSchema.setClassname(tableName);
				}
			}
		});

		final Label ownerLabel = new Label(group, SWT.NONE);
		ownerLabel.setText(Messages.lblOwner);

		ownerCombo = new Combo(group, SWT.NONE | SWT.READ_ONLY);
		final GridData gd_combo = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		gd_combo.horizontalIndent = 30;
		ownerCombo.setLayoutData(gd_combo);
		ownerCombo.setVisibleItemCount(10);
		fillOwnerCombo();

		final Label typeLabel = new Label(group, SWT.NONE);
		typeLabel.setText(Messages.lblType);

		tableTypeText = new Text(group, SWT.BORDER);
		final GridData gd_tableTypeText = new GridData(SWT.FILL, SWT.CENTER,
				false, false);
		gd_tableTypeText.horizontalIndent = 30;
		tableTypeText.setLayoutData(gd_tableTypeText);

		final Label schemaTypeLabel = new Label(group, SWT.NONE);
		schemaTypeLabel.setText(Messages.lblSchemaType);

		schemaTypeText = new Text(group, SWT.BORDER);
		final GridData gd_schemaTypeText = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		gd_schemaTypeText.horizontalIndent = 30;
		schemaTypeText.setLayoutData(gd_schemaTypeText);

		inheritanceBTN = new Button(group, SWT.CHECK);
		inheritanceBTN.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(final SelectionEvent e) {
				if (inheritanceBTN.getSelection()) {
					sharedColumn.setWidth(Width_sharedColumn);
					sharedColumn.setResizable(true);

					inheritColumn.setWidth(90);
					inheritColumn.setResizable(true);

					classColumn.setWidth(90);
					classColumn.setResizable(true);

					inheritanceTabItem = new TabItem(tabFolder, SWT.NONE, 1);
					inheritanceTabItem.setText(Messages.infoInheritTab);
					inheritanceTabItem.setControl(inheritContainer);
				} else {
					sharedColumn.setWidth(0);
					sharedColumn.setResizable(false);

					inheritColumn.setWidth(0);
					inheritColumn.setResizable(false);

					classColumn.setWidth(0);
					classColumn.setResizable(false);

					inheritanceTabItem.dispose();
				}
				attrContentProvider.setShowClassAttribute(inheritanceBTN.getSelection());
				columnTableView.refresh();
			}
		});
		final GridData gd_inheritanceBTN = new GridData(SWT.LEFT, SWT.CENTER,
				false, false, 2, 1);
		inheritanceBTN.setLayoutData(gd_inheritanceBTN);
		inheritanceBTN.setText(Messages.btnShowInherit);

		final Label columnsLabel = new Label(composite_Genaral, SWT.NONE);
		columnsLabel.setText(Messages.lblColumn);

		columnTableView = new TableViewer(composite_Genaral, SWT.FULL_SELECTION
				| SWT.MULTI | SWT.BORDER);
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

		sharedColumn = new TableColumn(columnsTable, SWT.NONE);
		sharedColumn.setWidth(0);
		sharedColumn.setResizable(false);
		sharedColumn.setText(Messages.tblColumnShared);
		sharedColumn.setAlignment(SWT.CENTER);

		inheritColumn = new TableColumn(columnsTable, SWT.NONE);
		inheritColumn.setAlignment(SWT.CENTER);
		inheritColumn.setWidth(0);
		inheritColumn.setResizable(false);
		inheritColumn.setText(Messages.tblColumnInherit);

		classColumn = new TableColumn(columnsTable, SWT.NONE);
		classColumn.setWidth(0);
		classColumn.setResizable(false);
		classColumn.setText(Messages.tblColumnClass);
		classColumn.setAlignment(SWT.CENTER);

		TableColumn hiddenClassColumn = new TableColumn(columnsTable, SWT.NONE);
		hiddenClassColumn.setWidth(0);
		hiddenClassColumn.setResizable(false);
		hiddenClassColumn.setText(Messages.tblColumnClass);
		hiddenClassColumn.setAlignment(SWT.CENTER);

		attrContentProvider = new AttributeTableViewerContentProvider();
		attrLabelProvider = new AttributeTableViewerLabelProvider(
				database.getDatabaseInfo(), newSchema);

		columnTableView.setContentProvider(attrContentProvider);
		columnTableView.setLabelProvider(attrLabelProvider);

		columnsTable.setLinesVisible(true);
		columnsTable.setHeaderVisible(true);
		attrLabelProvider.setSchema(newSchema);
		columnTableView.setInput(newSchema);
		columnTableView.refresh();

		final Composite composite_2 = new Composite(composite_Genaral, SWT.NONE);
		composite_2.setLayoutData(new GridData(SWT.RIGHT, SWT.CENTER, false,
				false));
		final GridLayout gridLayout_2 = new GridLayout();
		gridLayout_2.marginRight = 20;
		gridLayout_2.numColumns = 6;
		composite_2.setLayout(gridLayout_2);

		final Button setPkButton = new Button(composite_2, SWT.NONE);
		setPkButton.setLayoutData(new GridData(70, SWT.DEFAULT));
		setPkButton.setText(Messages.btnPK);
		setPkButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				SetPKDialog dlg = new SetPKDialog(getShell(), database,
						newSchema);
				int ret = dlg.open();
				if (ret == SetPKDialog.OK) {
					Constraint oldPK = dlg.getOldPK();
					Constraint newPK = dlg.getNewPK();
					String op = dlg.getOperation();
					if (op.equals("ADD")) { //$NON-NLS-1$
						newSchema.addConstraint(newPK);
						firePKAdded(newSchema, newPK);
					} else if (op.equals("DEL")) { //$NON-NLS-1$
						newSchema.getConstraints().remove(oldPK);
						firePKRemoved(newSchema, oldPK);
					} else if (op.equals("MODIFY")) { //$NON-NLS-1$
						newSchema.getConstraints().remove(oldPK);
						firePKRemoved(newSchema, oldPK);
						newSchema.addConstraint(newPK);
						firePKAdded(newSchema, newPK);
					}
					attrLabelProvider.setSchema(newSchema);
					columnTableView.setInput(newSchema);
				}

			}

			private void firePKAdded(SchemaInfo newSchema, Constraint newPK) {
				List<String> attrList = newPK.getAttributes();
				if (attrList.size() == 1) {
					String attr = attrList.get(0);
					DBAttribute a = newSchema.getDBAttributeByName(attr, false);
					boolean changed = false;
					if (!a.isNotNull()) {
						a.setNotNull(true);
						changed = true;
					}
					if (!a.isUnique()) {
						a.setUnique(true);
						changed = true;
					}
					if (changed) {
						changeList.addSchemeChangeLog(new SchemeChangeLog(
								a.getName(), a.getName(),
								SchemeInnerType.TYPE_ATTRIBUTE));
					}
				} else {
					for (String attr : attrList) {
						DBAttribute a = newSchema.getDBAttributeByName(attr,
								false);
						boolean changed = false;
						if (!a.isNotNull()) {
							a.setNotNull(true);
							changed = true;
						}
						if (changed) {
							changeList.addSchemeChangeLog(new SchemeChangeLog(
									a.getName(), a.getName(),
									SchemeInnerType.TYPE_ATTRIBUTE));
						}
					}
				}

			}

			private void firePKRemoved(SchemaInfo newSchema, Constraint oldPK) {
				List<String> attrList = oldPK.getAttributes();
				if (attrList.size() == 1) {
					String attr = attrList.get(0);
					DBAttribute a = newSchema.getDBAttributeByName(attr, false);
					boolean changed = true;
					a.setNotNull(false);
					a.setUnique(false);
					if (changed) {
						changeList.addSchemeChangeLog(new SchemeChangeLog(
								a.getName(), a.getName(),
								SchemeInnerType.TYPE_ATTRIBUTE));
					}
				} else {
					for (String attr : attrList) {
						DBAttribute a = newSchema.getDBAttributeByName(attr,
								false);
						boolean changed = true;
						a.setNotNull(false);
						if (changed) {
							changeList.addSchemeChangeLog(new SchemeChangeLog(
									a.getName(), a.getName(),
									SchemeInnerType.TYPE_ATTRIBUTE));
						}
					}
				}

			}
		});

		upBTN = new Button(composite_2, SWT.NONE);
		upBTN.setText(Messages.btnUp);

		// upBTN.setImage(CubridManagerUIPlugin.getImage("image/QueryEditor/qe_previouspage.png"));

		final Button downBTN = new Button(composite_2, SWT.DOWN);
		downBTN.setText(Messages.btnDown);
		// downBTN.setImage(CubridManagerUIPlugin.getImage("image/QueryEditor/qe_nextpage.png"));
		upBTN.setEnabled(isNewTableFlag);
		downBTN.setEnabled(isNewTableFlag);
		upBTN.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				TableItem[] selection = columnsTable.getSelection();
				if (selection != null && selection.length >= 1) {
					String attrName = selection[0].getText(1);
					boolean isClassAttr = Boolean.parseBoolean(selection[0].getText(10));

					boolean isInstanceAttribute = !isClassAttr;
					List<DBAttribute> list = null;
					if (isInstanceAttribute) {
						list = newSchema.getAttributes();
					} else {
						list = newSchema.getClassAttributes();
					}
					int index = -1;
					for (int i = 0; i < list.size(); i++) {
						if (list.get(i).getName().equals(attrName)) {
							index = i;
						}
					}
					if (index == 0) {
						// do nothing
					} else {
						list.add(index - 1, list.remove(index));
						columnTableView.setInput(newSchema);
						columnTableView.refresh();
					}

				}
			}
		});
		downBTN.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				TableItem[] selection = columnsTable.getSelection();
				if (selection != null && selection.length >= 1) {
					String attrName = selection[0].getText(1);
					boolean isClassAttr = Boolean.parseBoolean(selection[0].getText(10));

					boolean isInstanceAttribute = !isClassAttr;
					List<DBAttribute> list = null;
					if (isInstanceAttribute) {
						list = newSchema.getAttributes();
					} else {
						list = newSchema.getClassAttributes();
					}
					int index = -1;
					for (int i = 0; i < list.size(); i++) {
						if (list.get(i).getName().equals(attrName)) {
							index = i;
						}
					}
					if (index == list.size() - 1) {
						// do nothing
					} else {
						list.add(index + 1, list.remove(index));
						columnTableView.setInput(newSchema);
						columnTableView.refresh();
					}

				}
			}
		});

		final Button addButton = new Button(composite_2, SWT.NONE);
		final GridData gd_addButton = new GridData(80, SWT.DEFAULT);
		gd_addButton.horizontalIndent = 10;
		addButton.setLayoutData(gd_addButton);
		addButton.setText(Messages.btnAdd);
		addButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				boolean showObjectConcept = inheritanceBTN.getSelection();
				AddAttributeDialog dlg = new AddAttributeDialog(getShell(),
						database, newSchema, showObjectConcept);
				int returnCode = dlg.open();
				if (returnCode == AddAttributeDialog.OK) {
					// whenever new table or edit table,the same action
					DBAttribute addAttribute = dlg.getEditAttribute();
					String tableName = newSchema.getClassname();
					if (addAttribute != null) {
						addAttribute.setInherit(tableName);
					} else {
						return;
					}
					String newAttrName = addAttribute.getName();
					boolean isClassAttribute = dlg.isClassAttribute();
					newSchema.removeDBAttributeByName(addAttribute.getName(),
							isClassAttribute);
					newSchema.addDBAttribute(addAttribute, isClassAttribute);
					if (addAttribute.isUnique()) {
						Constraint unique = new Constraint();
						unique.setType(ConstraintType.UNIQUE.getText());
						unique.addAttribute(newAttrName);
						unique.addRule(newAttrName + " ASC");
						unique.setName(SystemNamingUtil.getUniqueName(
								tableName, unique.getRules()));
						newSchema.addConstraint(unique);
						indexTableView.setInput(newSchema);
					}

					addNewAttrLog(newAttrName, isClassAttribute);

					attrLabelProvider.setSchema(newSchema);
					columnTableView.setInput(newSchema);
				}

			}
		});

		final Button editButton = new Button(composite_2, SWT.NONE);
		final GridData gd_editButton = new GridData(80, SWT.DEFAULT);
		gd_editButton.horizontalIndent = 10;
		editButton.setLayoutData(gd_editButton);
		editButton.setText(Messages.btnEdit);
		editButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				TableItem[] selection = columnsTable.getSelection();
				if (selection != null && selection.length >= 1) {
					String attrName = selection[0].getText(1);
					boolean isOldAttrClass = Boolean.parseBoolean(selection[0].getText(10));
					DBAttribute attr = newSchema.getDBAttributeByName(attrName,
							isOldAttrClass);
					if (!attr.getInherit().equals(newSchema.getClassname())) {
						CommonTool.openErrorBox(Messages.errColumnNotEdit);
						return;
					}

					boolean isEditAll = isNewTableFlag ? isNewTableFlag
							: changeList.isNewAdded(attrName, isOldAttrClass);
					boolean showObjectConcept = inheritanceBTN.getSelection();
					AddAttributeDialog dlg = new AddAttributeDialog(getShell(),
							database, newSchema, attrName, isOldAttrClass,
							isEditAll, showObjectConcept);
					int ret = dlg.open();
					if (ret == IDialogConstants.OK_ID) {
						DBAttribute editAttribute = dlg.getEditAttribute();
						DBAttribute oldAttribute = dlg.getOldAttribute();
						String tableName = newSchema.getClassname();
						if (editAttribute != null) {
							editAttribute.setInherit(tableName);
						} else {
							return;
						}
						boolean isNewAttrClass = dlg.isClassAttribute();
						String newAttrName = editAttribute.getName();

						List<SchemaInfo> superList = SuperClassUtil.getSuperClasses(
								database.getDatabaseInfo(), newSchema);
						if (isEditAll) {
							if (isOldAttrClass != isNewAttrClass) { // attribute
								// type
								// changed
								newSchema.removeDBAttributeByName(attrName,
										isOldAttrClass);
								addDropAttrLog(attrName, isOldAttrClass);

								newSchema.addDBAttribute(editAttribute,
										isNewAttrClass);
								addNewAttrLog(newAttrName, isNewAttrClass);
							} else {
								newSchema.replaceDBAttributeByName(
										oldAttribute, editAttribute,
										isNewAttrClass, superList);
								addEditAttrLog(attrName, newAttrName,
										isNewAttrClass);
							}
							if (!oldAttribute.isUnique()
									&& editAttribute.isUnique()) {
								Constraint unique = new Constraint();
								unique.setType(ConstraintType.UNIQUE.getText());

								unique.addAttribute(newAttrName);
								unique.addRule(newAttrName + " ASC");

								unique.setName(SystemNamingUtil.getUniqueName(
										tableName, unique.getRules()));

								newSchema.addConstraint(unique);
							} else if (oldAttribute.isUnique()
									&& !editAttribute.isUnique()) {
								newSchema.removeUniqueByAttrName(attrName);
							}
							indexTableView.setInput(newSchema);
						} else {
							newSchema.replaceDBAttributeByName(oldAttribute,
									editAttribute, isNewAttrClass, superList);
							addEditAttrLog(attrName, newAttrName,
									isNewAttrClass);
						}
						SuperClassUtil.fireSuperClassChanged(
								database.getDatabaseInfo(), oldSchema,
								newSchema, newSchema.getSuperClasses());
						attrLabelProvider.setSchema(newSchema);
						columnTableView.setInput(newSchema);
					}

				}
			}
		});

		final Button deleteButton = new Button(composite_2, SWT.NONE);
		final GridData gd_deleteButton = new GridData(80, SWT.DEFAULT);
		gd_deleteButton.horizontalIndent = 10;
		deleteButton.setLayoutData(gd_deleteButton);
		deleteButton.setText(Messages.btnDel);
		deleteButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				TableItem[] selection = columnsTable.getSelection();
				if (selection != null && selection.length >= 1) {
					String attrName = selection[0].getText(1);
					boolean isClassAttr = Boolean.parseBoolean(selection[0].getText(10));

					DBAttribute oldAttribute = newSchema.getDBAttributeByName(
							attrName, isClassAttr);
					if (!oldAttribute.getInherit().equals(
							newSchema.getClassname())) {
						CommonTool.openErrorBox(Messages.errColumnNotDrop);
						return;
					}

					if (!isClassAttr) {
						newSchema.getAttributes().remove(oldAttribute);
						newSchema.removeUniqueByAttrName(attrName);
						indexTableView.setInput(newSchema);
					} else {
						newSchema.getClassAttributes().remove(oldAttribute);
					}
					SuperClassUtil.fireSuperClassChanged(
							database.getDatabaseInfo(), oldSchema, newSchema,
							newSchema.getSuperClasses());
					String oldAttrName = oldAttribute.getName();

					addDropAttrLog(oldAttrName, isClassAttr);

					attrLabelProvider.setSchema(newSchema);
					columnTableView.setInput(newSchema);

				}
			}
		});

		// inheritanceTabItem = new TabItem(tabFolder, SWT.NONE);
		// inheritanceTabItem.setText("Inheritance");

		inheritContainer = new Composite(tabFolder, SWT.NONE);
		final GridLayout gridLayout_3 = new GridLayout();
		gridLayout_3.makeColumnsEqualWidth = true;
		gridLayout_3.marginTop = 5;
		gridLayout_3.marginRight = 5;
		gridLayout_3.marginLeft = 5;
		gridLayout_3.numColumns = 2;
		inheritContainer.setLayout(gridLayout_3);
		// inheritanceTabItem.setControl(inheritContainer);

		createSupperClassTable();

		createResolutinTable();

		final TabItem foreignKeyTabItem = new TabItem(tabFolder, SWT.NONE);
		foreignKeyTabItem.setText(Messages.infoIndexesTab);

		final Composite composite_1 = new Composite(tabFolder, SWT.NONE);
		final GridLayout gridLayout_4 = new GridLayout();
		composite_1.setLayout(gridLayout_4);
		foreignKeyTabItem.setControl(composite_1);

		final Label label = new Label(composite_1, SWT.NONE);
		label.setText(Messages.lblFK);

		fkTableView = new TableViewer(composite_1, SWT.FULL_SELECTION
				| SWT.MULTI | SWT.BORDER);
		fkTableView.setColumnViewerEditor(null);
		fkTable = fkTableView.getTable();

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
		fkLabelProvider = new FKTableViewerLabelProvider(newSchema,
				database.getDatabaseInfo());

		fkTableView.setContentProvider(fkContentProvider);
		fkTableView.setLabelProvider(fkLabelProvider);
		fkTableView.setInput(newSchema);

		final Composite fkActionComposite = new Composite(composite_1, SWT.NONE);
		final GridData gd_fkActionComposite = new GridData(SWT.RIGHT,
				SWT.CENTER, false, false);
		fkActionComposite.setLayoutData(gd_fkActionComposite);
		final GridLayout gridLayout_5 = new GridLayout();
		gridLayout_5.numColumns = 3;
		gridLayout_5.marginRight = 20;
		fkActionComposite.setLayout(gridLayout_5);

		final Button addFKBTN = new Button(fkActionComposite, SWT.NONE);
		final GridData gd_addFKBTN = new GridData(80, SWT.DEFAULT);
		gd_addFKBTN.horizontalIndent = 10;
		addFKBTN.setLayoutData(gd_addFKBTN);
		addFKBTN.setText(Messages.btnAdd);
		addFKBTN.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				AddFKDialog dlg = new AddFKDialog(getShell(), database,
						newSchema);
				int returnCode = dlg.open();
				if (returnCode == AddFKDialog.OK) {
					Constraint fk = dlg.getRetFK();
					newSchema.addConstraint(fk);
					changeList.addSchemeChangeLog(new SchemeChangeLog(null,
							fk.getName(), SchemeInnerType.TYPE_FK));
					fkTableView.setInput(newSchema);
					fkTableView.refresh();
				}
			}
		});

		final Button deleteFKBTN = new Button(fkActionComposite, SWT.NONE);
		final GridData gd_deleteFKBTN = new GridData(80, SWT.DEFAULT);
		gd_deleteFKBTN.horizontalIndent = 10;
		deleteFKBTN.setLayoutData(gd_deleteFKBTN);
		deleteFKBTN.setText(Messages.btnDelete);

		deleteFKBTN.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				TableItem[] selection = fkTable.getSelection();
				if (selection != null && selection.length >= 1) {
					String fkName = selection[0].getText(0);

					List<SchemaInfo> superList = SuperClassUtil.getSuperClasses(
							database.getDatabaseInfo(),
							newSchema.getSuperClasses());
					if (newSchema.isInSuperClasses(superList, fkName)) {
						CommonTool.openErrorBox(Messages.errFKNotDrop);
						return;
					}

					newSchema.removeFKConstraint(fkName);
					changeList.addSchemeChangeLog(new SchemeChangeLog(fkName,
							null, SchemeInnerType.TYPE_FK));
					fkTableView.setInput(newSchema);
				}
			}
		});

		final Label labe2 = new Label(composite_1, SWT.NONE);
		labe2.setText(Messages.lblIndexes);

		indexTableView = new TableViewer(composite_1, SWT.FULL_SELECTION
				| SWT.MULTI | SWT.BORDER);
		indexTableView.setColumnViewerEditor(null);
		indexTable = indexTableView.getTable();

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

		final Composite indexActionComposite = new Composite(composite_1,
				SWT.NONE);
		final GridData gd_indexActionComposite = new GridData(SWT.RIGHT,
				SWT.CENTER, false, false);
		indexActionComposite.setLayoutData(gd_indexActionComposite);
		final GridLayout gridLayout_6 = new GridLayout();
		gridLayout_6.numColumns = 3;
		gridLayout_6.marginRight = 20;
		indexActionComposite.setLayout(gridLayout_6);

		final Button addIndexBTN = new Button(indexActionComposite, SWT.NONE);
		final GridData gd_addIndexBTN = new GridData(80, SWT.DEFAULT);
		gd_addIndexBTN.horizontalIndent = 10;
		addIndexBTN.setLayoutData(gd_addIndexBTN);
		addIndexBTN.setText(Messages.btnAdd);

		addIndexBTN.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				AddIndexDialog dlg = new AddIndexDialog(getShell(), newSchema);
				int returnCode = dlg.open();
				if (returnCode == AddIndexDialog.OK) {
					Constraint constraint = dlg.getRetConstraint();
					newSchema.addConstraint(constraint);
					changeList.addSchemeChangeLog(new SchemeChangeLog(null,
							constraint.getDefaultName(newSchema.getClassname())
									+ "$" + constraint.getName(), //$NON-NLS-1$
							SchemeInnerType.TYPE_INDEX));
					indexTableView.setInput(newSchema);
				}
			}
		});

		final Button deleteIndexBTN = new Button(indexActionComposite, SWT.NONE);
		final GridData gd_deleteIndexBTN = new GridData(80, SWT.DEFAULT);
		gd_deleteIndexBTN.horizontalIndent = 10;
		deleteIndexBTN.setLayoutData(gd_deleteIndexBTN);
		deleteIndexBTN.setText(Messages.btnDelete); //$NON-NLS-1$

		deleteIndexBTN.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				TableItem[] selection = indexTable.getSelection();
				if (selection != null && selection.length >= 1) {
					String indexName = selection[0].getText(0);
					String indexType = selection[0].getText(1);
					Constraint index = newSchema.getConstraintByName(indexName,
							indexType);
					List<SchemaInfo> superList = SuperClassUtil.getSuperClasses(
							database.getDatabaseInfo(),
							newSchema.getSuperClasses());
					if (newSchema.isInSuperClasses(superList, indexName)) {
						CommonTool.openErrorBox(Messages.errIndexNotDrop);
						return;
					}
					newSchema.removeConstraintByName(indexName, indexType);
					changeList.addSchemeChangeLog(new SchemeChangeLog(
							index.getDefaultName(newSchema.getClassname())
									+ "$" + index.getName(), null, //$NON-NLS-1$
							SchemeInnerType.TYPE_INDEX));
					indexTableView.setInput(newSchema);
				}
			}
		});

		final TabItem ddlTabItem = new TabItem(tabFolder, SWT.NONE);
		ddlTabItem.setText(Messages.infoSQLScriptTab);

		final Composite composite_4 = new Composite(tabFolder, SWT.NONE);
		composite_4.setLayout(new GridLayout());
		ddlTabItem.setControl(composite_4);

		sqlText = new StyledText(composite_4, SWT.WRAP | SWT.V_SCROLL
				| SWT.READ_ONLY | SWT.H_SCROLL | SWT.BORDER);
		CommonTool.registerContextMenu(sqlText, false);
		sqlText.setBackground(white);
		final GridData gd_sqlText = new GridData(SWT.FILL, SWT.FILL, true, true);
		gd_sqlText.heightHint = 89;
		sqlText.setLayoutData(gd_sqlText);

		init();
		return parent;
	}

	private void init() {
		String owner = null;
		if (oldSchema != null) {
			tableText.setText(oldSchema.getClassname());
			tableText.setEnabled(false);
			if (oldSchema.getType().equals("user")) { //$NON-NLS-1$
				schemaTypeText.setText(Messages.userSchema);
				schemaTypeText.setEnabled(false);
			} else {
				schemaTypeText.setText(Messages.systemSchema);
				schemaTypeText.setEnabled(false);
			}
			owner = oldSchema.getOwner();
			setTitle(Messages.bind(Messages.editTableMsgTitle,
					oldSchema.getClassname()));
			setMessage(Messages.editTableMsg);
			// String[] strs = new String[] { oldSchema.getClassname(),
			// database.getName(), database.getServer().getName() };
			String title = Messages.bind(Messages.editTableShellTitle,
					oldSchema.getClassname());
			setSuperClassInfo();
			getShell().setText(title);
		} else {
			tableText.setText(""); //$NON-NLS-1$
			schemaTypeText.setText(Messages.userSchema);
			schemaTypeText.setEnabled(false);
			owner = database.getUserName();
			setTitle(Messages.newTableMsgTitle);
			setMessage(Messages.newTableMsg);
			// String title = Messages.bind(Messages.newTableShellTitle,
			// database.getName(), database.getServer().getName());
			getShell().setText(Messages.newTableShellTitle);
		}
		for (int i = 0; i < ownerCombo.getItemCount(); i++) {
			if (ownerCombo.getItem(i).equalsIgnoreCase(owner)) {
				ownerCombo.select(i);
				break;
			}
		}
		tableTypeText.setText("table"); //$NON-NLS-1$
		tableTypeText.setEnabled(false);
	}

	private void fillOwnerCombo() {
		CommonQueryTask<DbUserInfoList> userTask = new CommonQueryTask<DbUserInfoList>(
				database.getServer().getServerInfo(),
				CommonSendMsg.commonDatabaseSendMsg, new DbUserInfoList());
		userTask.setDbName(database.getName());
		userTask.execute();

		List<DbUserInfo> userinfo = userTask.getResultModel().getUserList();
		if (userinfo == null) {
			return;
		}
		for (DbUserInfo ui : userinfo) {
			String userName = ui.getName();
			ownerCombo.add(userName.toUpperCase());
		}

	}

	private void createSupperClassTable() {
		final Label label_2 = new Label(inheritContainer, SWT.NONE);
		label_2.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, false, false));
		label_2.setText(Messages.lblSuperList);

		GridData gridData3 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.FILL, true, true);
		gridData3.widthHint = 150;
		gridData3.heightHint = 150;

		final Label superClassInformationLabel = new Label(inheritContainer,
				SWT.NONE);
		final GridData gd_superClassInformationLabel = new GridData();
		gd_superClassInformationLabel.horizontalIndent = 20;
		superClassInformationLabel.setLayoutData(gd_superClassInformationLabel);
		superClassInformationLabel.setText(Messages.lblSuperInfo);

		superclassTable = new Table(inheritContainer, SWT.FULL_SELECTION
				| SWT.SINGLE | SWT.BORDER);
		superclassTable.setLinesVisible(true);
		superclassTable.setLayoutData(gridData3);
		superclassTable.setHeaderVisible(false);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(100, 30, true));
		superclassTable.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(superclassTable, SWT.LEFT);
		tblcol.setText("col1"); //$NON-NLS-1$

		tvSuperClass = new TableViewer(inheritContainer, SWT.HIDE_SELECTION
				| SWT.BORDER);
		columnsTableSuperClass = tvSuperClass.getTable();
		columnsTableSuperClass.setLinesVisible(true);
		columnsTableSuperClass.setHeaderVisible(true);
		final GridData gd_columnsTable_1 = new GridData(SWT.FILL, SWT.FILL,
				true, true);
		gd_columnsTable_1.horizontalIndent = 15;
		gd_columnsTable_1.widthHint = 438;
		gd_columnsTable_1.heightHint = 150;
		columnsTableSuperClass.setLayoutData(gd_columnsTable_1);

		final TableColumn pkColumn_1 = new TableColumn(columnsTableSuperClass,
				SWT.NONE);
		pkColumn_1.setAlignment(SWT.CENTER);
		pkColumn_1.setWidth(30);
		pkColumn_1.setText(Messages.tblColumnPK);

		final TableColumn nameColumn_1 = new TableColumn(
				columnsTableSuperClass, SWT.NONE);
		nameColumn_1.setWidth(90);
		nameColumn_1.setText(Messages.tblColumnName);

		final TableColumn dataTypeColumn_1 = new TableColumn(
				columnsTableSuperClass, SWT.NONE);
		dataTypeColumn_1.setWidth(120);
		dataTypeColumn_1.setText(Messages.tblColumnDataType);

		final TableColumn newColumnTableColumn_1 = new TableColumn(
				columnsTableSuperClass, SWT.NONE);
		newColumnTableColumn_1.setAlignment(SWT.CENTER);
		newColumnTableColumn_1.setWidth(100);
		newColumnTableColumn_1.setText(Messages.tblColumnAutoIncr);

		final TableColumn defaultColumn_1 = new TableColumn(
				columnsTableSuperClass, SWT.NONE);
		defaultColumn_1.setWidth(98);
		defaultColumn_1.setText(Messages.tblColumnDefault);

		final TableColumn notNullColumn_1 = new TableColumn(
				columnsTableSuperClass, SWT.CENTER);
		notNullColumn_1.setAlignment(SWT.CENTER);
		notNullColumn_1.setWidth(70);
		notNullColumn_1.setText(Messages.tblColumnNotNull);

		final TableColumn uniqueColumn_1 = new TableColumn(
				columnsTableSuperClass, SWT.CENTER);
		uniqueColumn_1.setAlignment(SWT.CENTER);
		uniqueColumn_1.setWidth(70);
		uniqueColumn_1.setText(Messages.tblColumnUnique);

		sharedColumn_1 = new TableColumn(columnsTableSuperClass, SWT.CENTER);
		sharedColumn_1.setResizable(false);
		sharedColumn_1.setAlignment(SWT.CENTER);
		sharedColumn_1.setWidth(100);
		sharedColumn_1.setText(Messages.tblColumnShared);

		inheritColumn_1 = new TableColumn(columnsTableSuperClass, SWT.NONE);
		inheritColumn_1.setResizable(false);
		inheritColumn_1.setWidth(100);
		inheritColumn_1.setText(Messages.tblColumnInherit);

		classColumn_1 = new TableColumn(columnsTableSuperClass, SWT.CENTER);
		classColumn_1.setResizable(false);
		classColumn_1.setAlignment(SWT.CENTER);
		classColumn_1.setWidth(100);
		classColumn_1.setText(Messages.tblColumnClass);

		final Composite composite_3 = new Composite(inheritContainer, SWT.NONE);
		composite_3.setLayoutData(new GridData(SWT.CENTER, SWT.CENTER, true,
				false, 2, 1));
		final GridLayout gridLayout_3 = new GridLayout();
		gridLayout_3.numColumns = 2;
		composite_3.setLayout(gridLayout_3);
		superclassTable.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				TableItem[] items = superclassTable.getSelection();
				if (items != null) {
					String superTable = items[0].getText(0);
					SchemaInfo superSchema = database.getDatabaseInfo().getSchemaInfo(
							superTable);
					AttributeTableViewerContentProvider cp = new AttributeTableViewerContentProvider();
					AttributeTableViewerLabelProvider lp = new AttributeTableViewerLabelProvider(
							database.getDatabaseInfo(), superSchema);
					tvSuperClass.setContentProvider(cp);
					tvSuperClass.setLabelProvider(lp);
					tvSuperClass.setInput(superSchema);
				}
			}
		});

		final Button addSuperClassBTN = new Button(composite_3, SWT.NONE);
		final GridData gd_addSuperClassBTN = new GridData(SWT.FILL, SWT.CENTER,
				false, false);
		gd_addSuperClassBTN.widthHint = 80;
		addSuperClassBTN.setLayoutData(gd_addSuperClassBTN);
		addSuperClassBTN.setText(Messages.btnAdd);
		addSuperClassBTN.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				AddSuperDialog dlg = new AddSuperDialog(getShell(), database,
						newSchema);
				int ret = dlg.open();
				if (ret == AddSuperDialog.OK) {
					List<String> newSuperclass = dlg.getNewSuperclass();
					if (newSuperclass.equals(newSchema.getSuperClasses())) {
						return;
					} else if (!isNewTableFlag
							&& newSuperclass.contains(oldSchema.getClassname())) {
						String msg = Messages.bind(Messages.errInheritItself,
								oldSchema.getClassname());
						CommonTool.openErrorBox(msg);
						return;
					}

					boolean success = SuperClassUtil.fireSuperClassChanged(
							database.getDatabaseInfo(), oldSchema, newSchema,
							newSuperclass);
					if (success) {
						newSchema.setSuperClasses(newSuperclass);
						setSuperClassInfo();

						attrLabelProvider.setSchema(newSchema);
						columnTableView.setInput(newSchema);

						tvSuperClass.setInput(null);
						fkTableView.setInput(newSchema);
						indexTableView.setInput(newSchema);
					} else {
						CommonTool.openErrorBox(Messages.errDataTypeImcompatible);
					}
				}

			}
		});

		final Button deleteSuperClassBTN = new Button(composite_3, SWT.NONE);
		final GridData gd_deleteSuperClassBTN = new GridData(SWT.FILL,
				SWT.CENTER, false, false);
		gd_deleteSuperClassBTN.horizontalIndent = 20;
		gd_deleteSuperClassBTN.widthHint = 80;
		deleteSuperClassBTN.setLayoutData(gd_deleteSuperClassBTN);
		deleteSuperClassBTN.setText(Messages.btnDelete);
		deleteSuperClassBTN.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				TableItem[] items = superclassTable.getSelection();
				if (items != null) {
					String superTable = items[0].getText(0);
					tvSuperClass.setInput(null);
					List<String> newSuperclass = new ArrayList<String>();
					newSuperclass.addAll(newSchema.getSuperClasses());
					newSuperclass.remove(superTable);
					boolean success = SuperClassUtil.fireSuperClassChanged(
							database.getDatabaseInfo(), oldSchema, newSchema,
							newSuperclass);
					if (success) {
						newSchema.setSuperClasses(newSuperclass);
						setSuperClassInfo();

						attrLabelProvider.setSchema(newSchema);
						columnTableView.setInput(newSchema);
						tvSuperClass.setInput(null);
						fkTableView.setInput(newSchema);
						indexTableView.setInput(newSchema);
					} else {
						CommonTool.openErrorBox(Messages.errDataTypeImcompatible);
					}
				}
			}
		});
	}

	private void createResolutinTable() {
		final Label resolutionLabel = new Label(inheritContainer, SWT.NONE);
		resolutionLabel.setLayoutData(new GridData());
		resolutionLabel.setText(Messages.lblResolution);

		GridData gridData7 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.FILL, true, true);
		gridData7.heightHint = 130;
		gridData7.widthHint = 359;

		final Label sameColumnNamesLabel = new Label(inheritContainer, SWT.NONE);
		final GridData gd_sameColumnNamesLabel = new GridData();
		gd_sameColumnNamesLabel.horizontalIndent = 15;
		sameColumnNamesLabel.setLayoutData(gd_sameColumnNamesLabel);
		sameColumnNamesLabel.setText(Messages.lblConflicts);
		resolutinTable = new Table(inheritContainer, SWT.FULL_SELECTION
				| SWT.SINGLE | SWT.BORDER);
		resolutinTable.setLinesVisible(true);
		resolutinTable.setLayoutData(gridData7);
		resolutinTable.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(25, 50, true));
		tlayout.addColumnData(new ColumnWeightData(25, 50, true));
		tlayout.addColumnData(new ColumnWeightData(25, 50, true));
		tlayout.addColumnData(new ColumnWeightData(25, 50, true));
		resolutinTable.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(resolutinTable, SWT.LEFT);
		tblcol.setText(Messages.tblColumnType);
		tblcol = new TableColumn(resolutinTable, SWT.LEFT);
		tblcol.setText(Messages.tblColumnSuperTable);
		tblcol = new TableColumn(resolutinTable, SWT.LEFT);
		tblcol.setText(Messages.tblColumnColumns);
		tblcol = new TableColumn(resolutinTable, SWT.LEFT);
		tblcol.setText(Messages.tblColumnAlias);

		conflictsTable = new Table(inheritContainer, SWT.BORDER);
		conflictsTable.setLinesVisible(true);
		conflictsTable.setHeaderVisible(true);
		final GridData gd_sameNameColumnTable = new GridData(SWT.FILL,
				SWT.FILL, true, true);
		gd_sameNameColumnTable.horizontalIndent = 15;
		conflictsTable.setLayoutData(gd_sameNameColumnTable);

		final TableColumn newColumnTableColumn0 = new TableColumn(
				conflictsTable, SWT.NONE);
		newColumnTableColumn0.setWidth(50);
		newColumnTableColumn0.setText(Messages.tblColumnType);

		final TableColumn newColumnTableColumn = new TableColumn(
				conflictsTable, SWT.NONE);
		newColumnTableColumn.setWidth(100);
		newColumnTableColumn.setText(Messages.tblcolColumnName); //$NON-NLS-1$

		final TableColumn newColumnTableColumn_1 = new TableColumn(
				conflictsTable, SWT.NONE);
		newColumnTableColumn_1.setWidth(100);
		newColumnTableColumn_1.setText(Messages.tblcolDataType); //$NON-NLS-1$

		final TableColumn newColumnTableColumn_2 = new TableColumn(
				conflictsTable, SWT.NONE);
		newColumnTableColumn_2.setWidth(100);
		newColumnTableColumn_2.setText(Messages.tblColumnTableName);

		final Composite composite_3_1 = new Composite(inheritContainer,
				SWT.NONE);
		composite_3_1.setLayoutData(new GridData(SWT.CENTER, SWT.CENTER, true,
				false, 2, 1));
		final GridLayout gridLayout_4 = new GridLayout();
		gridLayout_4.numColumns = 2;
		composite_3_1.setLayout(gridLayout_4);

		addResolutinBTN = new Button(composite_3_1, SWT.NONE);
		final GridData gd_addResolutinBTN = new GridData(SWT.FILL, SWT.CENTER,
				false, false);
		gd_addResolutinBTN.widthHint = 80;
		addResolutinBTN.setLayoutData(gd_addResolutinBTN);
		addResolutinBTN.setText(Messages.btnAdd);

		addResolutinBTN.addSelectionListener(new SelectionAdapter() {

			public void widgetSelected(SelectionEvent e) {
				List<String[]> columnConflicts = SuperClassUtil.getColumnConflicts(
						database.getDatabaseInfo(), newSchema,
						newSchema.getSuperClasses(), true);
				String[][] classConflicts = columnConflicts.toArray(new String[columnConflicts.size()][]);

				columnConflicts = SuperClassUtil.getColumnConflicts(
						database.getDatabaseInfo(), newSchema,
						newSchema.getSuperClasses(), false);
				String[][] conflicts = columnConflicts.toArray(new String[columnConflicts.size()][]);
				AddResolutionDialog dlg = new AddResolutionDialog(getShell(),
						conflicts, classConflicts, newSchema);
				int ret = dlg.open();
				if (ret == AddResolutionDialog.OK) {
					DBResolution newResolution = dlg.getResolution();
					assert (newResolution != null);
					String tbl = newResolution.getClassName();
					String column = newResolution.getName();
					String alias = newResolution.getAlias();
					if (newResolution != null) {
						boolean isClassType = dlg.isClassResolution();
						List<DBResolution> resolutions = null;
						if (isClassType) {
							resolutions = newSchema.getClassResolutions();
						} else {
							resolutions = newSchema.getResolutions();
						}
						if (alias.equals("")) {
							for (int i = resolutions.size() - 1; i >= 0; i--) {
								DBResolution r = resolutions.get(i);
								// remove resolution
								if (r.getName().equals(column)) {
									if (r.getAlias().equals("")) { //$NON-NLS-1$
										resolutions.remove(i);
									}
								}
							}
						} else {
							for (int i = resolutions.size() - 1; i >= 0; i--) {
								DBResolution r = resolutions.get(i);
								// remove resolution
								if (r.getName().equals(column)
										&& r.getClassName().equals(tbl)) {
									DBResolution removedResolution = resolutions.remove(i);
									DBResolution addedResolution = SuperClassUtil.getNextResolution(
											resolutions, removedResolution,
											columnConflicts);
									resolutions.add(addedResolution);
								}
							}
						}
						if (isClassType) {
							newSchema.addClassResolution(newResolution);
							SuperClassUtil.fireResolutionChanged(
									database.getDatabaseInfo(), oldSchema,
									newSchema, true);
						} else {
							newSchema.addResolution(newResolution);
							SuperClassUtil.fireResolutionChanged(
									database.getDatabaseInfo(), oldSchema,
									newSchema, false);
						}
					}

					setSuperClassInfo();
					attrLabelProvider.setSchema(newSchema);
					columnTableView.setInput(newSchema);

					tvSuperClass.setInput(null);
				}

			}
		});

		final Button delteResolutionBTN = new Button(composite_3_1, SWT.NONE);
		final GridData gd_delteResolutionBTN = new GridData(SWT.FILL,
				SWT.CENTER, false, false);
		gd_delteResolutionBTN.horizontalIndent = 20;
		gd_delteResolutionBTN.widthHint = 80;
		delteResolutionBTN.setLayoutData(gd_delteResolutionBTN);
		delteResolutionBTN.setText(Messages.btnDelete);
		delteResolutionBTN.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				TableItem[] items = resolutinTable.getSelection();
				if (items != null) {
					String type = items[0].getText(0);
					String superTable = items[0].getText(1);
					String column = items[0].getText(2);
					List<DBResolution> resolutions = null;
					DBResolution removedResolution = null;
					if (type.equals("Class")) { //$NON-NLS-1$
						resolutions = newSchema.getClassResolutions();
					} else {
						resolutions = newSchema.getResolutions();
					}
					for (int i = 0; i < resolutions.size(); i++) {
						DBResolution r = resolutions.get(i);
						if (r.getName().equals(column)
								&& r.getClassName().equals(superTable)) {
							removedResolution = resolutions.remove(i);
						}
					}
					assert (removedResolution != null);
					if ("".equals(removedResolution.getAlias())) {
						List<String[]> columnConflicts = null;
						boolean isClassType;
						if (type.equals("Class")) { //$NON-NLS-1$
							isClassType = true;
						} else {
							isClassType = false;
						}
						columnConflicts = SuperClassUtil.getColumnConflicts(
								database.getDatabaseInfo(), newSchema,
								newSchema.getSuperClasses(), isClassType);
						DBResolution nextResolution = SuperClassUtil.getNextResolution(
								resolutions, removedResolution, columnConflicts);
						assert (nextResolution != null);
						newSchema.addResolution(nextResolution, isClassType);

						SuperClassUtil.fireResolutionChanged(
								database.getDatabaseInfo(), oldSchema,
								newSchema, isClassType);
					} else {

					}

				}
				setSuperClassInfo();

				attrLabelProvider.setSchema(newSchema);
				columnTableView.setInput(newSchema);

				tvSuperClass.setInput(null);

			}
		});

	}

	private void setSuperClassInfo() {
		superclassTable.setToolTipText(Messages.tipSuperClassTable);
		resolutinTable.setToolTipText(Messages.tipResolutionTable);

		superclassTable.removeAll();
		List<String> superClasses = newSchema.getSuperClasses();
		for (int i = 0, n = superClasses.size(); i < n; i++) {
			String rec = superClasses.get(i);
			TableItem item = new TableItem(superclassTable, SWT.NONE);
			item.setText(0, rec);
		}

		resolutinTable.removeAll();
		List<DBResolution> resolutions = newSchema.getClassResolutions();
		for (int i = 0, n = resolutions.size(); i < n; i++) {
			DBResolution rec = resolutions.get(i);
			TableItem item = new TableItem(resolutinTable, SWT.NONE);
			item.setText(0, "Class"); //$NON-NLS-1$
			item.setText(1, rec.getClassName());
			item.setText(2, rec.getName());
			item.setText(3, rec.getAlias());
		}
		resolutions = newSchema.getResolutions();
		for (int i = 0, n = resolutions.size(); i < n; i++) {
			DBResolution rec = resolutions.get(i);
			TableItem item = new TableItem(resolutinTable, SWT.NONE);
			item.setText(0, ""); //$NON-NLS-1$
			item.setText(1, rec.getClassName());
			item.setText(2, rec.getName());
			item.setText(3, rec.getAlias());
		}
		showColumnConflicts();
	}

	private void showColumnConflicts() {
		conflictsTable.removeAll();

		List<String> superClasses = newSchema.getSuperClasses();
		List<String[]> list = SuperClassUtil.getColumnConflicts(
				database.getDatabaseInfo(), newSchema, superClasses, true);
		for (String[] strs : list) {
			TableItem item = new TableItem(conflictsTable, SWT.NONE);
			item.setText(0, "Class"); //$NON-NLS-1$
			item.setText(1, strs[0]);
			item.setText(2, strs[1]);
			item.setText(3, strs[2]);
		}
		list.clear();
		list.addAll(SuperClassUtil.getColumnConflicts(
				database.getDatabaseInfo(), newSchema, superClasses, false));

		for (String[] strs : list) {
			TableItem item = new TableItem(conflictsTable, SWT.NONE);
			item.setText(0, ""); //$NON-NLS-1$
			item.setText(1, strs[0]);
			item.setText(2, DataType.getShownType(strs[1]));
			item.setText(3, strs[2]);
		}
		if (list.size() > 0) {
			addResolutinBTN.setEnabled(true);
		} else {
			addResolutinBTN.setEnabled(false);
		}
	}

	/**
	 * verify whether current table name is valid
	 * 
	 */
	private boolean verifyTableName() {
		setErrorMessage(null);
		String tableName = tableText.getText();
		if (tableName.equals("")) {
			setErrorMessage(Messages.errNoTableName);
			return false;
		} else {
			String retstr = CommonTool.validateCheckInIdentifier(tableName);
			if (retstr.length() > 0) {
				setErrorMessage(Messages.bind(
						Messages.renameInvalidTableNameMSG, "table", tableName));
				tableText.setFocus();
				return false;
			} else if (!CommonTool.isASCII(tableName)) {
				setErrorMessage(Messages.errMultiBytes);
				tableText.setFocus();
				return false;
			}
		}
		return true;
	}

	/**
	 * add a log of dropping an attribute to change list
	 * 
	 * @param isClassAttribute
	 * @param oldAttrName
	 */
	private void addDropAttrLog(String oldAttrName, boolean isClassAttribute) {
		if (!isClassAttribute) {
			changeList.addSchemeChangeLog(new SchemeChangeLog(oldAttrName,
					null, SchemeInnerType.TYPE_ATTRIBUTE));
		} else {
			changeList.addSchemeChangeLog(new SchemeChangeLog(oldAttrName,
					null, SchemeInnerType.TYPE_CLASSATTRIBUTE));
		}
	}

	/**
	 * add a log of adding an attribute to change list
	 * 
	 * @param newAttrName
	 * @param isClassAttribute
	 */
	private void addNewAttrLog(String newAttrName, boolean isClassAttribute) {
		if (!isClassAttribute) {
			changeList.addSchemeChangeLog(new SchemeChangeLog(null,
					newAttrName, SchemeInnerType.TYPE_ATTRIBUTE));
		} else {
			changeList.addSchemeChangeLog(new SchemeChangeLog(null,
					newAttrName, SchemeInnerType.TYPE_CLASSATTRIBUTE));
		}
	}

	/**
	 * add a log of editing an attribute to change list
	 * 
	 * @param attrName
	 * @param isClassAttribute
	 * @param newAttrName
	 */
	private void addEditAttrLog(String attrName, String newAttrName,
			boolean isClassAttribute) {
		if (!isClassAttribute) {
			changeList.addSchemeChangeLog(new SchemeChangeLog(attrName,
					newAttrName, SchemeInnerType.TYPE_ATTRIBUTE));
		} else {
			changeList.addSchemeChangeLog(new SchemeChangeLog(attrName,
					newAttrName, SchemeInnerType.TYPE_CLASSATTRIBUTE));
		}
	}

}
