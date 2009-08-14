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

import java.lang.reflect.InvocationTargetException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.dialogs.ProgressMonitorDialog;
import org.eclipse.jface.operation.IRunnableWithProgress;
import org.eclipse.jface.viewers.CellEditor;
import org.eclipse.jface.viewers.ComboBoxCellEditor;
import org.eclipse.jface.viewers.ICellModifier;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.jface.viewers.TextCellEditor;
import org.eclipse.jface.viewers.ViewerSorter;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Item;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.cubrid.sp.task.CommonSQLExcuterTask;
import com.cubrid.cubridmanager.core.cubrid.table.model.ClassInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetAllAttrTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetAllClassListTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetViewAllColumnsTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ValidateQueryTask;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.query.format.SqlFormattingStrategy;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.TableContentProvider;
import com.cubrid.cubridmanager.ui.spi.TableLabelProvider;
import com.cubrid.cubridmanager.ui.spi.TableViewerSorter;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * The dialog of create view
 * 
 * @author robin 2009-6-4
 */
public class CreateViewDialog extends
		CMTitleAreaDialog {

	private static final Logger logger = LogUtil.getLogger(CreateViewDialog.class);
	private Combo ownerCombo;

	public List<Map<String, String>> queryListData = new ArrayList<Map<String, String>>();
	public TableViewer queryTableViewer;

	private boolean isRunning = false;
	private static SqlFormattingStrategy formator = new SqlFormattingStrategy();
	private List<Map<String, String>> viewColListData = new ArrayList<Map<String, String>>();
	private TableViewer viewColTableViewer;
	private boolean isPropertyQuery = false;
	private Composite barComp;
	private Composite parentComp;
	private StyledText sqlText;
	private Text tableText;
	private Text querydescText;
	private CubridDatabase database;
	private Composite composite;
	private TabFolder tabFolder = null;
	boolean isNewTableFlag;
	private String newLine;
	private String[] defaultType = { "", "shared", "default" };
	private ClassInfo classInfo = null;
	private List<String> vclassList = null;
	private List<DBAttribute> attrList = null;
	private List<DbUserInfo> userinfo = null;
	private static final int BUTTON_ADD_ID = 1001;
	private static final int BUTTON_DROP_ID = 1002;
	private static final int BUTTON_EDIT_ID = 1003;

	// private static final int BUTTON_VALIDATE_ID = 1004;

	public CreateViewDialog(Shell parentShell, CubridDatabase database,
			boolean isNew) {
		super(parentShell);
		this.database = database;
		this.isNewTableFlag = isNew;
		newLine = com.cubrid.cubridmanager.core.CommonTool.newLine;
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == BUTTON_ADD_ID) {// add
			AddQueryDialog addDlg = new AddQueryDialog(parentComp.getShell(),
					true, -1, this);
			if (addDlg.open() == IDialogConstants.OK_ID) {
				queryTableViewer.getTable().setSelection(
						queryTableViewer.getTable().getItemCount() - 1);
				querydescText.setText(formatSql(queryTableViewer.getTable().getItem(
						queryTableViewer.getTable().getItemCount() - 1).getText()));
			}
			queryTableViewer.getTable().getColumn(0).setWidth(200);
			setButtonStatus();
			valid();
			return;
		} else if (buttonId == BUTTON_DROP_ID) {// delete
			int index = queryTableViewer.getTable().getSelectionIndex();
			queryListData.remove(index);
			queryTableViewer.refresh();
			if (queryListData.size() > index) {
				queryTableViewer.getTable().setSelection(index);
				querydescText.setText(formatSql(queryTableViewer.getTable().getItem(
						index).getText()));
			} else if (index > 0) {
				queryTableViewer.getTable().setSelection(index - 1);
				querydescText.setText(formatSql(queryTableViewer.getTable().getItem(
						index - 1).getText()));
			} else {
				queryTableViewer.getTable().setSelection(index - 1);
				querydescText.setText("");
			}

			validateResult(null, false, -1);
			// querydescText.setText(value);
			setButtonStatus();
			valid();
			return;

		} else if (buttonId == BUTTON_EDIT_ID) {
			StringBuffer sb = new StringBuffer();
			int index = queryTableViewer.getTable().getSelectionIndex();

			sb.append(queryListData.get(index).get("0"));

			AddQueryDialog addDlg = new AddQueryDialog(parentComp.getShell(),
					false, index, this);
			if (addDlg.open() == IDialogConstants.OK_ID) {// add
				queryTableViewer.getTable().setSelection(index);
				querydescText.setText(formatSql(queryTableViewer.getTable().getItem(
						index).getText()));

				// Map<String, String> map = queryListData.get(index);
				// map.put("0", sb.toString());
				// queryTableViewer.refresh();
				// for (int i = 0; i <
				// queryTableViewer.getTable().getColumnCount();
				// i++)
				// queryTableViewer.getTable().getColumn(i).pack();
				// validateResult();
			}

			setButtonStatus();
			valid();
			return;
		} else if (buttonId == IDialogConstants.OK_ID) {
			if (valid()) {

				CommonSQLExcuterTask task = new CommonSQLExcuterTask(
						database.getDatabaseInfo());
				if (!isNewTableFlag) {
					String dropSql = " drop view \"" + tableText.getText()
							+ "\"";
					task.addSqls(dropSql);
				}

				task.addSqls(getCreateSQLScript());
				if (!database.getDatabaseInfo().getAuthLoginedDbUserInfo().getName().equalsIgnoreCase(
						ownerCombo.getText()))
					task.addCallSqls(getOwnerSql());
				execTask(-1, new ITask[] { task }, true, getParentShell());
				if (task.getErrorMsg() != null) {
					return;
				}
			} else {
				return;
			}
		} else if (buttonId == IDialogConstants.CANCEL_ID) {
			this.getShell().dispose();
			this.close();
		}
		super.buttonPressed(buttonId);
	}

	@Override
	protected Control createDialogArea(Composite parent) {

		parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp,
				CubridManagerHelpContextIDs.databaseView);
		composite = new Composite(parentComp, SWT.NONE);
		final GridData gd_composite = new GridData(SWT.FILL, SWT.FILL, true,
				true);
		composite.setLayoutData(gd_composite);

		GridLayout layout = new GridLayout();
		layout.numColumns = 1;
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		layout.numColumns = 1;
		composite.setLayout(layout);

		tabFolder = new TabFolder(composite, SWT.NONE);
		final GridData gd_tabFolder = new GridData(SWT.FILL, SWT.FILL, true,
				true);
		tabFolder.setLayoutData(gd_tabFolder);

		final TabItem generalTabItem = new TabItem(tabFolder, SWT.NONE);
		generalTabItem.setText(Messages.tabItemGeneral);

		final Composite composite_Genaral = createGeneralComposite();
		generalTabItem.setControl(composite_Genaral);

		final TabItem ddlTabItem = new TabItem(tabFolder, SWT.NONE);
		ddlTabItem.setText(Messages.tabItemSQLScript);

		final Composite sqlScriptComp = createSQLScriptComposite();
		ddlTabItem.setControl(sqlScriptComp);

		tabFolder.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				// if(sqlText==null)
				StringBuffer sb = new StringBuffer();
				sb.append(getCreateSQLScript());
				if (isNewTableFlag) {
					String owner = ownerCombo.getText();
					if (!database.getDatabaseInfo().getAuthLoginedDbUserInfo().getName().equalsIgnoreCase(
							owner)) {
						sb.append(";" + newLine + newLine + newLine).append(
								getOwnerSql()).append(";" + newLine);
					}
				} else {
					String owner = ownerCombo.getText();
					if (!classInfo.getOwnerName().equalsIgnoreCase(owner)) {
						sb.append(";" + newLine + newLine + newLine).append(
								getOwnerSql()).append(";" + newLine);
					}
				}

				sqlText.setText(formatSql(sb.toString()));
			}
		});
		init();
		return parent;
	}

	private Composite createSQLScriptComposite() {
		final Composite composite = new Composite(tabFolder, SWT.NONE);
		composite.setLayout(new GridLayout());
		sqlText = new StyledText(composite, SWT.WRAP | SWT.BORDER
				| SWT.READ_ONLY);
		CommonTool.registerContextMenu(sqlText, false);
		final GridData gd_sqlText = new GridData(SWT.FILL, SWT.FILL, true, true);
		sqlText.setLayoutData(gd_sqlText);

		return composite;
	}

	private Composite createGeneralComposite() {
		final Composite composite = new Composite(tabFolder, SWT.NONE);
		final GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		gridLayout.makeColumnsEqualWidth = true;
		composite.setLayout(gridLayout);

		final Group group = new Group(composite, SWT.NONE);
		GridData gd_group = new GridData(SWT.FILL, SWT.CENTER, true, false);
		gd_group.horizontalSpan = 2;
		group.setLayoutData(gd_group);
		final GridLayout gridLayout_1 = new GridLayout();
		gridLayout_1.numColumns = 2;
		group.setLayout(gridLayout_1);

		final Label tableNameLabel = new Label(group, SWT.SHADOW_IN);
		tableNameLabel.setText(Messages.lblViewName);

		tableText = new Text(group, SWT.BORDER);
		final GridData gd_tableText = new GridData(SWT.FILL, SWT.CENTER, false,
				false);
		gd_tableText.horizontalIndent = 30;
		tableText.setLayoutData(gd_tableText);

		final Label ownerLabel = new Label(group, SWT.NONE);
		ownerLabel.setText(Messages.lblViewOwnerName);

		ownerCombo = new Combo(group, SWT.DROP_DOWN | SWT.READ_ONLY);
		final GridData gd_combo = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		gd_combo.horizontalIndent = 30;
		ownerCombo.setLayoutData(gd_combo);
		ownerCombo.setVisibleItemCount(10);
		final Label querySQLLabel = new Label(composite, SWT.LEFT | SWT.WRAP);
		querySQLLabel.setText(Messages.lblQueryList);
		querySQLLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		final Label SQLDescLabel = new Label(composite, SWT.LEFT | SWT.WRAP);
		SQLDescLabel.setText(Messages.lblSelectQueryList);
		SQLDescLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		final String[] columnNameArr1 = new String[] { "col1" };
		queryTableViewer = CommonTool.createCommonTableViewer(composite, null,
				columnNameArr1, CommonTool.createGridData(GridData.FILL_BOTH,
						1, 1, -1, 100));
		queryTableViewer.getTable().setHeaderVisible(false);
		queryTableViewer.setInput(queryListData);
		queryTableViewer.getTable().addSelectionListener(
				new SelectionListener() {

					public void widgetDefaultSelected(SelectionEvent e) {

					}

					public void widgetSelected(SelectionEvent e) {
						int index = queryTableViewer.getTable().getSelectionIndex();
						String value = queryListData.get(index).get("0");
						if (value != null)
							querydescText.setText(formatSql(value));
						setButtonStatus();
					}

				});
		queryTableViewer.getTable().getColumn(0).setWidth(200);
		querydescText = new Text(composite, SWT.LEFT | SWT.BORDER
				| SWT.READ_ONLY | SWT.WRAP | SWT.V_SCROLL);
		querydescText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_BOTH, 1, 1, -1, 200));
		barComp = new Composite(composite, SWT.NONE);
		final GridData gdbarComp = new GridData(GridData.FILL_HORIZONTAL);
		gdbarComp.horizontalSpan = 2;
		barComp.setLayoutData(gdbarComp);
		GridLayout layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		barComp.setLayout(layout);

		final Label columnsLabel = new Label(composite, SWT.NONE);
		columnsLabel.setText(Messages.lblTableNameColumns);
		final String[] columnNameArr = new String[] { Messages.tblColViewName,
				Messages.tblColViewDataType, Messages.tblColViewDefaultType,
				Messages.tblColViewDefaultValue

		};
		viewColTableViewer = createCommonTableViewer(composite, null,
				columnNameArr, CommonTool.createGridData(GridData.FILL_BOTH, 2,
						4, -1, 200));

		viewColTableViewer.setInput(viewColListData);
		viewColTableViewer.setColumnProperties(columnNameArr);
		// viewColTableViewer.getTable().setEnabled(!isPropertyQuery);
		CellEditor[] editors = new CellEditor[4];
		editors[0] = new TextCellEditor(viewColTableViewer.getTable());
		editors[1] = null;
		editors[2] = new ComboBoxCellEditor(viewColTableViewer.getTable(),
				defaultType, SWT.READ_ONLY);
		editors[3] = new TextCellEditor(viewColTableViewer.getTable());

		viewColTableViewer.setCellEditors(editors);
		viewColTableViewer.setCellModifier(new ICellModifier() {
			@SuppressWarnings("unchecked")
			public boolean canModify(Object element, String property) {

				if (isPropertyQuery)
					return false;
				Map<String, String> map = (Map<String, String>) element;
				if (property.equals(columnNameArr[3])) {
					String defaultTypeStr = map.get("2");
					if (defaultTypeStr == null
							|| defaultType[0].equals(defaultTypeStr))
						return false;
				}
				return true;
			}

			@SuppressWarnings("unchecked")
			public Object getValue(Object element, String property) {
				Map<String, String> map = (Map<String, String>) element;
				if (property.equals(columnNameArr[0])) {
					return map.get("0");
				} else if (property.equals(columnNameArr[2])) {
					String str = map.get("2");
					int index = 0;
					if (str != null) {
						for (int i = 0; defaultType != null
								&& i < defaultType.length; i++) {
							if (str.equals(defaultType[i])) {
								index = i;
								break;
							}
						}
					}
					return Integer.valueOf(index);
				} else if (property.equals(columnNameArr[3])) {
					String type = map.get("1");
					String value = map.get("3");
					if (value == null)
						value = "";
					if (type != null
							&& ("CHAR".equalsIgnoreCase(type)
									|| "STRING".equalsIgnoreCase(type) || "VARCHAR".equalsIgnoreCase(type)))
						if (value.startsWith("'") && value.endsWith("'")
								&& value.length() > 1)
							value = value.substring(1, value.length() - 1);
					return value;
				}
				return null;
			}

			@SuppressWarnings("unchecked")
			public void modify(Object element, String property, Object value) {
				if (element instanceof Item) {
					element = ((Item) element).getData();
				}
				Map<String, String> map = (Map<String, String>) element;
				String type = map.get("1");
				if (property.equals(columnNameArr[0])) {
					map.put("0", value.toString());
				} else if (property.equals(columnNameArr[2])) {
					int index = Integer.parseInt(value.toString());
					if (index == 0)
						map.put("3", "");
					map.put("2", defaultType[index]);
				} else if (property.equals(columnNameArr[3])) {
					String val = String.valueOf(value);
					if (val == null)
						val = "";
					if ("INTEGER".equalsIgnoreCase(type)
							|| "FLOAT".equalsIgnoreCase(type)
							|| "NUMBERRIC".equalsIgnoreCase(type)
							|| "BIGINT".equalsIgnoreCase(type)
							|| "SMALLINT".equalsIgnoreCase(type)
							|| "DOUBLE".equalsIgnoreCase(type)) {
						if (val.matches("[0-9\\-]+(\\.[0-9]+)?"))
							map.put("3", value.toString());
					} else {
						map.put("3", value.toString());
					}
				}
				viewColTableViewer.refresh();
			}
		});

		return composite;
	}

	private void init() {
		String owner = null;
		fillOwnerCombo();
		if (!isNewTableFlag) {
			if (classInfo == null)
				return;
			tableText.setText(classInfo.getClassName());
			tableText.setEnabled(false);
			tableText.addModifyListener(new ModifyListener() {
				public void modifyText(ModifyEvent e) {
					valid();
				}
			});

			owner = classInfo.getOwnerName();
			String[] strs = new String[] {
					classInfo.getClassName(),
					isPropertyQuery ? Messages.msgPropertyInfo
							: Messages.msgEditInfo };
			setTitle(Messages.bind(Messages.editViewMsgTitle, strs));
			setMessage(Messages.editViewMsg);
			strs = new String[] {
					classInfo.getClassName(),
					isPropertyQuery ? Messages.msgPropertyInfo
							: Messages.msgEditInfo };
			String title = Messages.bind(Messages.editViewShellTitle, strs);
			getShell().setText(title);
			for (String sql : vclassList) {
				Map<String, String> map = new HashMap<String, String>();
				map.put("0", sql);
				queryListData.add(map);
			}
			if (vclassList.size() > 0)
				querydescText.setText(formatSql(vclassList.get(0)));

			// "Name", "Data type", "Default type", "Default value"
			for (DBAttribute attr : attrList) {
				Map<String, String> map = new HashMap<String, String>();
				map.put("0", attr.getName());
				String type = attr.getType();
				if ("VARNCHAR".equals(type))
					type = "NCHAR VARYING";
				if ("VARBIT".equals(type))
					type = "BIT VARYING";
				if ("OBJECT".equalsIgnoreCase(type)) {
					if (attr.getDomainClassName() != null
							&& !"".equals(attr.getDomainClassName()))
						type = attr.getDomainClassName();
					else
						type = "OBJECT";
				}
				map.put("1", type);
				map.put("2", defaultType[0]);
				map.put("3", defaultType[0]);

				String dfltType = null;
				String value = null;
				if (attr.getDefault() != null && !attr.getDefault().equals("")) {
					if (attr.isShared())
						dfltType = defaultType[1];
					else
						dfltType = defaultType[2];
					value = attr.getDefault();
				}
				if (value == null)
					value = "";
				if (type != null
						&& ("CHAR".equalsIgnoreCase(type)
								|| "STRING".equalsIgnoreCase(type) || "VARCHAR".equalsIgnoreCase(type)))
					if (value.startsWith("'") && value.endsWith("'")
							&& value.length() > 1)
						value = value.substring(1, value.length() - 1);
				map.put("2", dfltType);
				map.put("3", value);
				viewColListData.add(map);
			}
			viewColTableViewer.refresh();
			for (int i = 0; i < viewColTableViewer.getTable().getColumnCount(); i++)
				viewColTableViewer.getTable().getColumn(i).pack();
			queryTableViewer.getTable().select(0);
		} else {
			tableText.setText("");
			tableText.addModifyListener(new ModifyListener() {
				public void modifyText(ModifyEvent e) {
					valid();
				}
			});
			owner = database.getUserName();
			setTitle(Messages.newViewMsgTitle);
			setMessage(Messages.newViewMsg);
			// String title = Messages
			// .bind(Messages.newViewShellTitle, database.getName(),
			// database.getServer().getName());
			getShell().setText(Messages.newViewShellTitle);
		}

		for (int i = 0; i < ownerCombo.getItemCount(); i++) {
			if (ownerCombo.getItem(i).equalsIgnoreCase(owner)) {
				ownerCombo.select(i);
				break;
			}
		}
		queryTableViewer.refresh();

	}

	private void fillOwnerCombo() {

		if (userinfo == null) {
			return;
		}
		for (DbUserInfo ui : userinfo) {
			String userName = ui.getName();
			ownerCombo.add(userName.toUpperCase());
		}

	}

	public void execTask(final int buttonId, final ITask[] tasks,
			boolean cancelable, Shell shell) {
		final Display display = shell.getDisplay();
		isRunning = false;
		try {
			new ProgressMonitorDialog(getShell()).run(true, cancelable,
					new IRunnableWithProgress() {
						public void run(final IProgressMonitor monitor) throws InvocationTargetException,
								InterruptedException {
							monitor.beginTask(
									com.cubrid.cubridmanager.ui.spi.Messages.msgRunning,
									IProgressMonitor.UNKNOWN);

							if (monitor.isCanceled()) {
								return;
							}

							isRunning = true;
							Thread thread = new Thread() {
								public void run() {
									while (!monitor.isCanceled() && isRunning) {
										try {
											sleep(1);
										} catch (InterruptedException e) {
										}
									}
									if (monitor.isCanceled()) {
										for (ITask t : tasks) {
											if (t != null)
												t.cancel();
										}

									}
								}
							};
							thread.start();
							if (monitor.isCanceled()) {
								isRunning = false;
								return;
							}
							for (ITask task : tasks) {

								if (task != null) {
									if (task instanceof GetAllClassListTask) {
										((GetAllClassListTask) task).getClassInfoTaskExcute();

									} else if (task instanceof GetViewAllColumnsTask) {
										((GetViewAllColumnsTask) task).getAllVclassListTaskExcute();
									} else if (task instanceof GetAllAttrTask) {
										((GetAllAttrTask) task).getDbAllAttrListTaskExcute();
									} else {
										task.execute();
									}
									final String msg = task.getErrorMsg();
									if (monitor.isCanceled()) {
										isRunning = false;
										return;
									}
									if (msg != null && msg.length() > 0
											&& !monitor.isCanceled()) {
										display.syncExec(new Runnable() {
											public void run() {
												CommonTool.openErrorBox(
														getShell(), msg);
											}
										});
										isRunning = false;
										return;
									}
								}
								if (monitor.isCanceled()) {
									isRunning = false;
									return;
								}
							}
							if (monitor.isCanceled()) {
								isRunning = false;
								return;
							}
							if (!monitor.isCanceled()) {
								display.syncExec(new Runnable() {
									public void run() {
										if (buttonId > 0) {
											setReturnCode(buttonId);
											close();
										}
									}
								});
							}
							isRunning = false;
							monitor.done();
						}
					});
		} catch (InvocationTargetException e) {
			logger.error(e.getMessage(), e);
		} catch (InterruptedException e) {
			logger.error(e.getMessage(), e);
		}
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		getShell().setSize(600, 700);
		CommonTool.centerShell(getShell());
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(barComp, BUTTON_ADD_ID, Messages.btnAddParameter, true);
		createButton(barComp, BUTTON_DROP_ID, Messages.btnDeleteParameter, true);
		createButton(barComp, BUTTON_EDIT_ID, Messages.btnEditParameter, true);
		// createButton(barComp, BUTTON_VALIDATE_ID, Messages.btnValidateColumn,
		// true);

		createButton(parent, IDialogConstants.OK_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, false);
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
		setButtonStatus();
	}

	private String getCreateSQLScript() {
		StringBuffer sb = new StringBuffer();
		sb.append("CREATE VIEW ");
		String viewName = "";
		if (tableText != null && tableText.getText() != null
				&& !tableText.getText().equals(""))
			viewName = tableText.getText();
		else
			sb.append("[VIEWNAME]");
		if (viewName != null)
			sb.append("\"" + viewName + "\"");
		sb.append("(");

		for (Map<String, String> map : viewColListData) {// "Name", "Data
			// type", "Shared",
			// "Default","Default
			// value"
			String type = map.get("1");
			sb.append(newLine).append(" \"").append(map.get("0")).append("\" ").append(
					type);
			String defaultType = map.get("2");
			String defaultValue = map.get("3");

			if (defaultType != null && !"".equals(defaultType)
					&& defaultValue != null && !"".equals(defaultValue)) {

				if (type != null
						&& ("CHAR".equalsIgnoreCase(type)
								|| "STRING".equalsIgnoreCase(type) || "VARCHAR".equalsIgnoreCase(type)))
					sb.append(" " + defaultType).append(
							" '" + defaultValue + "'");
				else
					sb.append(" " + defaultType).append(" " + defaultValue);
			}
			sb.append(",");
		}

		if (viewColListData.size() > 0 && sb.length() > 0) {
			sb.deleteCharAt(sb.length() - 1);
		}
		sb.append(")").append(newLine);
		sb.append("    AS ");

		for (int i = 0; i < queryListData.size(); i++) {
			Map<String, String> map = queryListData.get(i);
			sb.append(newLine).append(map.get("0"));
			if (i != queryListData.size() - 1)
				sb.append(newLine).append(" UNION ALL ");
		}
		return sb.toString();
	}

	/**
	 * get the sql of change owner
	 * 
	 * @param tableName
	 * @param newOwner
	 * @return
	 */
	private String getOwnerSql() {
		String tableName = tableText.getText();
		String owner = ownerCombo.getText();
		if (database.getDatabaseInfo().getAuthLoginedDbUserInfo().getName().equalsIgnoreCase(
				owner))
			return null;
		StringBuffer bf = new StringBuffer();
		bf.append("call change_owner ('");
		if (tableName != null)
			bf.append(tableName);
		bf.append("','");
		if (owner != null)
			bf.append(owner);
		bf.append("') on class db_authorizations");
		return bf.toString();
	}

	public ClassInfo getClassInfo() {
		return classInfo;
	}

	public void setClassInfo(ClassInfo classInfo) {
		this.classInfo = classInfo;
	}

	public List<String> getVclassList() {
		return vclassList;
	}

	public void setVclassList(List<String> vclassList) {
		this.vclassList = vclassList;
	}

	public List<DBAttribute> getAttrList() {
		return attrList;
	}

	public void setAttrList(List<DBAttribute> attrList) {
		this.attrList = attrList;
	}

	public List<DbUserInfo> getUserinfo() {
		return userinfo;
	}

	public void setUserinfo(List<DbUserInfo> userinfo) {
		this.userinfo = userinfo;
	}

	public boolean validateResult(String plusSql, boolean isNewSql, int index) {
		ValidateQueryTask task = new ValidateQueryTask(
				database.getDatabaseInfo());
		Map<String, String> oldSql = null;
		if (!isNewSql && index >= 0 && index < queryListData.size()) {
			oldSql = queryListData.get(index);
			Map<String, String> newSql = new HashMap<String, String>();
			newSql.put("0", plusSql);
			queryListData.set(index, newSql);
		}

		for (int i = 0; i < queryListData.size(); i++) {
			// (Map<String, String> m : queryListData) {
			// }
			Map<String, String> m = queryListData.get(i);
			task.addSqls(m.get("0"));
		}
		if (isNewSql && plusSql != null && !"".equals(plusSql))
			task.addSqls(plusSql);
		execTask(-1, new ITask[] { task }, true, getParentShell());
		if (task.getErrorMsg() != null) {
			queryListData.set(index, oldSql);
			return false;
		} else {
			List<Map<String, String>> result = task.getResult();

			while (viewColListData.size() > 0)
				viewColListData.remove(0);
			for (Map<String, String> m : result) {
				viewColListData.add(m);
			}
			viewColTableViewer.refresh();
			for (int i = 0; i < viewColTableViewer.getTable().getColumnCount(); i++) {
				viewColTableViewer.getTable().getColumn(i).pack();
			}
		}
		return true;
	}

	private void setButtonStatus() {
		int index = queryTableViewer.getTable().getSelectionCount();
		if (index > 0) {

			getButton(BUTTON_DROP_ID).setEnabled(true);
			getButton(BUTTON_EDIT_ID).setEnabled(true);
		} else {

			getButton(BUTTON_DROP_ID).setEnabled(false);
			getButton(BUTTON_EDIT_ID).setEnabled(false);
		}

		if (isPropertyQuery) {
			getButton(BUTTON_ADD_ID).setEnabled(false);
			getButton(BUTTON_DROP_ID).setEnabled(false);
			getButton(BUTTON_EDIT_ID).setEnabled(false);
			// getButton(BUTTON_VALIDATE_ID).setEnabled(false);
			getButton(IDialogConstants.OK_ID).setEnabled(false);
			tableText.setEnabled(false);
			ownerCombo.setEnabled(false);
		}

	}

	public boolean isPropertyQuery() {
		return isPropertyQuery;
	}

	public void setPropertyQuery(boolean isPropertyQuery) {
		this.isPropertyQuery = isPropertyQuery;
	}

	public TableViewer createCommonTableViewer(Composite parent,
			ViewerSorter sorter, final String[] columnNameArr, GridData gridData) {
		final TableViewer tableViewer = new TableViewer(parent, SWT.V_SCROLL
				| SWT.MULTI | SWT.BORDER | SWT.H_SCROLL | SWT.FULL_SELECTION);
		tableViewer.setContentProvider(new TableContentProvider());
		tableViewer.setLabelProvider(new TableLabelProvider());
		if (sorter != null)
			tableViewer.setSorter(sorter);

		tableViewer.getTable().setLinesVisible(true);
		tableViewer.getTable().setHeaderVisible(true);
		tableViewer.getTable().setLayoutData(gridData);

		for (int i = 0; i < columnNameArr.length; i++) {
			final TableColumn tblColumn = new TableColumn(
					tableViewer.getTable(), SWT.LEFT
							| (!isPropertyQuery ? SWT.READ_ONLY : SWT.NULL));
			tblColumn.setText(columnNameArr[i]);
			if (sorter != null) {
				tblColumn.addSelectionListener(new SelectionAdapter() {
					public void widgetSelected(SelectionEvent event) {
						TableColumn column = (TableColumn) event.widget;
						int j = 0;
						for (j = 0; j < columnNameArr.length; j++) {
							if (column.getText().equals(columnNameArr[j])) {
								break;
							}
						}
						TableViewerSorter sorter = ((TableViewerSorter) tableViewer.getSorter());
						if (sorter == null) {
							return;
						}
						sorter.doSort(j);
						tableViewer.getTable().setSortColumn(column);
						tableViewer.getTable().setSortDirection(
								sorter.isAsc() ? SWT.UP : SWT.DOWN);
						tableViewer.refresh();
						for (int k = 0; k < tableViewer.getTable().getColumnCount(); k++) {
							tableViewer.getTable().getColumn(k).pack();
						}
					}
				});
			}
			tblColumn.pack();
		}
		return tableViewer;
	}

	private boolean valid() {
		setErrorMessage(null);
		if (isNewTableFlag) {
			if (getButton(IDialogConstants.OK_ID) != null)
				getButton(IDialogConstants.OK_ID).setEnabled(false);
			if (tableText.getText() == null || "".equals(tableText.getText())) {
				setErrorMessage(Messages.errInputViewName);

				return false;
			}

			String str = CommonTool.validateCheckInIdentifier(tableText.getText());
			if (str != null && !str.equals("")) {
				setErrorMessage(Messages.bind(Messages.errInputValidViewName,
						str));
				return false;
			}
			if (tableText.getText().length() > ValidateUtil.MAX_NAME_LENGTH) {
				setErrorMessage(Messages.bind(Messages.errInputNameLength,
						ValidateUtil.MAX_NAME_LENGTH));
				return false;
			}
		}
		if (queryListData.size() == 0) {
			setErrorMessage(Messages.errAddSpecification);
			return false;
		}
		if (viewColListData.size() == 0) {
			setErrorMessage(Messages.errClickValidate);
			return false;
		}

		if (getButton(IDialogConstants.OK_ID) != null)
			getButton(IDialogConstants.OK_ID).setEnabled(true);
		return true;
	}

	/**
	 * Format the sql script
	 * 
	 * @param sql
	 * @return
	 */
	private String formatSql(String sql) {
		sql = formator.format(sql + ";");
		return sql.trim().endsWith(";") ? sql.trim().substring(0,
				sql.trim().length() - 1) : "";
	}

}
