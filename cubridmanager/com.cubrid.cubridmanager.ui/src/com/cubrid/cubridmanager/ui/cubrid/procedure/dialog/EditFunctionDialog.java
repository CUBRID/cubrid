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
import org.eclipse.swt.events.KeyListener;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Combo;
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
public class EditFunctionDialog extends
		CMTrayDialog {
	private static final Logger logger = LogUtil.getLogger(EditFunctionDialog.class);
	private Combo returnTypeCombo;
	private org.eclipse.swt.widgets.List javaTypeList;
	private Text javaTypeText;

	private Table funcParamsTable;
	private List<Map<String, String>> funcParamsListData = new ArrayList<Map<String, String>>();
	private TableViewer funcParamsTableViewer;
	private static SqlFormattingStrategy formator = new SqlFormattingStrategy();
	private Text funcNameText;

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
	private Label javaTypeLabel2 = null;
	private Label javaTypeLabel;
	private Label returnSQLTypeLabel = null;
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
	public EditFunctionDialog(Shell parentShell) {
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
		item.setText(Messages.tabItemFuncSetting);
		Composite lockComposite = createUserComposit();
		item.setControl(lockComposite);

		item = new TabItem(tabFolder, SWT.NONE);
		item.setText(Messages.tabItemSQLScript);
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

		functionNameLabel.setText(Messages.lblFunctionName);

		funcNameText = new Text(composite, SWT.BORDER);
		funcNameText.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, false));
		funcNameText.addKeyListener(new org.eclipse.swt.events.KeyAdapter() {
			public void keyPressed(KeyEvent e) {
			}

			public void keyReleased(KeyEvent e) {
				String userName = funcNameText.getText();
				if (null == userName || "".equals(userName)
						|| userName.length() <= 0) {
					getButton(IDialogConstants.OK_ID).setEnabled(false);
					return;
				}
				getButton(IDialogConstants.OK_ID).setEnabled(true);
			}
		});

		final String[] userColumnNameArr = new String[] {
				Messages.tblColFunctionParamName,
				Messages.tblColFunctionParamType,
				Messages.tblColFunctionJavaParamType,
				Messages.tblColFunctionModel

		};
		funcParamsTableViewer = CommonTool.createCommonTableViewer(composite,
				null, userColumnNameArr, CommonTool.createGridData(
						GridData.FILL_BOTH, 6, 4, -1, 200));
		funcParamsTable = funcParamsTableViewer.getTable();
		funcParamsTableViewer.getTable().addSelectionListener(
				new SelectionListener() {

					public void widgetDefaultSelected(SelectionEvent e) {
					}

					public void widgetSelected(SelectionEvent e) {
						if (funcParamsTableViewer.getTable().getSelectionCount() > 0) {
							getButton(BUTTON_EDIT_ID).setEnabled(true);
							getButton(BUTTON_UP_ID).setEnabled(true);
							getButton(BUTTON_DOWN_ID).setEnabled(true);
							getButton(BUTTON_DROP_ID).setEnabled(true);
						}
					}

				});
		funcParamsTableViewer.setInput(funcParamsListData);
		funcParamsTable.setLinesVisible(true);
		funcParamsTable.setHeaderVisible(true);
		funcParamsTable.addSelectionListener(new SelectionAdapter() {
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
		returnSQLTypeLabel = new Label(composite, SWT.LEFT | SWT.WRAP);

		returnSQLTypeLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		returnSQLTypeLabel.setText(Messages.lblReturnSQLType);

		returnTypeCombo = new Combo(composite, SWT.SINGLE);
		returnTypeCombo.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		returnTypeCombo.setVisibleItemCount(10);
		returnTypeCombo.addModifyListener(new ModifyListener() {

			public void modifyText(ModifyEvent e) {
				String name = returnTypeCombo.getText();
				String level = sqlTypeMap.get(name.toUpperCase());

				returnTypeCombo.setData(level);
				if (sqlTypeMap.containsKey(name.toUpperCase())) {
					setJavaTypeEnable(true);
				} else
					setJavaTypeEnable(false);
				if (level == null)
					return;
				if ("4".equals(level)) {
					javaTypeList.setEnabled(false);
					javaTypeList.setSelection(-1);
					javaTypeText.setEnabled(true);
					javaTypeLabel2.setEnabled(true);
					javaTypeLabel.setEnabled(false);
				} else {
					javaTypeList.setEnabled(true);
					javaTypeText.setEnabled(false);
					javaTypeLabel2.setEnabled(false);
					javaTypeLabel.setEnabled(true);
					List<String> list = javaTypeMap.get(level);
					javaTypeList.removeAll();
					if (list != null)
						for (String tmp : list)
							javaTypeList.add(tmp);
					javaTypeList.select(0);
				}
				setJavaTypeList();
			}

		});
		returnTypeCombo.addSelectionListener(new SelectionListener() {

			public void widgetDefaultSelected(SelectionEvent e) {
				List<String> list = javaTypeMap.get("1");
				javaTypeList.removeAll();
				for (String tmp : list)
					javaTypeList.add(tmp);
			}

			public void widgetSelected(SelectionEvent e) {
				String name = returnTypeCombo.getText();
				String level = sqlTypeMap.get(name);
				returnTypeCombo.setData(level);
				if (level.equals("4")) {
					javaTypeList.setEnabled(false);
					javaTypeList.setSelection(-1);
					javaTypeText.setEnabled(true);
					javaTypeLabel2.setEnabled(true);
					javaTypeLabel.setEnabled(false);
				} else {
					javaTypeList.setEnabled(true);
					javaTypeText.setEnabled(false);
					javaTypeLabel2.setEnabled(false);
					javaTypeLabel.setEnabled(true);
					List<String> list = javaTypeMap.get(level);
					javaTypeList.removeAll();
					for (String tmp : list)
						javaTypeList.add(tmp);
					javaTypeList.select(0);
				}
			}
		});
		returnTypeCombo.addKeyListener(new KeyListener() {

			public void keyPressed(KeyEvent e) {
			}

			public void keyReleased(KeyEvent e) {

				// paramTypeCombo.;
			}

		});
		final Label javaNameLabel = new Label(composite, SWT.LEFT | SWT.WRAP);
		javaNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		javaNameLabel.setText(Messages.lblJavaFunctionName);

		javaNameText = new Text(composite, SWT.BORDER);
		GridData gd_javaNameText = new GridData(GridData.FILL_HORIZONTAL);
		javaNameText.setLayoutData(gd_javaNameText);
		javaTypeLabel2 = new Label(composite, SWT.LEFT | SWT.WRAP);
		javaTypeLabel2.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		javaTypeLabel2.setText(Messages.lblSpecialJavaType);
		javaTypeLabel2.setEnabled(false);
		javaTypeText = new Text(composite, SWT.BORDER);
		javaTypeText.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, false));
		javaTypeText.setEnabled(false);
		javaTypeLabel = new Label(composite, SWT.LEFT | SWT.WRAP);
		javaTypeLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		javaTypeLabel.setText(Messages.lblReturnJavaType);

		javaTypeList = new org.eclipse.swt.widgets.List(composite, SWT.BORDER
				| SWT.H_SCROLL | SWT.V_SCROLL);
		GridData gd_javaTypeList = new GridData(GridData.FILL_HORIZONTAL);
		gd_javaTypeList.heightHint = 60;
		javaTypeList.setLayoutData(gd_javaTypeList);

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
		// getShell().setSize(500, 550);
		CommonTool.centerShell(getShell());
		if (!isNewFlag())
			getShell().setText(Messages.msgEditFunctionDialog);
		else
			getShell().setText(Messages.msgAddFunctionDialog);
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
						true, funcParamsListData);
				if (addDlg.open() == IDialogConstants.OK_ID) {// add
					funcParamsListData.add(model);
					funcParamsTableViewer.refresh();
					for (int i = 0; i < funcParamsTableViewer.getTable().getColumnCount(); i++)
						funcParamsTableViewer.getTable().getColumn(i).pack();
				}
			} catch (Exception e) {
				logger.error(e.getMessage(), e);
			}

		} else if (buttonId == BUTTON_EDIT_ID) {// edit
			int index = funcParamsTable.getSelectionIndex();
			if (index < 0)
				return;
			Map<String, String> map = funcParamsListData.get(index);
			AddFuncParamsDialog editDlg = new AddFuncParamsDialog(
					parentComp.getShell(), map, sqlTypeMap, javaTypeMap, false,
					funcParamsListData);

			if (editDlg.open() == IDialogConstants.OK_ID) {
				funcParamsTableViewer.refresh();
				for (int i = 0; i < funcParamsTableViewer.getTable().getColumnCount(); i++)
					funcParamsTableViewer.getTable().getColumn(i).pack();
			}

		} else if (buttonId == BUTTON_UP_ID) {// up
			int index = funcParamsTable.getSelectionIndex();
			if (index <= 0)
				return;
			Map<String, String> map = funcParamsListData.get(index);
			Map<String, String> preMap = funcParamsListData.get(index - 1);
			funcParamsListData.set(index - 1, map);
			funcParamsListData.set(index, preMap);
			funcParamsTableViewer.refresh();
		} else if (buttonId == BUTTON_DOWN_ID) {// down
			int index = funcParamsTable.getSelectionIndex();
			if (index < 0 || index >= funcParamsListData.size() - 1)
				return;
			Map<String, String> map = funcParamsListData.get(index);
			Map<String, String> nextMap = funcParamsListData.get(index + 1);
			funcParamsListData.set(index + 1, map);
			funcParamsListData.set(index, nextMap);
			funcParamsTableViewer.refresh();
		} else if (buttonId == BUTTON_DROP_ID) {// drop
			int index = funcParamsTable.getSelectionIndex();
			if (index < 0)
				return;
			funcParamsListData.remove(index);
			funcParamsTableViewer.refresh();
			getButton(BUTTON_EDIT_ID).setEnabled(false);
			getButton(BUTTON_UP_ID).setEnabled(false);
			getButton(BUTTON_DOWN_ID).setEnabled(false);
			getButton(BUTTON_DROP_ID).setEnabled(false);
		} else if (buttonId == IDialogConstants.OK_ID) {
			if (valid()) {
				CommonSQLExcuterTask task = new CommonSQLExcuterTask(
						database.getDatabaseInfo());
				if (!newFlag) {
					String dropSql = " drop function \""
							+ funcNameText.getText() + "\"";
					task.addSqls(dropSql);
				}
				task.addSqls(getSQLScript());
				connect(-1, new ITask[] { task }, true, getParentShell());
				if (task.getErrorMsg() != null) {
					return;
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
		for (String name : sqlTypeMap.keySet())
			returnTypeCombo.add(name);
		returnTypeCombo.select(0);
		returnTypeCombo.setData("-1");
		if (!newFlag) {

			if (spInfo != null) {
				funcNameText.setEnabled(false);
				String returnType = spInfo.getReturnType();
				String target = spInfo.getTarget();
				String[] javaParamType = null;

				/** funcNameText * */
				funcNameText.setText(spInfo.getSpName());
				if ("void".equalsIgnoreCase(returnType)) {
					returnTypeCombo.select(0);

					List<String> list = javaTypeMap.get("0");
					javaTypeList.removeAll();
					if (list != null)
						for (String tmp : list)
							javaTypeList.add(tmp);
					javaTypeList.select(0);
				} else {
					returnTypeCombo.setText(returnType);
					String level = sqlTypeMap.get(returnType);

					if (level == null || "".equals(level))
						level = "-1";

					returnTypeCombo.setData(level);
					List<String> list = javaTypeMap.get(level);
					javaTypeList.removeAll();
					if (list != null)
						for (String tmp : list)
							javaTypeList.add(tmp);
					javaTypeList.select(0);
				}

				/** java type * */
				if (target != null && target.length() > 0) {
					String returnJavaType = null;
					String javaFuncName = target.substring(0,
							target.indexOf("(")).trim();
					javaParamType = (target.substring(target.indexOf("(") + 1,
							target.indexOf(")"))).split(",");
					if (target.substring(target.indexOf(")")).indexOf("return ") > 0)
						returnJavaType = target.substring(
								target.indexOf(")") + 1).replaceFirst(
								"return ", "").trim();

					if ("4".equals(returnTypeCombo.getData())
							|| "-1".equals(returnTypeCombo.getData())) {
						setJavaTypeEnable(false);
						if (returnJavaType != null)
							javaTypeText.setText(returnJavaType);
					} else {
						setJavaTypeEnable(true);
						if (returnJavaType != null)
							for (int i = 0; i < javaTypeList.getItemCount(); i++) {
								String type = javaTypeList.getItem(i);
								if (returnJavaType.equals(type))
									javaTypeList.select(i);
							}
					}
					javaNameText.setText(javaFuncName);

				}
				List<SPArgsInfo> argsInfoList = spInfo.getArgsInfoList();
				if (javaParamType == null
						|| argsInfoList.size() != javaParamType.length)
					return;
				while (funcParamsListData.size() > 0)
					funcParamsListData.remove(0);
				for (int i = 0; i < javaParamType.length; i++) {
					for (SPArgsInfo spArgsInfo : argsInfoList) {
						if (spArgsInfo.getIndex() == i) {
							Map<String, String> model = new HashMap<String, String>();
							model.put("0", spArgsInfo.getArgName());
							model.put("1", spArgsInfo.getDataType());
							model.put("2", javaParamType[i]);
							model.put("3",
									spArgsInfo.getSpArgsType().toString());
							funcParamsListData.add(model);
						}
					}
				}
				funcParamsTableViewer.refresh();
				for (int i = 0; i < funcParamsTableViewer.getTable().getColumnCount(); i++)
					funcParamsTableViewer.getTable().getColumn(i).pack();

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
			if (funcNameText.getText() == null
					|| "".equals(funcNameText.getText())) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.errInputFunctionName);
				return false;
			}

			String str = CommonTool.validateCheckInIdentifier(funcNameText.getText());
			if (str != null && !str.equals("")) {
				CommonTool.openErrorBox(parentComp.getShell(), Messages.bind(
						Messages.errInputParameterNameValid, str));

				return false;
			}
			if (funcNameText.getText().length() > ValidateUtil.MAX_NAME_LENGTH) {
				CommonTool.openErrorBox(Messages.bind(
						Messages.errInputFunctionNameLength, "function",
						ValidateUtil.MAX_NAME_LENGTH));
				return false;
			}
		}
		if (javaNameText.getText() == null || "".equals(javaNameText.getText())) {
			CommonTool.openErrorBox(parentComp.getShell(),
					Messages.errInputJavaFunctionName);
			return false;
		}

		if (returnTypeCombo.getText().equals(Messages.msgVoidReturnType)) {
			CommonTool.openErrorBox(parentComp.getShell(),
					Messages.errInputSelectSqlType);
			return false;
		}
		if ("4".equals(returnTypeCombo.getData())
				|| "-1".equals(returnTypeCombo.getData())) {
			if (javaTypeText.getText() == null
					|| javaTypeText.getText().equals("")) {
				CommonTool.openErrorBox(parentComp.getShell(),
						Messages.errInputSpecialJavaType);
				return false;
			}

		} else if (javaTypeList.getSelectionCount() == 0) {
			CommonTool.openErrorBox(parentComp.getShell(),
					Messages.errInputSelectJavaType);
			return false;
		}
		String javaName = javaNameText.getText();
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
		sb.append("CREATE").append(" FUNCTION ");
		String functionName = funcNameText.getText();
		if (functionName == null)
			functionName = "";
		sb.append("\"" + functionName + "\"").append("(");
		for (Map<String, String> map : funcParamsListData) {
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
		if (funcParamsListData.size() > 0) {
			if (sb.length() > 0)
				sb.deleteCharAt(sb.length() - 1);
			if (javaSb.length() > 0)
				javaSb.deleteCharAt(javaSb.length() - 1);
		}
		sb.append(")");
		String returnType = returnTypeCombo.getText();
		if (!Messages.msgVoidReturnType.equals(returnType)) {
			sb.append(" RETURN ").append(returnType);
		}
		sb.append(com.cubrid.cubridmanager.core.CommonTool.newLine).append(
				"AS LANGUAGE JAVA ").append(
				com.cubrid.cubridmanager.core.CommonTool.newLine);
		String javaFuncName = javaNameText.getText();
		if (javaFuncName == null)
			javaFuncName = "";
		sb.append("NAME '").append(javaFuncName).append("(").append(javaSb).append(
				")");

		if (returnTypeCombo.getData().equals("4")
				|| returnTypeCombo.getData().equals("-1")) {
			String javaType = javaTypeText.getText();
			if (javaType != null && !"".equals(javaType)) {
				sb.append(" return " + javaType);
			}
		} else {
			String[] javaType = javaTypeList.getSelection();
			if (javaType.length > 0 && javaType[0] != null
					&& !"".equals(javaType[0]))
				sb.append(" return " + javaType[0]);
		}

		sb.append("'");
		return formatSql(sb.toString());
	}

	private void initSqlTypeMap() {
		if (sqlTypeMap == null)
			sqlTypeMap = new TreeMap<String, String>();
		sqlTypeMap.put(Messages.msgVoidReturnType, "0");
		sqlTypeMap.put("CHAR", "1");
		sqlTypeMap.put("VARCHAR", "1");
		sqlTypeMap.put("STRING", "1");
		sqlTypeMap.put("NUMERIC", "2");
		sqlTypeMap.put("BIGINT", "2");
		sqlTypeMap.put("SHORT", "2");
		sqlTypeMap.put("INT", "2");
		sqlTypeMap.put("FLOAT", "2");
		sqlTypeMap.put("DOUBLE", "2");
		sqlTypeMap.put("CURRENCY", "2");
		sqlTypeMap.put("DATE", "3");
		sqlTypeMap.put("DATETIME", "3");
		sqlTypeMap.put("TIME", "3");
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

	private void setJavaTypeEnable(boolean flag) {
		javaTypeList.setEnabled(flag);
		javaTypeList.setSelection(flag ? 0 : -1);
		javaTypeLabel.setEnabled(flag);
		javaTypeLabel2.setEnabled(!flag);
		javaTypeText.setEnabled(!flag);
		// setJavaTypeList();
	}

	private void setJavaTypeList() {
		String sqlType = returnTypeCombo.getText();
		if (sqlType == null || sqlType.equals(""))
			return;
		String level = sqlTypeMap.get(sqlType.toUpperCase());
		if (level == null || level.equals("")) {
			returnTypeCombo.setData("-1");
			return;
		}
		returnTypeCombo.setData(level);
		List<String> list = javaTypeMap.get(level);
		javaTypeList.removeAll();
		if (list != null)
			for (String tmp : list)
				javaTypeList.add(tmp);
		javaTypeList.select(0);
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
