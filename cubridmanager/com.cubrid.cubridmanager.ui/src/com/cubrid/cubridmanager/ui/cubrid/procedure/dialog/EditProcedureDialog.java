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
package com.cubrid.cubridmanager.ui.cubrid.procedure.dialog;

import java.lang.reflect.InvocationTargetException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.dialogs.ProgressMonitorDialog;
import org.eclipse.jface.operation.IRunnableWithProgress;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.events.KeyEvent;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.cubrid.sp.model.SPArgsInfo;
import com.cubrid.cubridmanager.core.cubrid.sp.model.SPArgsType;
import com.cubrid.cubridmanager.core.cubrid.sp.model.SPInfo;
import com.cubrid.cubridmanager.core.cubrid.sp.task.CommonSQLExcuterTask;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.cubrid.procedure.Messages;
import com.cubrid.cubridmanager.ui.query.format.SqlFormattingStrategy;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.TableLabelProvider;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTrayDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * Show the edit user dialog
 * 
 * @author robin 2009-3-18
 */
public class EditProcedureDialog extends
		CMTrayDialog {
	private static final Logger logger = LogUtil.getLogger(EditProcedureDialog.class);
	private Table procParamsTable;
	private List<Map<String, String>> procParamsListData = new ArrayList<Map<String, String>>();
	private TableViewer procParamsTableViewer;
	private static SqlFormattingStrategy formator = new SqlFormattingStrategy();
	private Text procNameText;

	private CubridDatabase database = null;
	private TabFolder tabFolder;
	private Composite parentComp;
	private StyledText sqlScriptText;
	private Text javaNameText;
	private boolean isRunning = false;
	private Composite barComp;
	private GridLayout layout;
	private Map<String, String> sqlTypeMap = null;
	private Map<String, List<String>> javaTypeMap = null;
	private SPInfo spInfo = null;
	private boolean newFlag = false;
	private static final int BUTTON_ADD_ID = 1002;
	private static final int BUTTON_EDIT_ID = 1003;
	private static final int BUTTON_UP_ID = 1004;
	private static final int BUTTON_DOWN_ID = 1005;
	private static final int BUTTON_DROP_ID = 1006;

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public EditProcedureDialog(Shell parentShell) {
		super(parentShell);
		initJavaType();
		initSqlTypeMap();
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp,
				CubridManagerHelpContextIDs.databaseProcedure);
		tabFolder = new TabFolder(parentComp, SWT.NONE);
		tabFolder.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		tabFolder.setLayout(layout);

		TabItem item = new TabItem(tabFolder, SWT.NONE);
		item.setText(Messages.tabItemProcSetting);
		Composite lockComposite = createUserComposit();
		item.setControl(lockComposite);

		item = new TabItem(tabFolder, SWT.NONE);
		item.setText(Messages.tabItemProcSQLScript);
		Composite composite = createSqlScriptComposit();
		item.setControl(composite);
		tabFolder.addSelectionListener(new SelectionListener() {

			public void widgetDefaultSelected(SelectionEvent e) {
			}

			public void widgetSelected(SelectionEvent e) {
				sqlScriptText.setText(getSQLScript());
			}
		});
		initial();
		return parentComp;
	}

	private Composite createUserComposit() {
		final Composite composite = new Composite(tabFolder, SWT.LEFT
				| SWT.WRAP);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		layout.numColumns = 2;
		composite.setLayout(layout);
		final Label functionNameLabel = new Label(composite, SWT.NONE);
		functionNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		;
		functionNameLabel.setText(Messages.lblProcedureName);

		procNameText = new Text(composite, SWT.BORDER);
		procNameText.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, false));
		procNameText.addKeyListener(new org.eclipse.swt.events.KeyAdapter() {
			public void keyPressed(KeyEvent e) {
			}

			public void keyReleased(KeyEvent e) {
				String userName = procNameText.getText();
				if (null == userName || "".equals(userName)
						|| userName.length() <= 0) {
					getButton(IDialogConstants.OK_ID).setEnabled(false);
					return;
				}
				getButton(IDialogConstants.OK_ID).setEnabled(true);
			}
		});

		final String[] userColumnNameArr = new String[] {
				Messages.tblColProcedureParamName,
				Messages.tblColProcedureParamType,
				Messages.tblColProcedureJavaParamType,
				Messages.tblColProcedureModel };
		procParamsTableViewer = CommonTool.createCommonTableViewer(composite,
				null, userColumnNameArr, CommonTool.createGridData(
						GridData.FILL_BOTH, 6, 4, -1, 200));
		procParamsTable = procParamsTableViewer.getTable();
		procParamsTableViewer.getTable().addSelectionListener(
				new SelectionListener() {

					public void widgetDefaultSelected(SelectionEvent e) {
					}

					public void widgetSelected(SelectionEvent e) {
						if (procParamsTableViewer.getTable().getSelectionCount() > 0) {
							getButton(BUTTON_EDIT_ID).setEnabled(true);
							getButton(BUTTON_UP_ID).setEnabled(true);
							getButton(BUTTON_DOWN_ID).setEnabled(true);
							getButton(BUTTON_DROP_ID).setEnabled(true);
						}
					}

				});
		procParamsTableViewer.setInput(procParamsListData);
		procParamsTable.setLinesVisible(true);
		procParamsTable.setHeaderVisible(true);
		procParamsTable.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				// setBtnEnableDisable();
			}
		});
		barComp = new Composite(composite, SWT.NONE);
		final GridData gdbarComp = new GridData(GridData.FILL_HORIZONTAL);
		gdbarComp.horizontalSpan = 2;
		barComp.setLayoutData(gdbarComp);
		GridLayout layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		barComp.setLayout(layout);

		final Label javaNameLabel = new Label(composite, SWT.LEFT | SWT.WRAP);
		javaNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		javaNameLabel.setText(Messages.lblJavaFunctionName);

		javaNameText = new Text(composite, SWT.BORDER);
		GridData gd_javaNameText = new GridData(GridData.FILL_HORIZONTAL);
		javaNameText.setLayoutData(gd_javaNameText);

		return composite;
	}

	private Composite createSqlScriptComposit() {
		final Composite composite = new Composite(tabFolder, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		sqlScriptText = new StyledText(composite, SWT.BORDER | SWT.WRAP
				| SWT.MULTI | SWT.READ_ONLY);
		CommonTool.registerContextMenu(sqlScriptText, false);
		sqlScriptText.setLayoutData(new GridData(GridData.FILL_BOTH));
		composite.setLayout(layout);

		return composite;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		// getShell().setSize(500, 420);
		CommonTool.centerShell(getShell());
		if (!isNewFlag())
			getShell().setText(Messages.msgEditProcedureDialog);
		else
			getShell().setText(Messages.msgAddProcedureDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(barComp, BUTTON_ADD_ID, Messages.btnAddParameter, true);
		createButton(barComp, BUTTON_EDIT_ID, Messages.btnEditParameter, true);
		createButton(barComp, BUTTON_DROP_ID, Messages.btnDropParameter, true);
		createButton(barComp, BUTTON_UP_ID, Messages.btnUpParameter, true);
		createButton(barComp, BUTTON_DOWN_ID, Messages.btnDownParameter, true);
		createButton(parent, IDialogConstants.OK_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);

		getButton(BUTTON_EDIT_ID).setEnabled(false);
		getButton(BUTTON_UP_ID).setEnabled(false);
		getButton(BUTTON_DOWN_ID).setEnabled(false);
		getButton(BUTTON_DROP_ID).setEnabled(false);
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == BUTTON_ADD_ID) {
			Map<String, String> model = new HashMap<String, String>();

			try {
				AddFuncParamsDialog addDlg = new AddFuncParamsDialog(
						parentComp.getShell(), model, sqlTypeMap, javaTypeMap,
						true, procParamsListData);
				if (addDlg.open() == IDialogConstants.OK_ID) {// add
					procParamsListData.add(model);
					procParamsTableViewer.refresh();
					for (int i = 0; i < procParamsTableViewer.getTable().getColumnCount(); i++)
						procParamsTableViewer.getTable().getColumn(i).pack();
				}
			} catch (Exception e) {
				logger.error(e.getMessage(), e);
			}

		} else if (buttonId == BUTTON_EDIT_ID) {// edit
			int index = procParamsTable.getSelectionIndex();
			if (index < 0)
				return;
			Map<String, String> map = procParamsListData.get(index);
			AddFuncParamsDialog editDlg = new AddFuncParamsDialog(
					parentComp.getShell(), map, sqlTypeMap, javaTypeMap, false,
					procParamsListData);
			if (editDlg.open() == IDialogConstants.OK_ID) {
				procParamsTableViewer.refresh();
				for (int i = 0; i < procParamsTableViewer.getTable().getColumnCount(); i++)
					procParamsTableViewer.getTable().getColumn(i).pack();
			}

		} else if (buttonId == BUTTON_UP_ID) {// up
			int index = procParamsTable.getSelectionIndex();
			if (index <= 0)
				return;
			Map<String, String> map = procParamsListData.get(index);
			Map<String, String> preMap = procParamsListData.get(index - 1);
			procParamsListData.set(index - 1, map);
			procParamsListData.set(index, preMap);
			procParamsTableViewer.refresh();
		} else if (buttonId == BUTTON_DOWN_ID) {// down
			int index = procParamsTable.getSelectionIndex();
			if (index < 0 || index >= procParamsListData.size() - 1)
				return;
			Map<String, String> map = procParamsListData.get(index);
			Map<String, String> nextMap = procParamsListData.get(index + 1);
			procParamsListData.set(index + 1, map);
			procParamsListData.set(index, nextMap);
			procParamsTableViewer.refresh();
		} else if (buttonId == BUTTON_DROP_ID) {// drop
			int index = procParamsTable.getSelectionIndex();
			if (index < 0)
				return;
			procParamsListData.remove(index);
			procParamsTableViewer.refresh();
			getButton(BUTTON_EDIT_ID).setEnabled(false);
			getButton(BUTTON_UP_ID).setEnabled(false);
			getButton(BUTTON_DOWN_ID).setEnabled(false);
			getButton(BUTTON_DROP_ID).setEnabled(false);
		} else if (buttonId == IDialogConstants.OK_ID) {
			if (valid()) {
				CommonSQLExcuterTask task = new CommonSQLExcuterTask(
						database.getDatabaseInfo());
				if (!newFlag) {
					String dropSql = " drop procedure \""
							+ procNameText.getText() + "\"";
					task.addSqls(dropSql);
				}

				try {
					task.addSqls(getSQLScript());
					connect(-1, new ITask[] { task }, true, getParentShell());
					if (task.getErrorMsg() != null) {
						return;
					}
				} catch (Exception e) {
					logger.error(e.getMessage(), e);
				}

			} else {
				return;
			}
		}

		super.buttonPressed(buttonId);
	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {

		if (!newFlag) {

			if (spInfo != null) {
				procNameText.setEnabled(false);
				String target = spInfo.getTarget();
				String[] javaParamType = null;

				/** funcNameText * */
				procNameText.setText(spInfo.getSpName());

				/** java type * */
				if (target != null && target.length() > 0
						&& target.indexOf("(") > 0 && target.indexOf(")") > 0) {

					String javaFuncName = target.substring(0,
							target.indexOf("(")).trim();

					javaParamType = (target.substring(target.indexOf("(") + 1,
							target.indexOf(")"))).split(",");

					javaNameText.setText(javaFuncName);

				}
				List<SPArgsInfo> argsInfoList = spInfo.getArgsInfoList();
				if (javaParamType == null
						|| argsInfoList.size() != javaParamType.length)
					return;
				while (procParamsListData.size() > 0)
					procParamsListData.remove(0);
				for (int i = 0; i < javaParamType.length; i++) {
					for (SPArgsInfo spArgsInfo : argsInfoList) {
						if (spArgsInfo.getIndex() == i) {
							Map<String, String> model = new HashMap<String, String>();
							model.put("0", spArgsInfo.getArgName());
							model.put("1", spArgsInfo.getDataType());
							model.put("2", javaParamType[i]);
							model.put("3",
									spArgsInfo.getSpArgsType().toString());
							procParamsListData.add(model);
						}
					}
				}
				procParamsTableViewer.refresh();
				for (int i = 0; i < procParamsTableViewer.getTable().getColumnCount(); i++)
					procParamsTableViewer.getTable().getColumn(i).pack();

			}
		}
	}

	/**
	 * 
	 * Check the data validation
	 * 
	 * @return
	 */
	public boolean valid() {
		if (newFlag) {
			if (procNameText.getText() == null
					|| "".equals(procNameText.getText())) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.errInputProcedureName);
				return false;
			}
			String str = CommonTool.validateCheckInIdentifier(procNameText.getText());
			if (str != null && !str.equals("")) {
				CommonTool.openErrorBox(parentComp.getShell(), Messages.bind(
						Messages.errInputParameterNameValid, str));
				return false;
			}
			if (procNameText.getText().length() > ValidateUtil.MAX_NAME_LENGTH) {
				CommonTool.openErrorBox(Messages.bind(
						Messages.errInputFunctionNameLength, "procedure",
						ValidateUtil.MAX_NAME_LENGTH));
				return false;
			}
		}
		String javaName = javaNameText.getText();
		if (javaName == null || "".equals(javaName)) {
			CommonTool.openErrorBox(parentComp.getShell(),
					Messages.errInputJavaProcedureName);
			return false;
		}

		if (!javaName.matches("[\\w]*\\.[\\w]*")) {
			CommonTool.openErrorBox(parentComp.getShell(),
					Messages.errValidJavaFunctionName);
			return false;
		}

		return true;
	}

	/**
	 * 
	 * Get added CubridDatabase
	 * 
	 * @return
	 */
	public CubridDatabase getDatabase() {
		return database;
	}

	/**
	 * 
	 * Set edited CubridDatabase
	 * 
	 * @param database
	 */
	public void setDatabase(CubridDatabase database) {
		this.database = database;
	}

	public void connect(final int buttonId, final ITask[] tasks,
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
									task.execute();
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

	public boolean isNewFlag() {
		return newFlag;
	}

	public void setNewFlag(boolean newFlag) {
		this.newFlag = newFlag;
	}

	static class MyTableLabelProvider extends
			TableLabelProvider {

		@SuppressWarnings("unchecked")
		@Override
		public Image getColumnImage(Object element, int columnIndex) {
			Map<String, Object> item = (Map<String, Object>) element;
			if (columnIndex > 0) {
				Boolean flag = (Boolean) item.get(columnIndex + "");
				return flag ? CubridManagerUIPlugin.getImage("icons/checked.gif")
						: CubridManagerUIPlugin.getImage("icons/unchecked.gif");

			}
			return null;
		}

		@SuppressWarnings("unchecked")
		@Override
		public String getColumnText(Object element, int columnIndex) {
			if (!(element instanceof Map)) {
				return "";
			}
			if (columnIndex != 0)
				return "";
			Map<String, Object> map = (Map<String, Object>) element;
			return map.get("" + columnIndex).toString();
		}
	}

	private String getSQLScript() {

		StringBuffer sb = new StringBuffer();
		StringBuffer javaSb = new StringBuffer();
		sb.append("CREATE").append(" PROCEDURE ");
		String procedureName = procNameText.getText();
		if (procedureName == null)
			procedureName = "";
		sb.append("\"" + procedureName + "\"").append("(");
		for (Map<String, String> map : procParamsListData) {
			// "PARAMS_INDEX", "PARAM_NAME", "PARAM_TYPE", "JAVA_PARAM_TYPE"
			String name = map.get("0");
			String type = map.get("1");
			String javaType = map.get("2");
			String paramModel = map.get("3");
			sb.append("\"" + name + "\"").append(" ");
			if (!paramModel.equalsIgnoreCase(SPArgsType.IN.toString())) {
				sb.append(paramModel).append(" ");
			}
			sb.append(type).append(",");
			javaSb.append(javaType).append(",");
		}
		if (procParamsListData.size() > 0) {
			if (sb.length() > 0)
				sb.deleteCharAt(sb.length() - 1);
			if (javaSb.length() > 0)
				javaSb.deleteCharAt(javaSb.length() - 1);
		}
		sb.append(")");

		sb.append(com.cubrid.cubridmanager.core.CommonTool.newLine).append(
				"AS LANGUAGE JAVA ").append(
				com.cubrid.cubridmanager.core.CommonTool.newLine);
		String javaFuncName = javaNameText.getText();
		if (javaFuncName == null)
			javaFuncName = "";
		sb.append("NAME '").append(javaFuncName).append("(").append(javaSb).append(
				")");

		sb.append("'");
		return formatSql(sb.toString());
	}

	private void initSqlTypeMap() {
		if (sqlTypeMap == null)
			sqlTypeMap = new TreeMap<String, String>();
		sqlTypeMap.put("--void--", "0");
		sqlTypeMap.put("CHAR", "1");
		sqlTypeMap.put("VARCHAR", "1");
		sqlTypeMap.put("STRING", "1");
		sqlTypeMap.put("NUMERIC", "2");
		sqlTypeMap.put("SHORT", "2");
		sqlTypeMap.put("INT", "2");
		sqlTypeMap.put("BIGINT", "2");
		sqlTypeMap.put("FLOAT", "2");
		sqlTypeMap.put("DOUBLE", "2");
		sqlTypeMap.put("CURRENCY", "2");
		sqlTypeMap.put("DATE", "3");
		sqlTypeMap.put("TIME", "3");
		sqlTypeMap.put("DATETIME", "3");
		sqlTypeMap.put("TIMESTAMP", "3");
		sqlTypeMap.put("SET", "4");
		sqlTypeMap.put("MULTISET", "4");
		sqlTypeMap.put("SEQUENCE", "4");
		sqlTypeMap.put("OBJECT", "5");
		sqlTypeMap.put("CURSOR", "6");
	}

	/**
	 * 
	 * 
	 */
	private void initJavaType() {
		if (javaTypeMap == null)
			javaTypeMap = new TreeMap<String, List<String>>();
		if (!javaTypeMap.containsKey("1")) {
			List<String> list = new ArrayList<String>();
			list.add("java.lang.String");
			list.add("java.sql.Date");
			list.add("java.sql.Time");
			list.add("java.sql.Timestamp");
			list.add("java.lang.Byte");
			list.add("java.lang.Short");
			list.add("java.lang.Integer");
			list.add("java.lang.Long");
			list.add("java.lang.Float");
			list.add("java.lang.Double");
			list.add("java.math.BigDecimal");
			list.add("byte");
			list.add("short");
			list.add("int");
			list.add("long");
			list.add("float");
			list.add("double");
			javaTypeMap.put("1", list);
		}
		if (!javaTypeMap.containsKey("2")) {
			List<String> list = new ArrayList<String>();
			list.add("java.lang.Byte");
			list.add("java.lang.Short");
			list.add("java.lang.Integer");
			list.add("java.lang.Long");
			list.add("java.lang.Float");
			list.add("java.lang.Double");
			list.add("java.math.BigDecimal");
			list.add("java.lang.String");
			list.add("byte");
			list.add("short");
			list.add("int");
			list.add("long");
			list.add("float");
			list.add("double");
			javaTypeMap.put("2", list);
		}
		if (!javaTypeMap.containsKey("3")) {
			List<String> list = new ArrayList<String>();
			list.add("java.sql.Date");
			list.add("java.sql.Time");
			list.add("java.sql.Timestamp");
			list.add("java.lang.String");
			javaTypeMap.put("3", list);
		}
		if (!javaTypeMap.containsKey("4")) {
			List<String> list = new ArrayList<String>();
			javaTypeMap.put("4", list);
		}
		if (!javaTypeMap.containsKey("5")) {
			List<String> list = new ArrayList<String>();
			list.add("cubrid.sql.CUBRIDOID");
			javaTypeMap.put("5", list);
		}
		if (!javaTypeMap.containsKey("6")) {
			List<String> list = new ArrayList<String>();
			list.add("cubrid.jdbc.driver.CUBRIDResultSet");
			javaTypeMap.put("6", list);
		}
	}

	public SPInfo getSpInfo() {
		return spInfo;
	}

	public void setSpInfo(SPInfo spInfo) {
		this.spInfo = spInfo;
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
