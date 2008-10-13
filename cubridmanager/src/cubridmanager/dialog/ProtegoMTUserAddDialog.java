package cubridmanager.dialog;

import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import cubrid.upa.UpaClient;
import cubrid.upa.UpaException;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.action.ProtegoUserManagementAction;

import org.eclipse.swt.widgets.Group;

public class ProtegoMTUserAddDialog extends Dialog {
	private boolean ret = false;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="39,60"
	private Composite compositeBody = null;
	private Composite compositeButton = null;
	private Group groupBody = null;
	private Label labelMTName = null;
	private Label labelDBName = null;
	private Label labelExpDate = null;
	private Text textMTName = null;
	private Text textDBName = null;
	private Text textExpDate = null;
	private Label labelNote = null;
	private Text textNote = null;
	private Label labelExpDateEx = null;
	private Button buttonSave = null;
	private Button buttonCancel = null;
	private Text textExpHour = null;
	private Label label = null;

	public ProtegoMTUserAddDialog(Shell parent) {
		super(parent);
	}

	public ProtegoMTUserAddDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(sShell);
		sShell.setDefaultButton(buttonSave);
		sShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	/**
	 * This method initializes sShell1
	 * 
	 */
	private void createSShell() {
		sShell = new Shell(getParent(), SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		// sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM); // for
		// visual-editor
		sShell.setLayout(new GridLayout());
		createCompositeBody();
		createCompositeButton();
		sShell.pack();
		sShell.setText(Messages.getString("TITLE.MTREG"));
	}

	/**
	 * This method initializes compositeBody
	 * 
	 */
	private void createCompositeBody() {
		compositeBody = new Composite(sShell, SWT.NONE);
		compositeBody.setLayout(new FillLayout());
		createGroupBody();
	}

	/**
	 * This method initializes compositeButton
	 * 
	 */
	private void createCompositeButton() {
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.heightHint = 22;
		gridData12.widthHint = 80;
		GridData gridData10 = new org.eclipse.swt.layout.GridData();
		gridData10.heightHint = 22;
		gridData10.widthHint = 80;
		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData9.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 2;
		compositeButton = new Composite(sShell, SWT.NONE);
		compositeButton.setLayout(gridLayout1);
		compositeButton.setLayoutData(gridData9);
		buttonSave = new Button(compositeButton, SWT.NONE);
		buttonSave.setText(Messages.getString("BUTTON.SAVE"));
		buttonSave.setLayoutData(gridData12);
		buttonSave.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String day = textExpDate.getText().trim();
						String hour = textExpHour.getText().trim();
						if ((day.length() == 0) || (hour.length() == 0)) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.INPUTEXPTIME"));
							return;
						}

						int iDay, iHour;
						try {
							iDay = Integer.parseInt(day);
							iHour = Integer.parseInt(hour);
						} catch (Exception ee) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.INVALIDEXPTIME"));
							return;
						}

						int expHour = iDay * 24 + iHour;

						String dbName = textDBName.getText().trim();
						if (dbName.length() == 0) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.INPUTTARGETDB"));
							return;
						}

						String mtName = textMTName.getText().trim();
						if (mtName.length() == 0) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.INPUTMTNAME"));
							return;
						}

						String noteMessage = textNote.getText().trim();
						if (noteMessage.length() == 0) {
							noteMessage = " ";
						}

						try {
							UpaClient.mtReg2(
									ProtegoUserManagementAction.dlg.upaKey,
									expHour, dbName, mtName, noteMessage);
						} catch (UpaException ee) {
							CommonTool.ErrorBox(sShell, "Auth transfer failed.");
							return;
						}
						ret = true;
						sShell.dispose();
					}
				});
		buttonCancel = new Button(compositeButton, SWT.NONE);
		buttonCancel.setText(Messages.getString("BUTTON.CANCEL"));
		buttonCancel.setLayoutData(gridData10);
		buttonCancel
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						sShell.dispose();
					}
				});
	}

	/**
	 * This method initializes groupBody
	 * 
	 */
	private void createGroupBody() {
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 50;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.widthHint = 50;
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.heightHint = 15;
		gridData7.widthHint = 50;
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.heightHint = 20;
		gridData6.grabExcessHorizontalSpace = true;
		gridData6.widthHint = 110;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.heightHint = 20;
		gridData5.grabExcessHorizontalSpace = true;
		gridData5.widthHint = 110;
		GridData gridData31 = new org.eclipse.swt.layout.GridData(
				GridData.FILL_HORIZONTAL);
		gridData31.heightHint = 20;
		gridData31.grabExcessHorizontalSpace = true;
		gridData31.widthHint = 110;
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData21.heightHint = 20;
		gridData21.widthHint = 110;
		gridData21.grabExcessHorizontalSpace = true;
		gridData21.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalSpan = 4;
		gridData3.widthHint = 250;
		gridData3.heightHint = 80;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 4;
		gridData1.widthHint = -1;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.heightHint = 15;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 4;
		gridData.widthHint = 180;
		gridData.grabExcessHorizontalSpace = false;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.heightHint = 15;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 5;
		gridLayout.makeColumnsEqualWidth = false;
		groupBody = new Group(compositeBody, SWT.NONE);
		groupBody.setLayout(gridLayout);
		labelMTName = new Label(groupBody, SWT.NONE);
		labelMTName.setText(Messages.getString("TABLE.MTUSERNAME"));
		labelMTName.setLayoutData(gridData21);
		textMTName = new Text(groupBody, SWT.BORDER);
		textMTName.setLayoutData(gridData);
		labelDBName = new Label(groupBody, SWT.NONE);
		labelDBName.setText(Messages.getString("TABLE.DATABASE"));
		labelDBName.setLayoutData(gridData31);
		textDBName = new Text(groupBody, SWT.BORDER);
		textDBName.setLayoutData(gridData1);
		labelExpDate = new Label(groupBody, SWT.NONE);
		labelExpDate.setText(Messages.getString("TABLE.EXPIRETIME"));
		labelExpDate.setLayoutData(gridData5);
		textExpDate = new Text(groupBody, SWT.BORDER);
		textExpDate.setLayoutData(gridData7);
		labelExpDateEx = new Label(groupBody, SWT.NONE);
		textExpHour = new Text(groupBody, SWT.BORDER);
		textExpHour.setLayoutData(gridData2);
		label = new Label(groupBody, SWT.NONE);
		label.setText(Messages.getString("TABLE.TIMEDATA"));
		labelNote = new Label(groupBody, SWT.NONE);
		labelNote.setText(Messages.getString("TABLE.NOTE"));
		labelNote.setLayoutData(gridData6);
		textNote = new Text(groupBody, SWT.BORDER | SWT.MULTI);
		textNote.setLayoutData(gridData3);
		labelExpDateEx.setText(Messages.getString("LABEL.DAY"));
		labelExpDateEx.setLayoutData(gridData11);
	}

	/**
	 * This method initializes sShell
	 * 
	 */
}
