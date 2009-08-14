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

import java.util.List;
import java.util.Map;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.ui.cubrid.procedure.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * The Dialog of add function parameter
 * 
 * @author robin 2009-3-11
 */
public class AddFuncParamsDialog extends CMTitleAreaDialog {
	private org.eclipse.swt.widgets.List javaTypeList;
	private Combo paramTypeCombo;
	private Combo paramModelCombo;

	private Composite parentComp;
	private CubridDatabase database = null;
	private Map<String, String> model;
	private Map<String, String> sqlTypeMap = null;
	private Map<String, List<String>> javaTypeMap = null;
	private Text parameterNameText = null;
	private Label javaTypeLabel = null;
	private Label javaTypeLabel2 = null;
	private Text javaTypeText;
	private boolean newFlag = true;
	private final static String[] paramModelStrs = new String[]
		{
		        "IN",
		        "OUT",
		        "INOUT"
		};
	private List<Map<String, String>> paramList;

	public AddFuncParamsDialog(Shell parentShell,Map<String, String> model,Map<String, String> sqlTypeMap,
	        Map<String, List<String>> javaTypeMap,boolean newFlag,List<Map<String, String>> paramList) {
		super(parentShell);
		this.sqlTypeMap = sqlTypeMap;
		this.javaTypeMap = javaTypeMap;
		this.model = model;
		this.newFlag = newFlag;
		this.paramList = paramList;
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		parentComp = (Composite) super.createDialogArea(parent);

		final Composite composite = new Composite(parentComp, SWT.NONE);
		composite.setLayout(new GridLayout());
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		createdbNameGroup(composite);
		if (newFlag) {
			setTitle(Messages.titleAddFuncParamDialog);
			setMessage(Messages.msgAddFuncParamDialog);
		} else {
			setTitle(Messages.titleEditFuncParamDialog);
			setMessage(Messages.msgEditFuncParamDialog);
		}
		initial();
		return parentComp;
	}

	/**
	 * Create Database Name Group
	 * 
	 * @param composite
	 */
	private void createdbNameGroup(Composite composite) {

		final Group dbnameGroup = new Group(composite, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginWidth = 10;
		layout.marginHeight = 10;
		layout.numColumns = 2;
		final GridData gd_dbnameGroup = new GridData(GridData.FILL_BOTH);
		dbnameGroup.setLayoutData(gd_dbnameGroup);
		dbnameGroup.setLayout(layout);

		final Label parameterNameLabel = new Label(dbnameGroup, SWT.LEFT | SWT.WRAP);

		parameterNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		parameterNameLabel.setText(Messages.lblParameterName);

		parameterNameText = new Text(dbnameGroup, SWT.BORDER);
		parameterNameText.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, false));
		parameterNameText.addModifyListener(new ModifyListener() {
			public void modifyText(ModifyEvent e) {
				setValidMessage();
			}
		});

		final Label databaseName = new Label(dbnameGroup, SWT.LEFT | SWT.WRAP);

		databaseName.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		databaseName.setText(Messages.lblSqlType);

