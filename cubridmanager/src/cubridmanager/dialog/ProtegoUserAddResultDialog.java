package cubridmanager.dialog;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;

import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import cubrid.upa.UpaUserInfo;
import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.Messages;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;

public class ProtegoUserAddResultDialog extends Dialog {
	private boolean ret = false;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="39,11"
	private Composite compositeBody = null;
	private Composite compositeButton = null;
	private Group groupResult = null;
	private Label labelFileName = null;
	private Text textResultSummary = null;
	private Label labelFailedItems = null;
	private Table tableFailedList = null;
	private Button buttonSaveToFile = null;
	private Button buttonOK = null;
	public UpaUserInfo[] failedList = null;

	public ProtegoUserAddResultDialog(Shell parent) {
		super(parent);
	}

	public ProtegoUserAddResultDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(sShell);
		// sShell.setDefaultButton(IDOK);
		sShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		// sShell = new Shell(Application.mainwindow.getShell(),
		// SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setLayout(new GridLayout());
		createCompositeBody();
		createCompositeButton();
		sShell.pack();
	}

	/**
	 * This method initializes compositeBody
	 * 
	 */
	private void createCompositeBody() {
		compositeBody = new Composite(sShell, SWT.NONE);
		compositeBody.setLayout(new GridLayout());
		createGroupResult();
	}

	/**
	 * This method initializes compositeButton
	 * 
	 */
	private void createCompositeButton() {
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData11.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData1.heightHint = 25;
		gridData1.widthHint = 80;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData.heightHint = 25;
		gridData.widthHint = 80;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		compositeButton = new Composite(sShell, SWT.NONE);
		compositeButton.setLayout(gridLayout);
		compositeButton.setLayoutData(gridData11);
		buttonSaveToFile = new Button(compositeButton, SWT.NONE);
		buttonSaveToFile.setText(Messages.getString("BUTTON.SAVETOFILE"));
		buttonSaveToFile.setLayoutData(gridData1);
		buttonSaveToFile
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						FileDialog dlg = new FileDialog(Application.mainwindow
								.getShell(), SWT.SAVE | SWT.APPLICATION_MODAL);
						dlg
								.setFilterExtensions(new String[] { "*.txt",
										"*.*" });
						dlg.setFilterNames(new String[] { "Text file",
								"All files" });
						File curdir = new File(".");
						try {
							dlg.setFilterPath(curdir.getCanonicalPath());
						} catch (Exception ee) {
							dlg.setFilterPath(".");
						}

						String fileName = dlg.open();
						if (fileName != null) {
							File targetFile = new File(fileName);
							FileWriter writer = null;
							BufferedWriter bufWriter = null;
							try {
								writer = new FileWriter(targetFile);
								bufWriter = new BufferedWriter(writer);

								/*
								 * User registration file format : User-DN AP-ID DB-NAME DB-USER
								 * Expire-Date
								 */
								for (int i = 0; i < failedList.length; i++) {
									bufWriter.write(failedList[i].getUuserdn());
									bufWriter.write("\t");
									bufWriter.write(failedList[i].getApid());
									bufWriter.write("\t");
									bufWriter.write(failedList[i].getDbname());
									bufWriter.write("\t");
									bufWriter.write(failedList[i].getDbuser());
									bufWriter.write("\t");
									bufWriter.write(failedList[i].getExpdate());
									bufWriter.write("\n");
								}
							} catch (Exception ee) {
								CommonTool.ErrorBox(ee.getMessage());
							}

							try {
								bufWriter.close();
								writer.close();
							} catch (Exception ee) {
							}
						}
					}
				});
		buttonOK = new Button(compositeButton, SWT.NONE);
		buttonOK.setText("OK");
		buttonOK.setLayoutData(gridData);
		buttonOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
	}

	/**
	 * This method initializes groupResult
	 * 
	 */
	private void createGroupResult() {
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.heightHint = 50;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = -1;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		groupResult = new Group(compositeBody, SWT.NONE);
		groupResult.setText("result");
		groupResult.setLayout(new GridLayout());
		labelFileName = new Label(groupResult, SWT.NONE);
		labelFileName.setText(Messages.getString("LABEL.FILENAME"));
		labelFileName.setLayoutData(gridData2);
		textResultSummary = new Text(groupResult, SWT.BORDER | SWT.MULTI);
		textResultSummary.setEnabled(true);
		textResultSummary.setEditable(false);
		textResultSummary.setLayoutData(gridData3);
		setResultSummary();
		labelFailedItems = new Label(groupResult, SWT.NONE);
		labelFailedItems.setText(Messages.getString("TEXT.FAILEDLIST"));
		labelFailedItems.setLayoutData(gridData4);
		createTableFailedList();
	}

	/**
	 * This method initializes tableFailedList
	 * 
	 */
	private void createTableFailedList() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.widthHint = 450;
		gridData5.heightHint = 250;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		tableFailedList = new Table(groupResult, SWT.NONE);
		tableFailedList.setHeaderVisible(true);
		tableFailedList.setLayoutData(gridData5);
		tableFailedList.setLinesVisible(true);

		TableColumn ApId = new TableColumn(tableFailedList, SWT.LEFT);
		TableColumn UserDn = new TableColumn(tableFailedList, SWT.LEFT);
		TableColumn DbName = new TableColumn(tableFailedList, SWT.LEFT);
		TableColumn DbUser = new TableColumn(tableFailedList, SWT.LEFT);
		TableColumn ExpTime = new TableColumn(tableFailedList, SWT.LEFT);

		ApId.setText(Messages.getString("TABLE.APID"));
		ApId.setWidth(150);

		UserDn.setText(Messages.getString("TABLE.USERDN"));
		UserDn.setWidth(300);

		DbName.setText(Messages.getString("TABLE.DATABASE"));
		DbName.setWidth(100);

		DbUser.setText(Messages.getString("TABLE.DATABASEUSER"));
		DbUser.setWidth(100);

		ExpTime.setText(Messages.getString("TABLE.EXPIRETIME"));
		ExpTime.setWidth(250);

		setTableInfo();
	}

	private void setTableInfo() {
		if (failedList == null)
			return;
		/*
		 * user registration file format : User-DN AP-ID DB-NAME DB-USER Expire-Date
		 */
		for (int i = 0; i < failedList.length; i++) {
			TableItem item = new TableItem(tableFailedList, SWT.NONE);
			item.setText(0, failedList[i].getApid());
			item.setText(1, failedList[i].getUuserdn());
			item.setText(2, failedList[i].getDbname());
			item.setText(3, failedList[i].getDbuser());
			item.setText(4, failedList[i].getExpdate());
		}
	}

	private void setResultSummary() {
		String str = new String();
		str = String.valueOf(failedList.length)
				+ Messages.getString("TEXT.FAILED_N_LIST");
		textResultSummary.setText(str);
	}
}
