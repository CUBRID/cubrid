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
package com.cubrid.cubridmanager.ui.cubrid.serial.dialog;

import java.math.BigInteger;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.serial.model.SerialInfo;
import com.cubrid.cubridmanager.core.cubrid.serial.task.CreateOrEditSerialTask;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.serial.Messages;
import com.cubrid.cubridmanager.ui.query.format.SqlFormattingStrategy;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * The dialog is responsible to collect serial information.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-3 created by pangqiren
 */
public class CreateOrEditSerialDialog extends
		CMTitleAreaDialog implements
		ModifyListener {
	private Text serialNameText = null;
	private Text startValText = null;
	private Text incrementValText = null;
	private Text maxValText = null;
	private Text minValText = null;
	private Button cycleButton = null;
	private ISchemaNode currentNode = null;
	private TabFolder tabFolder = null;
	private StyledText sqlScriptText = null;
	private static SqlFormattingStrategy formator = new SqlFormattingStrategy();

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public CreateOrEditSerialDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		getHelpSystem().setHelp(parent,
				CubridManagerHelpContextIDs.databaseSerial);

		Composite parentComp = (Composite) super.createDialogArea(parent);
		Composite composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.numColumns = 2;
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);

		tabFolder = new TabFolder(composite, SWT.NONE);
		tabFolder.setLayoutData(new GridData(GridData.FILL_BOTH));
		layout = new GridLayout();
		tabFolder.setLayout(layout);

		TabItem item = new TabItem(tabFolder, SWT.NONE);
		item.setText(Messages.grpGeneral);
		composite = createGeneralInfoComp();
		item.setControl(composite);

		item = new TabItem(tabFolder, SWT.NONE);
		item.setText(Messages.grpSqlScript);
		composite = createSqlScriptComposit();
		item.setControl(composite);

		tabFolder.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				sqlScriptText.setText(getSQLScript());
			}
		});
		initial();
		if (currentNode != null
				&& currentNode.getType() == CubridNodeType.SERIAL) {
			setTitle(Messages.titleEditSerialDialog);
			setMessage(Messages.msgEditSerialDialog);
		} else {
			setTitle(Messages.titleCreateSerialDialog);
			setMessage(Messages.msgCreateSerialDialog);
		}

		return parentComp;
	}

	private Composite createGeneralInfoComp() {
		Composite composite = new Composite(tabFolder, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.numColumns = 2;
		composite.setLayout(layout);

		Label serialNameLabel = new Label(composite, SWT.LEFT);
		serialNameLabel.setText(Messages.lblSerialName);
		serialNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		serialNameText = new Text(composite, SWT.LEFT | SWT.BORDER);

		serialNameText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));

		Label startValLabel = new Label(composite, SWT.LEFT);
		if (currentNode != null
				&& currentNode.getType() == CubridNodeType.SERIAL_FOLDER)
			startValLabel.setText(Messages.lblStartValue);
		else
			startValLabel.setText(Messages.lblCurrentValue);
		startValLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		startValText = new Text(composite, SWT.LEFT | SWT.BORDER);
		startValText.setTextLimit(38);

		startValText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));

		Label incrementValLabel = new Label(composite, SWT.LEFT);
		incrementValLabel.setText(Messages.lblIncrementValue);
		incrementValLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		incrementValText = new Text(composite, SWT.LEFT | SWT.BORDER);
		incrementValText.setTextLimit(38);
		incrementValText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));

		Label minValLabel = new Label(composite, SWT.LEFT);
		minValLabel.setText(Messages.lblMinValue);
		minValLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		minValText = new Text(composite, SWT.LEFT | SWT.BORDER);
		minValText.setTextLimit(38);
		minValText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));

		Label maxValLabel = new Label(composite, SWT.LEFT);
		maxValLabel.setText(Messages.lblMaxValue);
		maxValLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		maxValText = new Text(composite, SWT.LEFT | SWT.BORDER);
		maxValText.setTextLimit(38);
		maxValText.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 1, 1, -1, -1));

		cycleButton = new Button(composite, SWT.LEFT | SWT.CHECK);
		cycleButton.setText(Messages.btnCycle);
		cycleButton.setLayoutData(CommonTool.createGridData(
				GridData.FILL_HORIZONTAL, 2, 1, -1, -1));
		cycleButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				valid();
			}
		});
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

	private String getSQLScript() {
		StringBuffer sb = new StringBuffer();
		if (currentNode != null
				&& currentNode.getType() == CubridNodeType.SERIAL) {
			sb.append("ALTER").append(" SERIAL ");
			String serialName = serialNameText.getText();
			if (serialName == null)
				serialName = "";
			sb.append("\"" + serialName + "\"");
			String incrementValue = incrementValText.getText();
			String minxValue = minValText.getText();
			String maxValue = maxValText.getText();
			boolean isCycle = cycleButton.getSelection();
			if (incrementValue.trim().length() > 0) {
				sb.append(" increment by ").append(incrementValue);
			}
			if (minxValue.trim().length() > 0) {
				sb.append(" minvalue ").append(minxValue);
			}
			if (maxValue.trim().length() > 0) {
				sb.append(" maxvalue ").append(maxValue);
			}
			if (isCycle) {
				sb.append(" cycle");
			} else {
				sb.append(" nocycle");
			}
		} else {
			sb.append("CREATE").append(" SERIAL ");
			String serialName = serialNameText.getText();
			if (serialName == null)
				serialName = "";
			sb.append("\"" + serialName + "\"");
			String startedValue = startValText.getText();
			String incrementValue = incrementValText.getText();
			String minxValue = minValText.getText();
			String maxValue = maxValText.getText();
			boolean isCycle = cycleButton.getSelection();
			if (startedValue.trim().length() > 0) {
				sb.append(" start with ").append(startedValue);
			}
			if (incrementValue.trim().length() > 0) {
				sb.append(" increment by ").append(incrementValue);
			}
			if (minxValue.trim().length() > 0) {
				sb.append(" minvalue ").append(minxValue);
			}
			if (maxValue.trim().length() > 0) {
				sb.append(" maxvalue ").append(maxValue);
			}
			if (isCycle) {
				sb.append(" cycle");
			} else {
				sb.append(" nocycle");
			}
		}
		return formatSql(sb.toString());
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

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		getShell().setSize(500, 600);
		CommonTool.centerShell(getShell());
		if (currentNode != null
				&& currentNode.getType() == CubridNodeType.SERIAL) {
			getShell().setText(Messages.titleEditSerialDialog);
		} else {
			getShell().setText(Messages.titleCreateSerialDialog);
		}
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnOK, true);
		getButton(IDialogConstants.OK_ID).setEnabled(false);
		createButton(parent, IDialogConstants.CANCEL_ID,
				com.cubrid.cubridmanager.ui.common.Messages.btnCancel, true);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			createSerial(buttonId);
		} else
			super.buttonPressed(buttonId);
	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {
		if (currentNode != null
				&& (currentNode.getType() == CubridNodeType.SERIAL)) {
			SerialInfo serialInfo = (SerialInfo) currentNode.getAdapter(SerialInfo.class);
			if (serialInfo != null) {
				serialNameText.setEditable(false);
				serialNameText.setText(serialInfo.getName());
				startValText.setEditable(false);
				startValText.setText(String.valueOf(serialInfo.getCurrentValue()));
				incrementValText.setText(serialInfo.getIncrementValue());
				minValText.setText(serialInfo.getMinValue());
				maxValText.setText(serialInfo.getMaxValue());
				cycleButton.setSelection(serialInfo.isCycle());
			}
		}
		if (currentNode != null
				&& currentNode.getType() == CubridNodeType.SERIAL_FOLDER) {
			startValText.setText("0");
			incrementValText.setText("1");
			minValText.setText("0");
			maxValText.setText("10000000000000000000000000000000000000");
			cycleButton.setSelection(false);
		}
		serialNameText.addModifyListener(this);
		startValText.addModifyListener(this);
		incrementValText.addModifyListener(this);
		minValText.addModifyListener(this);
		maxValText.addModifyListener(this);
	}

	/**
	 * 
	 * Execute task and create serial
	 * 
	 * @param buttonId
	 */
	private void createSerial(final int buttonId) {
		final String name = serialNameText.getText();
		final String startVal = startValText.getText();
		final String incrementVal = incrementValText.getText();
		final String minVal = minValText.getText();
		final String maxVal = maxValText.getText();
		final boolean isCycle = cycleButton.getSelection();
		TaskExecutor taskExcutor = new TaskExecutor() {
			public boolean exec(final IProgressMonitor monitor) {
				Display display = getShell().getDisplay();
				if (monitor.isCanceled()) {
					return false;
				}
				for (ITask task : taskList) {
					if (task instanceof CreateOrEditSerialTask) {
						CreateOrEditSerialTask createSerialTask = (CreateOrEditSerialTask) task;
						if (currentNode != null
								&& currentNode.getType() == CubridNodeType.SERIAL) {
							createSerialTask.editSerial(name, startVal,
									incrementVal, maxVal, minVal, isCycle);
						} else {
							createSerialTask.createSerial(name, startVal,
									incrementVal, maxVal, minVal, isCycle);
						}
					}
					final String msg = task.getErrorMsg();
					if (monitor.isCanceled()) {
						return false;
					}
					if (msg != null && msg.length() > 0
							&& !monitor.isCanceled()) {
						display.syncExec(new Runnable() {
							public void run() {
								CommonTool.openErrorBox(getShell(), msg);
							}
						});
						return false;
					}
					if (monitor.isCanceled()) {
						return false;
					}
				}
				if (!monitor.isCanceled()) {
					display.syncExec(new Runnable() {
						public void run() {
							setReturnCode(buttonId);
							close();
						}
					});
				}
				return true;
			}
		};
		CubridDatabase database = currentNode.getDatabase();
		DatabaseInfo databaseInfo = database.getDatabaseInfo();
		CreateOrEditSerialTask task = new CreateOrEditSerialTask(databaseInfo);
		taskExcutor.addTask(task);
		new ExecTaskWithProgress(taskExcutor).exec(true, true);
	}

	private void valid() {
		String name = serialNameText.getText();
		String startVal = startValText.getText();
		String incrementVal = incrementValText.getText();
		String minVal = minValText.getText();
		String maxVal = maxValText.getText();
		boolean isValidName = name.trim().length() > 0 && name.indexOf(" ") < 0
				&& name.indexOf("\"") < 0 && name.indexOf("\'") < 0;
		boolean isExist = false;
		if (currentNode != null
				&& currentNode.getType() == CubridNodeType.SERIAL_FOLDER) {
			if (currentNode.getChild(currentNode.getId()
					+ ICubridNodeLoader.NODE_SEPARATOR + name) != null) {
				isExist = true;
			}
		}
		boolean isValidStartVal = startVal.trim().length() > 0;
		boolean isTooLongStartVal = false;
		if (isValidStartVal) {
			isValidStartVal = ValidateUtil.isInteger(startVal);
			if (isValidStartVal
					&& startVal.trim().length() == 38
					&& (!startVal.trim().equals(
							"10000000000000000000000000000000000000") && !startVal.trim().equals(
							"-1000000000000000000000000000000000000"))) {
				isTooLongStartVal = true;
			}
		}
		boolean isValidIncrementVal = incrementVal.trim().length() > 0;
		boolean isTooLongIncrementVal = false;
		if (isValidIncrementVal) {
			isValidIncrementVal = ValidateUtil.isInteger(incrementVal);
			if (isValidIncrementVal
					&& incrementVal.trim().length() == 38
					&& (!incrementVal.trim().equals(
							"10000000000000000000000000000000000000") && !incrementVal.trim().equals(
							"-1000000000000000000000000000000000000"))) {
				isTooLongIncrementVal = true;
			}
		}
		boolean isValidMinVal = minVal.trim().length() > 0;
		boolean isTooLongMinVal = false;
		if (isValidMinVal) {
			isValidMinVal = ValidateUtil.isInteger(minVal);
			if (isValidMinVal
					&& minVal.trim().length() == 38
					&& (!minVal.trim().equals(
							"10000000000000000000000000000000000000") && !minVal.trim().equals(
							"-1000000000000000000000000000000000000"))) {
				isTooLongMinVal = true;
			}
		}
		boolean isValidMaxVal = maxVal.trim().length() > 0;
		boolean isTooLongMaxVal = false;
		if (isValidMaxVal) {
			isValidMaxVal = ValidateUtil.isInteger(maxVal);
			if (isValidMaxVal
					&& maxVal.trim().length() == 38
					&& (!maxVal.trim().equals(
							"10000000000000000000000000000000000000") && !maxVal.trim().equals(
							"-1000000000000000000000000000000000000"))) {
				isTooLongMaxVal = true;
			}
		}
		boolean isValidValue = true;
		if (isValidStartVal && !isTooLongStartVal && isValidMinVal
				&& !isTooLongMinVal && isValidMaxVal && !isTooLongMaxVal) {
			BigInteger startBigVal = new BigInteger(startVal);
			BigInteger minBigVal = new BigInteger(minVal);
			BigInteger maxBigVal = new BigInteger(maxVal);
			if (startBigVal.compareTo(minBigVal) >= 0
					&& maxBigVal.compareTo(startBigVal) >= 0) {
				isValidValue = true;
			} else {
				isValidValue = false;
			}
		}
		if (!isValidName) {
			setErrorMessage(Messages.errSerialName);
		} else if (isExist) {
			setErrorMessage(Messages.errSerialExist);
		} else if (!isValidStartVal || isTooLongStartVal) {
			if (currentNode != null
					&& currentNode.getType() == CubridNodeType.SERIAL_FOLDER) {
				setErrorMessage(Messages.bind(Messages.errStartValue,
						Messages.msgStartValue));
			} else {
				setErrorMessage(Messages.bind(Messages.errStartValue,
						Messages.msgCurrentValue));
			}
		} else if (!isValidIncrementVal || isTooLongIncrementVal) {
			setErrorMessage(Messages.errIncrementValue);
		} else if (!isValidMinVal || isTooLongMinVal) {
			setErrorMessage(Messages.errMinValue);
		} else if (!isValidMaxVal || isTooLongMaxVal) {
			setErrorMessage(Messages.errMaxValue);
		} else if (!isValidValue) {
			if (currentNode != null
					&& currentNode.getType() == CubridNodeType.SERIAL_FOLDER) {
				setErrorMessage(Messages.bind(Messages.errValue,
						Messages.msgStartValue));
			} else {
				setErrorMessage(Messages.bind(Messages.errValue,
						Messages.msgCurrentValue));
			}
		} else {
			setErrorMessage(null);
		}
		boolean isValid = isValidName && !isExist && isValidStartVal
				&& !isTooLongStartVal && isValidIncrementVal && isValidMinVal
				&& isValidMaxVal && !isTooLongIncrementVal && !isTooLongMinVal
				&& !isTooLongMaxVal && isValidValue;
		if (getButton(IDialogConstants.OK_ID) != null)
			getButton(IDialogConstants.OK_ID).setEnabled(isValid);
	}

	public void modifyText(ModifyEvent e) {
		valid();
	}

	/**
	 * 
	 * Set Cubrid node
	 * 
	 * @param schemaNode
	 */
	public void setCurrentNode(ISchemaNode schemaNode) {
		currentNode = schemaNode;
	}
}