		paramTypeCombo = new Combo(dbnameGroup, SWT.SINGLE);
		paramTypeCombo.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, false));
		paramTypeCombo.setVisibleItemCount(10);
		paramTypeCombo.addModifyListener(new ModifyListener() {

			public void modifyText(ModifyEvent e) {
				String name = paramTypeCombo.getText();
				if (sqlTypeMap.containsKey(name.toUpperCase())) {
					setJavaTypeEnable(true);
				} else
					setJavaTypeEnable(false);
				String level = sqlTypeMap.get(name.toUpperCase());
		
				
				paramTypeCombo.setData(level);
				if(level==null)
					return ;
				if (level.equals("4")) {
					setJavaTypeEnable(false);
				} else {
					setJavaTypeEnable(true);
					List<String> list = javaTypeMap.get(level);
					javaTypeList.removeAll();
					for (String tmp : list)
						javaTypeList.add(tmp);
					javaTypeList.select(0);
				}

				setValidMessage();

			}
		});

		javaTypeLabel2 = new Label(dbnameGroup, SWT.LEFT | SWT.WRAP);
		javaTypeLabel2.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		javaTypeLabel2.setText(Messages.lblSpecialJavaType);
		javaTypeLabel2.setEnabled(false);
		javaTypeText = new Text(dbnameGroup, SWT.BORDER);
		javaTypeText.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, false));
		javaTypeText.setEnabled(false);
		javaTypeText.addModifyListener(new ModifyListener() {
			public void modifyText(ModifyEvent e) {
				setValidMessage();
			}
		});
		javaTypeLabel = new Label(dbnameGroup, SWT.LEFT | SWT.WRAP);

		javaTypeLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		javaTypeLabel.setText(Messages.lblJavaType);
		javaTypeList = new org.eclipse.swt.widgets.List(dbnameGroup, SWT.BORDER | SWT.H_SCROLL | SWT.V_SCROLL);
		GridData gd = new GridData(GridData.FILL_BOTH);
		javaTypeList.setLayoutData(gd);
		javaTypeList.addSelectionListener(new SelectionListener() {

			public void widgetDefaultSelected(SelectionEvent e) {
			}

			public void widgetSelected(SelectionEvent e) {
				setValidMessage();

			}
		});

		final Label paramModelLabel = new Label(dbnameGroup, SWT.LEFT | SWT.WRAP);

		paramModelLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		paramModelLabel.setText(Messages.lblParamModel);

		paramModelCombo = new Combo(dbnameGroup, SWT.SINGLE | SWT.READ_ONLY);
		paramModelCombo.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, false));
	}

	private void initial() {
		/** init the combo * */
		for (String name : sqlTypeMap.keySet())
			if (!"0".equals(sqlTypeMap.get(name)))
				paramTypeCombo.add(name);
		paramTypeCombo.select(0);
		for (String t : paramModelStrs)
			paramModelCombo.add(t);
		paramModelCombo.select(0);

		/** update the value* */
		if (!newFlag) {
			String name = model.get("0");
			String sqlType = model.get("1");
			String javaType = model.get("2");
			String paramModel = model.get("3");

			if (name != null && !"".equals(name))
				parameterNameText.setText(name);
			if (sqlType != null && !"".equals(sqlType)) {
				paramTypeCombo.setText(sqlType);
				String level = sqlTypeMap.get(sqlType);
				if (level == null || "".equals(level))
					level = "-1";
				paramTypeCombo.setData(level);
				if ("4".equals(level) || "-1".equals(level)) {
					setJavaTypeEnable(false);
					javaTypeText.setText(javaType);
				} else {
					setJavaTypeEnable(true);
					List<String> list = javaTypeMap.get(sqlTypeMap.get(sqlType));
					javaTypeList.removeAll();
					for (String tmp : list)
						javaTypeList.add(tmp);

					for (int i = 0; i < javaTypeList.getItemCount(); i++) {
						String type = javaTypeList.getItem(i);
						if (javaType.equals(type))
							javaTypeList.select(i);
					}

				}
				for (int i = 0; i < paramModelCombo.getItemCount(); i++) {
					String type = paramModelCombo.getItem(i);
					if (paramModel.equals(type))
						paramModelCombo.select(i);
				}
			}
		} else {
			List<String> list = javaTypeMap.get("1");
			javaTypeList.removeAll();
			for (int i = 0; i < list.size(); i++) {
				String tmp = list.get(i);
				javaTypeList.add(tmp);
			}
			javaTypeList.setSelection(0);
			paramModelCombo.select(0);

		}
//		if(!newFlag)
//			parameterNameText.setEnabled(false);
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		// getShell().setSize(400, 420);
		CommonTool.centerShell(getShell());
		if (newFlag)
			getShell().setText(Messages.titleAddFuncParamDialog);
		else
			getShell().setText(Messages.titleEditFuncParamDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, com.cubrid.cubridmanager.ui.common.Messages.btnOK, false);
		String msg = getValidInput();
		setMessage(msg);
		if (msg != null && !"".equals(msg))
			getButton(IDialogConstants.OK_ID).setEnabled(false);
		else
			getButton(IDialogConstants.OK_ID).setEnabled(true);

		createButton(parent, IDialogConstants.CANCEL_ID, com.cubrid.cubridmanager.ui.common.Messages.btnCancel, false);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			if (!verify()) {
				return;
			} else {

				if (model == null)
					return;
				String name = parameterNameText.getText();
				String sqlType = paramTypeCombo.getText();
				String paramModel = paramModelCombo.getText();
				model.put("0", name);
				model.put("1", sqlType);
				if (!"4".equals(paramTypeCombo.getData()) && !"-1".equals(paramTypeCombo.getData())) {
					String[] javaType = javaTypeList.getSelection();
					model.put("2", javaType[0]);
				} else {
					String javaType = javaTypeText.getText();
					model.put("2", javaType);
				}
				model.put("3", paramModel);
			}
		}
		super.buttonPressed(buttonId);
	}

	private boolean verify() {
		String msg = getValidInput();
		if (msg != null && "".equals(msg)) {
			CommonTool.openErrorBox(getShell(), msg);
			return false;
		}
		setErrorMessage(null);
		return true;
	}

	@Override
	public boolean isHelpAvailable() {
		return true;
	}

	@Override
	protected int getShellStyle() {
		return super.getShellStyle() | SWT.RESIZE;
	}

	public CubridDatabase getDatabase() {
		return database;
	}

	public void setDatabase(CubridDatabase database) {
		this.database = database;
	}

	private void setValidMessage() {
		String msg = getValidInput();
		setErrorMessage(msg);
		if (getButton(IDialogConstants.OK_ID) != null)
			if (msg != null && !"".equals(msg))
				getButton(IDialogConstants.OK_ID).setEnabled(false);
			else
				getButton(IDialogConstants.OK_ID).setEnabled(true);
	}

	private String getValidInput() {

		String paramsName = parameterNameText.getText();
		if (newFlag) {
			if (paramsName == null || "".equals(paramsName))
				return Messages.errInputParameterName;
			if (paramsName.length() > ValidateUtil.MAX_NAME_LENGTH)
				return Messages.bind(Messages.errInputParameterNameLength, ValidateUtil.MAX_NAME_LENGTH);
			if (newFlag)
				for (Map<String, String> map : paramList) {
					String name = map.get("0");
					if (name != null && name.equalsIgnoreCase(paramsName))
						return Messages.errInputParameterNameDuplicate;
				}

			String str = CommonTool.validateCheckInIdentifier(paramsName);
			if (str != null && !str.equals("")) {
				return Messages.bind(Messages.errInputParameterNameValid, str);
			}
		}
		String paramType = paramTypeCombo.getText();

		if (paramType == null || "".equals(paramType))
			return Messages.errInputSqlType;
		// if (!sqlTypeMap.containsKey(paramType.toUpperCase()))
		// return "Please input the valid sql type";
		if (!"4".equals(paramTypeCombo.getData()) && !"-1".equals(paramTypeCombo.getData())) {
			String[] javaType = javaTypeList.getSelection();
			if (javaType.length != 1)
				return Messages.msgSelectJavaConfirm;
		} else {
			String javaType = javaTypeText.getText();
			if (javaType == null || "".equals(javaType))
				return Messages.msgSelectSpecialJavaConfirm;
		}

		return null;

	}

	private void setJavaTypeEnable(boolean flag) {
		javaTypeList.setEnabled(flag);
		javaTypeList.setSelection(flag ? 0 : -1);
		javaTypeLabel.setEnabled(flag);
		javaTypeLabel2.setEnabled(!flag);
		javaTypeText.setEnabled(!flag);
		setJavaTypeList();
	}

	private void setJavaTypeList() {
		String sqlType = paramTypeCombo.getText();
		if (sqlType == null || sqlType.equals(""))
			return;
		String level = sqlTypeMap.get(sqlType.toUpperCase());
		if (level == null || level.equals("")) {
			paramTypeCombo.setData("-1");
			return;
		}
		paramTypeCombo.setData(level);
		List<String> list = javaTypeMap.get(level);
		javaTypeList.removeAll();
		for (String tmp : list)
			javaTypeList.add(tmp);
		javaTypeList.select(0);
	}
}
