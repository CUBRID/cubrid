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
package com.cubrid.cubridmanager.ui.cubrid.trigger.dialog;

import java.sql.SQLException;
import java.util.List;

import org.apache.log4j.Logger;
import org.eclipse.jface.dialogs.IDialogConstants;
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
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetAllAttrTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetTablesTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerAction;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerActionTime;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerConditionTime;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerEvent;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.TriggerStatus;
import com.cubrid.cubridmanager.core.cubrid.trigger.model.Trigger;
import com.cubrid.cubridmanager.core.cubrid.trigger.model.TriggerDDL;
import com.cubrid.cubridmanager.core.cubrid.trigger.task.AddTriggerTask;
import com.cubrid.cubridmanager.core.cubrid.trigger.task.AlterTriggerTask;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.table.dialog.ImportDataDialog;
import com.cubrid.cubridmanager.ui.cubrid.trigger.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.SWTResourceManager;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

public class CreateTriggerDialog extends
		CMTitleAreaDialog {
	private StyledText sqlText;
	Color white = SWTResourceManager.getColor(SWT.COLOR_WHITE);
	private static final Logger logger = LogUtil.getLogger(ImportDataDialog.class);
	private Composite composite;
	private Text triggerNameText = null;
	private Combo triggerTargetTableCombo = null;
	private Combo triggerTargetColumnCombo = null;
	private Text triggerConditionText = null;
	private Text triggerActionText = null;
	private Text triggerPriorityText = null;

	private CubridDatabase database;
	private Button[] conditionTimeBTNs;
	private Button[] eventTypeBTNs;
	private Button[] actionTimeBTNs;
	private Button[] actionTypeBTNs;
	private Button[] statusBTNs;

	private Trigger trigger = null;
	private TabFolder tabFolder;
	public static int AlterTriggerOK_ID = 100;

	private void alterInit() {

		if (trigger == null) {
			return;
		}
		setMessage(Messages.triggerAlterMSG);
		setTitle(Messages.triggerAlterMSGTitle);

		triggerNameText.setText(trigger.getName());
		String table = trigger.getTarget_class();

		if (null != table) {
			if (!getTableList().contains(table)) {
				triggerTargetTableCombo.add(table);
			}
			triggerTargetTableCombo.setText(table);
		} else {
			triggerTargetTableCombo.setText("");
		}

		String column = trigger.getTarget_att();
		if (null != column) {
			triggerTargetColumnCombo.add(column);
			triggerTargetColumnCombo.setText(column);
		} else {
			triggerTargetColumnCombo.setText("");
		}

		String condition = trigger.getCondition();
		if (null != condition) {
			triggerConditionText.setText(condition);
		} else {
			triggerConditionText.setText("");
		}

		String action = trigger.getAction();

		if (null != action) {
			triggerActionText.setText(action);
		} else {
			triggerActionText.setText("");
		}

		triggerNameText.setEnabled(false);
		triggerTargetTableCombo.setEnabled(false);
		triggerTargetColumnCombo.setEnabled(false);
		triggerConditionText.setEnabled(false);
		triggerActionText.setEnabled(false);
		String conditionTime = trigger.getConditionTime();
		setConditionTime(conditionTime);
		setActionTime(trigger.getActionTime());
		setActionType(trigger.getActionType());

		setEventType(trigger.getEventType());
		setStatus(trigger.getStatus());
		for (Button b : conditionTimeBTNs) {
			b.setEnabled(false);
		}
		for (Button b : eventTypeBTNs) {
			b.setEnabled(false);
		}
		for (Button b : actionTimeBTNs) {
			b.setEnabled(false);
		}
		for (Button b : actionTypeBTNs) {
			b.setEnabled(false);
		}
		triggerPriorityText.setText(trigger.getPriority());
	}

	private void addListener() {
		SelectionAdapter eventTypeSelectionAdapter = new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				String curtype = getEventType();
				if (curtype.equals(Messages.eventTypeCommit)
						|| curtype.equals(Messages.eventTypeRollback)) {
					triggerTargetTableCombo.setText("");
					triggerTargetColumnCombo.setText("");
					triggerConditionText.setText("");
					triggerTargetTableCombo.setEnabled(false);
					triggerTargetColumnCombo.setEnabled(false);
					triggerConditionText.setEnabled(false);
				} else {
					triggerTargetTableCombo.setEnabled(true);
					triggerTargetColumnCombo.setEnabled(true);
					triggerConditionText.setEnabled(true);
				}
				if (curtype.equals(Messages.eventTypeInsert)
						|| curtype.equals(Messages.eventTypeSInsert)
						|| curtype.equals(Messages.eventTypeDelete)
						|| curtype.equals(Messages.eventTypeSDelete)) {

					triggerTargetTableCombo.setEnabled(true);
					triggerConditionText.setEnabled(true);

					triggerTargetColumnCombo.setText("");
					triggerTargetColumnCombo.setEnabled(false);
				} else if (curtype.equals(Messages.eventTypeUpdate)
						|| curtype.equals(Messages.eventTypeSUpdate)) {
					triggerTargetTableCombo.setEnabled(true);
					triggerConditionText.setEnabled(true);
					triggerTargetColumnCombo.setEnabled(true);
				}
				validateAll();
			}
		};
		for (Button b : eventTypeBTNs) {
			b.addSelectionListener(eventTypeSelectionAdapter);
		}
		SelectionAdapter actionTypeSelectionAdapter = new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				String curtype = getActionType();
				if (curtype.equals(Messages.actionTypeReject)
						|| curtype.equals(Messages.actionTypeInvalidateTransaction)) {
					triggerActionText.setEnabled(false);
				} else {
					triggerActionText.setEnabled(true);
					triggerActionText.setFocus();
				}
				validateAll();
			}
		};
		for (Button b : actionTypeBTNs) {
			b.addSelectionListener(actionTypeSelectionAdapter);
		}
		triggerTargetTableCombo.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				int index = triggerTargetTableCombo.getSelectionIndex();
				String table = triggerTargetTableCombo.getItem(index);
				try {
					addColumns(table);
				} catch (SQLException e1) {
					setErrorMessage(e1.getErrorCode() + ":" + e1.getMessage());
				}
				validateAll();
			}
		});
		if (trigger == null) {
			triggerTargetTableCombo.addModifyListener(new ModifyListener() {
				public void modifyText(ModifyEvent e) {
					boolean valid = validateEventType();
					if (valid) {
						validateAll();
					} else {
						changeOKButtonStatus(false);
					}
					triggerTargetTableCombo.setFocus();
				}
			});
		}

		if (trigger == null) {
			triggerNameText.addModifyListener(new ModifyListener() {
				public void modifyText(ModifyEvent e) {
					boolean valid = validateTriggerName();
					if (valid) {
						validateAll();
					} else {
						changeOKButtonStatus(false);
					}
					triggerNameText.setFocus();
				}
			});
		}
		triggerPriorityText.addModifyListener(new ModifyListener() {
			public void modifyText(ModifyEvent e) {
				boolean valid = validatePriority();
				if (valid) {
					validateAll();
				} else {
					changeOKButtonStatus(false);
				}
				triggerPriorityText.setFocus();
			}
		});
		tabFolder.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(final SelectionEvent e) {
				if (tabFolder.getSelection()[0].getText().equals(
						Messages.infoSQLScriptTab)) {
					StringBuffer sql = new StringBuffer();
					Trigger newTrigger = getNewTrigger();
					if (null == trigger) {
						sql.append(TriggerDDL.getDDL(newTrigger));
					} else {
						sql.append(TriggerDDL.getDDL(trigger));
						sql.append(CommonTool.getLineSeparator());
						sql.append(CommonTool.getLineSeparator());
						sql.append(CommonTool.getLineSeparator());
						sql.append(TriggerDDL.getAlterDDL(trigger, newTrigger));
					}
					sqlText.setText(sql.toString());
				}
			}
		});
	}

	private void createInit() {

		triggerNameText.setToolTipText(Messages.triggerToolTipName);
		triggerTargetTableCombo.setToolTipText(Messages.triggerToolTipEventTarget);
		triggerTargetColumnCombo.setToolTipText(Messages.triggerToolTipEventTarget);
		triggerConditionText.setToolTipText(Messages.triggerToolTipCondition);
		triggerActionText.setToolTipText(Messages.triggerToolTipActivity);
		triggerPriorityText.setToolTipText(Messages.triggerToolTipPriority);

		for (Button b : conditionTimeBTNs) {
			b.setToolTipText(Messages.triggerToolTipConditionTime);
		}
		for (Button b : eventTypeBTNs) {
			b.setToolTipText(Messages.triggerToolTipDatabaseEventType);
		}
		for (Button b : actionTimeBTNs) {
			b.setToolTipText(Messages.triggerToolTipDelayedTime);
		}
		for (Button b : actionTypeBTNs) {
			b.setToolTipText(Messages.triggerToolTipActivityType);
		}
		for (Button b : statusBTNs) {
			b.setToolTipText(Messages.triggerToolTipStatus);
		}

		triggerTargetTableCombo.setVisibleItemCount(20);
		triggerTargetColumnCombo.setVisibleItemCount(20);
		try {
			addTables();
		} catch (SQLException e1) {
			setErrorMessage(e1.getErrorCode() + ":" + e1.getMessage());
		}
		conditionTimeBTNs[0].setSelection(true);//Before

		eventTypeBTNs[0].setSelection(true);//insert
		triggerTargetColumnCombo.setEnabled(false);

		actionTimeBTNs[0].setSelection(true);//default
		actionTypeBTNs[0].setSelection(true);//reject
		triggerActionText.setEnabled(false);
		statusBTNs[0].setSelection(true);//active		
		triggerNameText.setFocus();

	}

	public CreateTriggerDialog(Shell parentShell, CubridDatabase database) {
		super(parentShell);
		this.database = database;
	}

	public CreateTriggerDialog(Shell parentShell, CubridDatabase database,
			Trigger trigger) {
		super(parentShell);
		this.database = database;
		this.trigger = trigger;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		if (null == trigger) {
			getShell().setText(Messages.newTriggerShellTitle);
		} else {
			getShell().setText(Messages.alterTriggerShellTitle);
			getShell().pack();
		}
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		if (null == trigger) {
			createButton(parent, IDialogConstants.OK_ID, Messages.okBTN, false);
			getButton(IDialogConstants.OK_ID).setEnabled(false);
		} else {
			createButton(parent, AlterTriggerOK_ID, Messages.okBTN, false);
		}
		createButton(parent, IDialogConstants.CANCEL_ID, Messages.cancleBTN,
				false);
	}

	private Trigger getNewTrigger() {
		Trigger newTrigger = new Trigger();
		String triggerName = triggerNameText.getText();

		String eventType = getEventType();
		String triggerEventTargetTable = triggerTargetTableCombo.getText().trim();
		String triggerEventTargetColumn = triggerTargetColumnCombo.getText().trim();

		String conditionTime = getConditionTime();
		String triggerActionType = getActionType();
		String triggerAction = triggerActionText.getText().trim();
		String CR = "\r";
		String NL = "\n";
		triggerAction = triggerAction.replaceAll(CR, "");
		triggerAction = triggerAction.replaceAll(NL, " ");

		String triggerCondition = triggerConditionText.getText().trim();
		triggerCondition = triggerCondition.replaceAll(CR, "");
		triggerCondition = triggerCondition.replaceAll(NL, " ");

		String actionTime = getActionTime();
		String triggerStatus = this.getStatus();
		String strPriority = triggerPriorityText.getText();

		newTrigger.setName(triggerName);
		newTrigger.setEventType(eventType);
		newTrigger.setTarget_class(triggerEventTargetTable);
		newTrigger.setTarget_att(triggerEventTargetColumn);
		newTrigger.setConditionTime(conditionTime);

		newTrigger.setAction(triggerAction);
		newTrigger.setActionType(triggerActionType);

		newTrigger.setActionTime(actionTime);
		newTrigger.setCondition(triggerCondition);
		newTrigger.setStatus(triggerStatus);
		newTrigger.setPriority(strPriority);
		return newTrigger;
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			try {
				setErrorMessage(null);

				String triggerName = triggerNameText.getText();

				String eventType = getEventType();
				String triggerEventTargetTable = triggerTargetTableCombo.getText().trim();
				String triggerEventTargetColumn = triggerTargetColumnCombo.getText().trim();
				AddTriggerTask task = new AddTriggerTask(
						database.getServer().getServerInfo());
				task.setDbName(database.getName());
				task.setTriggerName(triggerName);

				String conditionTime = getConditionTime();
				assert (null != conditionTime);
				task.setConditionTime(TriggerConditionTime.eval(conditionTime));

				assert (null != eventType);
				task.setEventType(TriggerEvent.eval(eventType));

				String triggerActionType = getActionType();
				String triggerAction = triggerActionText.getText().trim();
				String CR = "\r";
				String NL = "\n";
				triggerAction = triggerAction.replaceAll(CR, "");
				triggerAction = triggerAction.replaceAll(NL, " ");
				task.setAction(TriggerAction.eval(triggerActionType),
						triggerAction);

				if (triggerEventTargetTable.length() > 0) {
					if (triggerEventTargetColumn.length() > 0) {
						task.setEventTarget(triggerEventTargetTable + "("
								+ triggerEventTargetColumn + ")");
					} else {
						task.setEventTarget(triggerEventTargetTable);
					}
				}

				String actionTime = getActionTime();
				assert (null != actionTime);
				if (!actionTime.equals(Messages.actionTimeDefault)) { // action time selected
					task.setActionTime(TriggerActionTime.eval(actionTime));
				}

				String triggerCondition = triggerConditionText.getText().trim();
				triggerCondition = triggerCondition.replaceAll(CR, "");
				triggerCondition = triggerCondition.replaceAll(NL, " ");

				task.setCondition(triggerCondition);

				String triggerStatus = this.getStatus();
				assert (null != triggerStatus);
				task.setStatus(TriggerStatus.eval(triggerStatus));

				String strPriority = triggerPriorityText.getText();
				task.setPriority(strPriority);

				TaskExecutor taskExecutor = new CommonTaskExec();
				taskExecutor.addTask(task);
				new ExecTaskWithProgress(taskExecutor).exec();
				if (task.isSuccess()) {
					CommonTool.openInformationBox(Messages.MSG_INFORMATION,
							Messages.newTriggerSuccess);
					this.getShell().dispose();
					this.close();
				} else {
					//					CommonTool.openErrorBox(getShell(), task.getErrorMsg());
					return;
				}
			} catch (Exception e1) {
				//				CommonTool.ErrorBox(sShell, e1.getMessage());
				logger.error(e1);

			} catch (Error e1) {
				//				CommonTool.ErrorBox(sShell, e1.getMessage());
				logger.error(e1);

			}
		} else if (buttonId == AlterTriggerOK_ID) {
			validateAll();
			AlterTriggerTask task = new AlterTriggerTask(
					database.getServer().getServerInfo());
			task.setDbName(database.getName());
			task.setTriggerName(trigger.getName());

			String triggerStatus = this.getStatus();
			assert (null != triggerStatus);
			task.setStatus(TriggerStatus.eval(triggerStatus));

			String strPriority = triggerPriorityText.getText();
			task.setPriority(strPriority);
			//			task.execute();
			TaskExecutor taskExecutor = new CommonTaskExec();
			taskExecutor.addTask(task);
			new ExecTaskWithProgress(taskExecutor).exec();
			if (task.isSuccess()) {
				CommonTool.openInformationBox(Messages.MSG_INFORMATION,
						Messages.alterTriggerSuccess);
				this.getShell().dispose();
				this.close();
			} else {
				return;
			}

		} else if (buttonId == IDialogConstants.CANCEL_ID) {
			this.getShell().dispose();
			this.close();
		}
		setReturnCode(buttonId);
		super.buttonPressed(buttonId);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp,
				CubridManagerHelpContextIDs.databaseTrigger);
		parentComp.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, true));

		Composite tabComposite = new Composite(parentComp, SWT.NONE);
		final GridData gd_composite = new GridData(SWT.FILL, SWT.FILL, true,
				true);
		tabComposite.setLayoutData(gd_composite);

		GridLayout tabCompositeLayout = new GridLayout();
		tabCompositeLayout.numColumns = 1;
		tabCompositeLayout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		tabCompositeLayout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		tabCompositeLayout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		tabCompositeLayout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		tabCompositeLayout.numColumns = 1;
		tabComposite.setLayout(tabCompositeLayout);

		tabFolder = new TabFolder(tabComposite, SWT.NONE);

		tabFolder.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, true));

		final TabItem triggerTabItem = new TabItem(tabFolder, SWT.NONE);
		triggerTabItem.setText(Messages.infoTriggerTab);

		composite = new Composite(tabFolder, SWT.NONE);

		//		composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		triggerTabItem.setControl(composite);
		GridLayout layout = new GridLayout();
		layout.numColumns = 4;
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);

		//Trigger Name
		Label label1 = new Label(composite, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.triggerName);
		label1.setLayoutData(new GridData(100, SWT.DEFAULT));

		GridData gridData2 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.CENTER, true, false);
		triggerNameText = new Text(composite, SWT.BORDER);
		triggerNameText.setLayoutData(gridData2);

		Composite firstLineComposite = new Composite(composite, SWT.NONE);
		firstLineComposite.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true,
				false, 2, 1));
		GridLayout layout2 = new GridLayout();
		layout2.numColumns = 4;
		firstLineComposite.setLayout(layout);

		//Event target	table
		Label label3 = new Label(firstLineComposite, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.triggerEventTargetTable);
		label3.setLayoutData(new GridData(85, SWT.DEFAULT));

		GridData gridData6 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.FILL, true, false);

		triggerTargetTableCombo = new Combo(firstLineComposite, SWT.BORDER
				| SWT.READ_ONLY);
		triggerTargetTableCombo.setLayoutData(gridData6);

		//Event target column	
		Label label4 = new Label(firstLineComposite, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.triggerEventTargetColumn);
		label4.setLayoutData(new GridData(50, SWT.DEFAULT));

		GridData gridData7 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.FILL, true, false);

		triggerTargetColumnCombo = new Combo(firstLineComposite, SWT.BORDER
				| SWT.READ_ONLY);
		triggerTargetColumnCombo.setLayoutData(gridData7);

		createGroupCreateTrigger();
		createGroup1();
		final Label sqlStatementsOrLabel = new Label(composite, SWT.NONE);
		sqlStatementsOrLabel.setLayoutData(new GridData(SWT.LEFT, SWT.CENTER,
				false, false, 4, 1));
		sqlStatementsOrLabel.setText(Messages.sqlStatementMSG);

		triggerActionText = new Text(composite, SWT.BORDER | SWT.MULTI
				| SWT.WRAP | SWT.V_SCROLL);
		final GridData gd_eDIT_TRIGGER_ACTION = new GridData(SWT.FILL,
				SWT.FILL, true, true, 6, 1);
		gd_eDIT_TRIGGER_ACTION.heightHint = 157;
		triggerActionText.setLayoutData(gd_eDIT_TRIGGER_ACTION);

		final TabItem sqlScriptTabItem = new TabItem(tabFolder, SWT.NONE);
		sqlScriptTabItem.setText(Messages.infoSQLScriptTab);

		final Composite composite_1 = new Composite(tabFolder, SWT.NONE);
		composite_1.setLayout(new GridLayout());
		sqlScriptTabItem.setControl(composite_1);

		sqlText = new StyledText(composite_1, SWT.WRAP | SWT.V_SCROLL
				| SWT.READ_ONLY | SWT.H_SCROLL | SWT.BORDER);
		CommonTool.registerContextMenu(sqlText, false);
		sqlText.setBackground(white);
		final GridData gd_sqlText = new GridData(SWT.FILL, SWT.FILL, true, true);
		sqlText.setLayoutData(gd_sqlText);

		setTitle(Messages.newTriggerMSGTitle);
		createInit();
		alterInit();
		addListener();
		return parent;

	}

	String[][] conditionTimeMap = { { Messages.conditionTimeBefore, "BEFORE" },
			{ Messages.conditionTimeAfter, "AFTER" },
			{ Messages.conditionTimeDeferred, "DEFERRED" } };

	private String getConditionTime() {
		for (int i = 0; i < conditionTimeBTNs.length; i++) {
			if (conditionTimeBTNs[i].getSelection()) {
				for (String[] map : conditionTimeMap) {
					if (map[0].equals(conditionTimeBTNs[i].getText())) {
						return map[1];
					}
				}
			}
		}
		return null;
	}

	private void setConditionTime(String innerConditionTime) {
		String[][] maps = conditionTimeMap;
		Button[] buttons = conditionTimeBTNs;
		if (innerConditionTime == null) {
			for (Button b : buttons) {
				b.setSelection(false);
			}
			return;
		}
		for (String[] map : maps) {
			if (map[1].equals(innerConditionTime)) {
				for (Button b : buttons) {
					if (b.getText().equals(map[0])) {
						b.setSelection(true);
					} else {
						b.setSelection(false);
					}
				}
			}
		}
	}

	String[][] eventTypeMap = { { Messages.eventTypeInsert, "INSERT" },
			{ Messages.eventTypeSInsert, "STATEMENT INSERT" },
			{ Messages.eventTypeUpdate, "UPDATE" },
			{ Messages.eventTypeSUpdate, "STATEMENT UPDATE" },
			{ Messages.eventTypeDelete, "DELETE" },
			{ Messages.eventTypeSDelete, "STATEMENT DELETE" },
			{ Messages.eventTypeCommit, "COMMIT" },
			{ Messages.eventTypeRollback, "ROLLBACK" }, };

	private String getEventType() {
		for (int i = 0; i < eventTypeBTNs.length; i++) {
			if (eventTypeBTNs[i].getSelection()) {
				for (String[] map : eventTypeMap) {
					if (map[0].equals(eventTypeBTNs[i].getText())) {
						return map[1];
					}
				}
			}
		}
		return null;
	}

	private void setEventType(String innerEventType) {
		String[][] maps = eventTypeMap;
		Button[] buttons = eventTypeBTNs;
		for (String[] map : maps) {
			if (map[1].equals(innerEventType)) {
				for (Button b : buttons) {
					if (b.getText().equals(map[0])) {
						b.setSelection(true);
					} else {
						b.setSelection(false);
					}
				}
			}
		}
	}

	String[][] actionTimeMap = { { Messages.actionTimeDefault, "default" },
			{ Messages.actionTimeAfter, "AFTER" },
			{ Messages.actionTimeDeferred, "DEFERRED" } };

	private String getActionTime() {
		for (int i = 0; i < actionTimeBTNs.length; i++) {
			if (actionTimeBTNs[i].getSelection()) {
				for (String[] map : actionTimeMap) {
					if (map[0].equals(actionTimeBTNs[i].getText())) {
						return map[1];
					}
				}
			}
		}
		return null;
	}

	private void setActionTime(String innerActionTime) {
		String[][] maps = actionTimeMap;
		Button[] buttons = actionTimeBTNs;
		if (innerActionTime.equals("BEFORE")) {
			actionTimeBTNs[0].setSelection(true);
			conditionTimeBTNs[0].setSelection(true);
			return;
		}
		for (String[] map : maps) {
			if (map[1].equals(innerActionTime)) {
				for (Button b : buttons) {
					if (b.getText().equals(map[0])) {
						b.setSelection(true);
					} else {
						b.setSelection(false);
					}
				}
			}
		}
	}

	String[][] actionTypeMap = {
			{ Messages.actionTypeReject, "REJECT" },
			{ Messages.actionTypePrint, "PRINT" },
			{ Messages.actionTypeOtherSQL, "OTHER STATEMENT" },
			{ Messages.actionTypeInvalidateTransaction,
					"INVALIDATE TRANSACTION" }, };

	private String getActionType() {
		for (int i = 0; i < actionTypeBTNs.length; i++) {
			if (actionTypeBTNs[i].getSelection()) {
				for (String[] map : actionTypeMap) {
					if (map[0].equals(actionTypeBTNs[i].getText())) {
						return map[1];
					}
				}
			}
		}
		return null;
	}

	private void setActionType(String innerActionType) {
		String[][] maps = actionTypeMap;
		Button[] buttons = actionTypeBTNs;
		for (String[] map : maps) {
			if (map[1].equals(innerActionType)) {
				for (Button b : buttons) {
					if (b.getText().equals(map[0])) {
						b.setSelection(true);
					} else {
						b.setSelection(false);
					}
				}
			}
		}
	}

	String[][] statusMap = { { Messages.triggerStatusActive, "ACTIVE" },
			{ Messages.triggerStatusInactive, "INACTIVE" }, };
	private List<String> tableList;

	private String getStatus() {
		for (int i = 0; i < statusBTNs.length; i++) {
			if (statusBTNs[i].getSelection()) {
				for (String[] map : statusMap) {
					if (map[0].equals(statusBTNs[i].getText())) {
						return map[1];
					}
				}
			}
		}
		return null;
	}

	private void setStatus(String innerStatus) {
		String[][] maps = statusMap;
		Button[] buttons = statusBTNs;
		for (String[] map : maps) {
			if (map[1].equals(innerStatus)) {
				for (Button b : buttons) {
					if (b.getText().equals(map[0])) {
						b.setSelection(true);
					} else {
						b.setSelection(false);
					}
				}
			}
		}
	}

	private void createGroupCreateTrigger() {
		GridData gridData1 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.CENTER, true, false, 2, 1);

		Group groupCondtionTime = new Group(composite, SWT.NONE);
		groupCondtionTime.setText(Messages.conditionApply);
		groupCondtionTime.setLayoutData(gridData1);
		groupCondtionTime.setLayout(new GridLayout(3, true));
		conditionTimeBTNs = new Button[3];
		conditionTimeBTNs[0] = new Button(groupCondtionTime, SWT.RADIO);
		conditionTimeBTNs[1] = new Button(groupCondtionTime, SWT.RADIO);
		conditionTimeBTNs[1].setLayoutData(new GridData(SWT.LEFT, SWT.FILL,
				true, false));
		conditionTimeBTNs[2] = new Button(groupCondtionTime, SWT.RADIO);
		conditionTimeBTNs[0].setText(Messages.conditionTimeBefore);
		conditionTimeBTNs[1].setText(Messages.conditionTimeAfter);
		conditionTimeBTNs[2].setText(Messages.conditionTimeDeferred);

		GridData gridData2 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.FILL, true, false, 2, 4);

		Group groupEvent = new Group(composite, SWT.NONE);
		groupEvent.setText(Messages.triggerEvent);
		groupEvent.setLayoutData(gridData2);
		groupEvent.setLayout(new GridLayout(2, true));

		eventTypeBTNs = new Button[8];
		eventTypeBTNs[0] = new Button(groupEvent, SWT.RADIO);
		eventTypeBTNs[1] = new Button(groupEvent, SWT.RADIO);
		eventTypeBTNs[1].setLayoutData(new GridData(SWT.FILL, SWT.FILL, true,
				false));
		eventTypeBTNs[2] = new Button(groupEvent, SWT.RADIO);
		eventTypeBTNs[3] = new Button(groupEvent, SWT.RADIO);
		eventTypeBTNs[3].setLayoutData(new GridData(SWT.FILL, SWT.FILL, true,
				false));
		final GridData gd_eventTypeBTNs = new GridData(SWT.FILL, SWT.CENTER,
				false, false);
		gd_eventTypeBTNs.widthHint = 159;
		eventTypeBTNs[3].setLayoutData(gd_eventTypeBTNs);

		eventTypeBTNs[4] = new Button(groupEvent, SWT.RADIO);
		eventTypeBTNs[5] = new Button(groupEvent, SWT.RADIO);
		eventTypeBTNs[5].setLayoutData(new GridData(SWT.FILL, SWT.FILL, true,
				false));
		eventTypeBTNs[6] = new Button(groupEvent, SWT.RADIO);
		eventTypeBTNs[7] = new Button(groupEvent, SWT.RADIO);
		eventTypeBTNs[7].setLayoutData(new GridData(SWT.FILL, SWT.FILL, true,
				false));

		eventTypeBTNs[0].setText(Messages.eventTypeInsert);
		eventTypeBTNs[1].setText(Messages.eventTypeSInsert);
		eventTypeBTNs[2].setText(Messages.eventTypeUpdate);
		eventTypeBTNs[3].setText(Messages.eventTypeSUpdate);
		eventTypeBTNs[4].setText(Messages.eventTypeDelete);
		eventTypeBTNs[5].setText(Messages.eventTypeSDelete);
		eventTypeBTNs[6].setText(Messages.eventTypeCommit);
		eventTypeBTNs[7].setText(Messages.eventTypeRollback);

		GridData gridData3 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.FILL, true, false, 2, 4);

		Group groupAction = new Group(composite, SWT.NONE);
		groupAction.setText(Messages.triggerActionGroupText);
		groupAction.setLayoutData(gridData3);
		groupAction.setLayout(new GridLayout(3, false));

		//Condition
		Label label4 = new Label(groupAction, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.triggerCondition);
		final GridData gd_label4 = new GridData(101, SWT.DEFAULT);
		label4.setLayoutData(gd_label4);

		GridData gridData8 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.FILL, true, false, 2, 1);

		triggerConditionText = new Text(groupAction, SWT.BORDER);
		triggerConditionText.setLayoutData(gridData8);

		GridData gridData9 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.CENTER, true, false, 3, 1);

		Group actionTimeGroup = new Group(groupAction, SWT.NONE);
		actionTimeGroup.setText(Messages.triggerExecutionTime);
		actionTimeGroup.setLayoutData(gridData9);
		actionTimeGroup.setLayout(new GridLayout(3, true));
		actionTimeBTNs = new Button[3];
		actionTimeBTNs[0] = new Button(actionTimeGroup, SWT.RADIO);
		actionTimeBTNs[1] = new Button(actionTimeGroup, SWT.RADIO);
		actionTimeBTNs[2] = new Button(actionTimeGroup, SWT.RADIO);
		actionTimeBTNs[1].setLayoutData(new GridData(SWT.LEFT, SWT.FILL, true,
				false));
		actionTimeBTNs[0].setText(Messages.actionTimeDefault);
		actionTimeBTNs[1].setText(Messages.actionTimeAfter);
		actionTimeBTNs[2].setText(Messages.actionTimeDeferred);

		GridData gridData10 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.FILL, true, false, 3, 1);

		Group actionTypeGroup = new Group(groupAction, SWT.NONE);
		actionTypeGroup.setText(Messages.triggerContents);
		actionTypeGroup.setLayoutData(gridData10);
		actionTypeGroup.setLayout(new GridLayout(2, true));

		actionTypeBTNs = new Button[4];
		actionTypeBTNs[0] = new Button(actionTypeGroup, SWT.RADIO);
		actionTypeBTNs[0].setText(Messages.actionTypeReject);
		actionTypeBTNs[1] = new Button(actionTypeGroup, SWT.RADIO);
		actionTypeBTNs[1].setText(Messages.actionTypeInvalidateTransaction);
		actionTypeBTNs[2] = new Button(actionTypeGroup, SWT.RADIO);
		actionTypeBTNs[2].setText(Messages.actionTypePrint);
		actionTypeBTNs[3] = new Button(actionTypeGroup, SWT.RADIO);
		actionTypeBTNs[3].setText(Messages.actionTypeOtherSQL);
	}

	private void createGroup1() {
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 2;
		GridData gridData12 = new org.eclipse.swt.layout.GridData(SWT.FILL,
				SWT.FILL, false, false, 2, 2);
		Group group1 = new Group(composite, SWT.NONE);
		group1.setText(Messages.triggerOptionalGroupName);
		group1.setLayout(gridLayout2);
		group1.setLayoutData(gridData12);

		final Group statusGroup = new Group(group1, SWT.NONE);
		statusGroup.setText(Messages.triggerStatusGroupText);
		final GridData gd_statusGroup = new GridData(SWT.FILL, SWT.FILL, true,
				true);

		statusGroup.setLayoutData(gd_statusGroup);
		final GridLayout gridLayout = new GridLayout();
		statusGroup.setLayout(gridLayout);

		statusBTNs = new Button[2];
		statusBTNs[0] = new Button(statusGroup, SWT.RADIO);
		statusBTNs[0].setLayoutData(new GridData());
		statusBTNs[0].setText(Messages.triggerStatusActive);
		statusBTNs[1] = new Button(statusGroup, SWT.RADIO);
		statusBTNs[1].setLayoutData(new GridData());
		statusBTNs[1].setText(Messages.triggerStatusInactive);

		Group triggerPriorityGroup = new Group(group1, SWT.NONE);
		triggerPriorityGroup.setText(Messages.triggerPriorityGroupText);
		GridData gd_triggerPriorityGroup = new GridData(SWT.FILL, SWT.FILL,
				true, true);
		triggerPriorityGroup.setLayoutData(gd_triggerPriorityGroup);
		final GridLayout gridLayout_1 = new GridLayout();
		gridLayout_1.numColumns = 2;
		triggerPriorityGroup.setLayout(gridLayout_1);
		Label label7 = new Label(triggerPriorityGroup, SWT.LEFT | SWT.WRAP);
		label7.setLayoutData(new GridData(61, SWT.DEFAULT));
		label7.setText(Messages.triggerPriorityText);

		triggerPriorityText = new Text(triggerPriorityGroup, SWT.BORDER);
		final GridData gd_sPIN_TRIGGER_PRIORITY = new GridData(SWT.FILL,
				SWT.CENTER, true, false);
		triggerPriorityText.setLayoutData(gd_sPIN_TRIGGER_PRIORITY);
		triggerPriorityText.setText("00.00");

	}

	private List<String> getTableList() {
		if (null == tableList) {
			CubridDatabase db = database;
			DatabaseInfo dbInfo = db.getDatabaseInfo();
			GetTablesTask task = new GetTablesTask(dbInfo);
			tableList = task.getUserTables();
			if (task.getErrorMsg() != null) {
				CommonTool.openErrorBox(task.getErrorMsg());
				return tableList;
			}
		}
		return tableList;
	}

	private void addTables() throws SQLException {
		triggerTargetTableCombo.removeAll();
		triggerTargetTableCombo.add("");
		for (String table : getTableList()) {
			triggerTargetTableCombo.add(table);
		}
	}

	private void addColumns(String tableName) throws SQLException {
		triggerTargetColumnCombo.removeAll();
		CubridDatabase db = database;
		DatabaseInfo dbInfo = db.getDatabaseInfo();
		GetAllAttrTask task = new GetAllAttrTask(dbInfo);
		task.setClassName(tableName);
		task.getDbAllAttrListTaskExcute();
		if (task.getErrorMsg() != null) {
			CommonTool.openErrorBox(task.getErrorMsg());
			return;
		}
		List<DBAttribute> list = task.getAllAttrList();
		triggerTargetColumnCombo.add("");
		for (DBAttribute col : list) {
			triggerTargetColumnCombo.add(col.getName());
		}

	}

	/**
	 * validate below information <ui>
	 * <li>trigger name
	 * <li>event type
	 * 
	 */
	private boolean validateAll() {
		setErrorMessage(null);
		changeOKButtonStatus(false);
		if (!validateTriggerName())
			return false;
		if (!validateEventType())
			return false;
		if (!validatePriority())
			return false;
		changeOKButtonStatus(true);
		return true;
	}

	/**
	 * change ok button status: enabled, or disabled
	 * 
	 */
	private void changeOKButtonStatus(boolean valid) {
		if (null == trigger) {
			getButton(IDialogConstants.OK_ID).setEnabled(valid);
		} else {
			getButton(AlterTriggerOK_ID).setEnabled(valid);
		}
	}

	/**
	 * validate event type
	 * 
	 */
	private boolean validateEventType() {
		String eventType = getEventType();
		String triggerEventTargetTable = triggerTargetTableCombo.getText().trim();
		if (eventType != null && !eventType.equals(Messages.eventTypeCommit)
				&& !eventType.equals(Messages.eventTypeRollback)
				&& triggerEventTargetTable.length() <= 0) {
			setErrorMessage(Messages.enterEventTargetMSG);
			//			triggerTargetTableCombo.setFocus();
			return false;
		}
		return true;
	}

	/**
	 * validate trigger name
	 * 
	 */
	private boolean validateTriggerName() {
		String triggerName = triggerNameText.getText();
		String chkstr = CommonTool.validateCheckInIdentifier(triggerName);
		if (chkstr.length() > 0) {
			setErrorMessage(Messages.invalidTriggerNameError);
			triggerNameText.setFocus();
			return false;
		}
		return true;
	}

	/**
	 * validate priority
	 * 
	 */
	private boolean validatePriority() {
		String strPriority = triggerPriorityText.getText();
		try {
			double priority = Double.parseDouble(strPriority);
			if (priority < 0 || priority > 9999.9901) {
				setErrorMessage(Messages.errRangePriority);
				return false;
			}
			String format = Trigger.formatPriority(strPriority);
			if (priority != Double.parseDouble(format)) {
				setErrorMessage(Messages.errPriorityFormat);
				return false;
			}
		} catch (NumberFormatException numberFormatException) {
			setErrorMessage(Messages.errFormatPriority);
			return false;
		}
		return true;
	}

}
