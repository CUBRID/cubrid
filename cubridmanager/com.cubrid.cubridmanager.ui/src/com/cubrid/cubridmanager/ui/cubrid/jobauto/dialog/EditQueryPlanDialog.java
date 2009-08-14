package com.cubrid.cubridmanager.ui.cubrid.jobauto.dialog;

import java.util.ArrayList;
import java.util.List;
import java.util.Observable;
import java.util.Observer;

import org.eclipse.core.runtime.NullProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.model.AddEditType;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfoHelp;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.Messages;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.control.PeriodGroup;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * A dialog that show up when a user click the query plan context menu.
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-3-12 created by lizhiqiang
 */
public class EditQueryPlanDialog extends
		CMTitleAreaDialog implements
		Observer {

	private Text statementText;
	private Text idText;

	private AddEditType operation;
	private PeriodGroup periodGroup;
	private QueryPlanInfoHelp queryPlanInfo;

	private List<String> childrenLabel;
	private CubridDatabase database;
	private boolean isOkenable[];
	private int queryplanIdMaxLen = Integer.valueOf(Messages.queryplanIdMaxLen);

	/**
	 * Constructor
	 * 
	 * @param parentShell
	 */
	public EditQueryPlanDialog(Shell parentShell) {
		super(parentShell);
		isOkenable = new boolean[] { false, false, true, true, true, true };
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp, CubridManagerHelpContextIDs.databaseJobauto);
		final Composite composite = new Composite(parentComp, SWT.RESIZE);
		final GridData gdComposite = new GridData(SWT.FILL, SWT.FILL, true,
				true);
		gdComposite.widthHint = 500;
		composite.setLayoutData(gdComposite);
		final GridLayout gridLayout = new GridLayout();
		gridLayout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		gridLayout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		gridLayout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		gridLayout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(gridLayout);

		final Group queryGroup = new Group(composite, SWT.NONE);
		final GridData gd_queryGroup = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		queryGroup.setLayoutData(gd_queryGroup);
		queryGroup.setLayout(new GridLayout());
		queryGroup.setText(Messages.msgQryBasicGroupName);

		createBasicGroup(queryGroup);

		periodGroup = new PeriodGroup(this);
		periodGroup.addObserver(this);
		if (operation.equals(AddEditType.EDIT)) {
			// Sets the edit title and message
			setMessage(Messages.editQryPlanMsg);
			setTitle(Messages.editQryPlanTitle);
			getShell().setText(Messages.editQryPlanTitle);
			periodGroup.setTypeValue((queryPlanInfo.getPeriod()));
			periodGroup.setDetailValue(queryPlanInfo.getDetail());
			periodGroup.setHourValue(queryPlanInfo.getHour());
			periodGroup.setMinuteValue(queryPlanInfo.getMinute());

			isOkenable[0] = true;
			isOkenable[1] = true;
		} else {
			setMessage(Messages.addQryPlanMsg);
			setTitle(Messages.addQryPlanTitle);
			getShell().setText(Messages.addQryPlanTitle);
		}
		periodGroup.setMsgPeriodGroup(Messages.msgQryPeriodGroup);
		periodGroup.setMsgPeriodHourLbl(Messages.msgQryPeriodHourLbl);
		periodGroup.setMsgPeriodMinuteLbl(Messages.msgQryPeriodMinuteLbl);
		periodGroup.createPeriodGroup(composite);
		createStatementGroup(composite);

		return parentComp;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		super.createButtonsForButtonBar(parent);
		if (operation.equals(AddEditType.ADD))
			getButton(IDialogConstants.OK_ID).setEnabled(false);
	}

	/*
	 * Creates basic group
	 */
	private void createBasicGroup(Composite composite) {
		final Composite idComposite = new Composite(composite, SWT.NONE);
		final GridData gdIdComposite = new GridData(GridData.FILL_HORIZONTAL);
		idComposite.setLayoutData(gdIdComposite);
		final GridLayout gridLayout = new GridLayout(2, false);
		idComposite.setLayout(gridLayout);

		final Label queryExecPlanLabel = new Label(idComposite, SWT.NONE);
		final GridData gdQueryExecPlanLabel = new GridData(SWT.LEFT,
				SWT.CENTER, false, false);
		queryExecPlanLabel.setLayoutData(gdQueryExecPlanLabel);
		queryExecPlanLabel.setText(Messages.msgQryIdLbl);

		idText = new Text(idComposite, SWT.BORDER);
		final GridData gdIdText = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		gdIdText.widthHint = 80;
		idText.setLayoutData(gdIdText);

		// sets the initial value
		if (operation.equals(AddEditType.EDIT)) {
			idText.setText(queryPlanInfo.getQuery_id());
			idText.setEditable(false);
		} else {
			idText.setEditable(true);
		}
		idText.addModifyListener(new IdTextModifyListener());
	}

	/*
	 * Creates statement group
	 */
	private void createStatementGroup(Composite composite) {
		final Group statementGroup = new Group(composite, SWT.NONE);
		statementGroup.setText(Messages.msgQryStateLbl);
		final GridData gd_statementGroup = new GridData(SWT.FILL, SWT.FILL,
				true, true);
		statementGroup.setLayoutData(gd_statementGroup);
		statementGroup.setLayout(new GridLayout());

		statementText = new Text(statementGroup, SWT.WRAP | SWT.V_SCROLL
				| SWT.MULTI | SWT.BORDER);
		final GridData gd_text = new GridData(SWT.FILL, SWT.FILL, true, true);
		gd_text.widthHint = 470;
		gd_text.heightHint = 100;
		statementText.setLayoutData(gd_text);
		// sets the initial value
		if (operation.equals(AddEditType.EDIT)) {
			statementText.setText(queryPlanInfo.getQuery_string());
		}
		statementText.addModifyListener(new StatementModifyListener());
	}

	@Override
	public void okPressed() {
		// Updates the fields of backupPlanInfo
		queryPlanInfo.setDbname(database.getName());
		String newQueryid = idText.getText().trim();
		queryPlanInfo.setQuery_id(newQueryid);
		String newPeriodType = periodGroup.getTextOfTypeCombo();
		queryPlanInfo.setPeriod(newPeriodType);
		String hour = periodGroup.getHour();
		queryPlanInfo.setHour(hour);
		String minute = periodGroup.getMinute();
		queryPlanInfo.setMinute(minute);
		String detail = periodGroup.getTextOfDetailCombo();
		queryPlanInfo.setDetail(detail);
		String query = statementText.getText().trim();
		queryPlanInfo.setQuery_string(query);
		super.okPressed();

	}

	/**
	 * A class that response the change of idText
	 */
	private class IdTextModifyListener implements
			ModifyListener {
		public void modifyText(ModifyEvent e) {
			String id = idText.getText().trim();
			if (id.length() <= 0) {
				isOkenable[0] = false;
			} else {
				if (!ValidateUtil.isValidDBName(id)) {
					isOkenable[3] = false;
				} else if (childrenLabel.contains(id)) {
					isOkenable[4] = false;
				} else if (id.length() > queryplanIdMaxLen) {
					isOkenable[5] = false;
				} else {
					isOkenable[0] = true;
					isOkenable[3] = true;
					isOkenable[4] = true;
					isOkenable[5] = true;
				}
			}
			enableOk();
		}
	}

	/**
	 * A class that response the change of statementText
	 */
	public class StatementModifyListener implements
			ModifyListener {

		/*
		 * (non-Javadoc)
		 * 
		 * @see org.eclipse.swt.events.ModifyListener#modifyText(org.eclipse.swt.events.ModifyEvent)
		 */
		public void modifyText(ModifyEvent e) {
			String query = statementText.getText().trim();
			if (query.length() > 0) {
				isOkenable[1] = true;
			} else {
				isOkenable[1] = false;
			}
			enableOk();

		}

	}

	/**
	 * Sets the queryPlanInfo and selection which is a folder
	 * 
	 * @param selection the selection to set
	 */
	public void initPara(DefaultSchemaNode selection) {
		childrenLabel = new ArrayList<String>();
		ICubridNode[] childrenNode = null;
		QueryPlanInfo qpi = null;
		if (operation.equals(AddEditType.EDIT)) {
			qpi = (QueryPlanInfo) selection.getAdapter(QueryPlanInfo.class);
			childrenNode = selection.getParent().getChildren(
					new NullProgressMonitor());
		} else {
			qpi = new QueryPlanInfo();
			childrenNode = selection.getChildren(new NullProgressMonitor());
		}
		queryPlanInfo = new QueryPlanInfoHelp();
		queryPlanInfo.setQueryPlanInfo(qpi);
		database = selection.getDatabase();
		for (ICubridNode childNode : childrenNode) {
			childrenLabel.add(childNode.getLabel());
		}
	}

	/**
	 * @param operation the operation to set
	 */
	public void setOperation(AddEditType operation) {
		this.operation = operation;
	}

	/**
	 * @param childrenLabel the childrenLabel to set
	 */
	public void setChildrenLabel(List<String> childrenLabel) {
		this.childrenLabel = childrenLabel;
	}

	/**
	 * @param database the database to set
	 */
	public void setDatabase(CubridDatabase database) {
		this.database = database;
	}

	/**
	 * Enable the "OK" button
	 * 
	 */
	private void enableOk() {
		boolean is = true;
		for (int i = 0; i < isOkenable.length; i++) {
			is = is && isOkenable[i];
		}
		if (!isOkenable[0]) {
			setErrorMessage(Messages.errQueryplanIdEmpty);
		} else if (!isOkenable[3]) {
			setErrorMessage(Messages.errIdTextMsg);
		} else if (!isOkenable[5]) {
			setErrorMessage(Messages.errQueryplanIdLen);
		} else if (!isOkenable[4]) {
			setErrorMessage(Messages.errQueryPlanIdRepeatMsg);
		} else if (!isOkenable[2]) {
			periodGroup.enableOk();
		} else if (!isOkenable[1]) {
			setErrorMessage(Messages.errQueryplanStmtEmpty);
		} else {
			setErrorMessage(null);
		}
		if (is) {
			getButton(IDialogConstants.OK_ID).setEnabled(true);
		} else {
			getButton(IDialogConstants.OK_ID).setEnabled(false);
		}
	}

	/**
	 * Gets the instance of QueryPlanInfoHelp
	 * 
	 * @return
	 */
	public QueryPlanInfoHelp getQueryPlanInfo() {
		return queryPlanInfo;
	}

	/**
	 * Observer the change of instance of the type Period
	 */
	public void update(Observable o, Object arg) {
		boolean isAllow = (Boolean) arg;
		isOkenable[2] = isAllow;
		enableOk();
	}

}
