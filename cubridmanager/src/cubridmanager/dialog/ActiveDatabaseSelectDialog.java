package cubridmanager.dialog;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.DBUserInfo;

public class ActiveDatabaseSelectDialog extends Dialog {
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="10,51"
	private Label label1 = null;
	private Table LIST_ACTIVEDB = null;
	private TableItem CurrentLIST_ACTIVEDB = null;
	private Composite cmpTxtArea = null;
	private Label lblUser = null;
	private Text txtUser = null;
	private Label lblPassword = null;
	private Text txtPassword = null;
	private Composite cmpBtnArea = null;
	private Button BUTTON_OK = null;
	private Button BUTTON_CANCEL = null;
	public static DBUserInfo SelectedDB = null;
	private String currentSelectionDBName = null;

	public ActiveDatabaseSelectDialog(Shell parent) {
		super(parent);
	}

	public ActiveDatabaseSelectDialog(Shell parent, int style) {
		super(parent, style);
	}

	public ActiveDatabaseSelectDialog(Shell parent, String current_db) {
		super(parent);
		currentSelectionDBName = current_db;
	}

	public DBUserInfo doModal() {
		int actdbcnt = 0;
		AuthItem aurec;

		SelectedDB = null;
		for (int i = 0, n = MainRegistry.Authinfo.size(); i < n; i++) {
			aurec = (AuthItem) MainRegistry.Authinfo.get(i);
			if (aurec.status == MainConstants.STATUS_START)
				actdbcnt++;
		}
		if (actdbcnt <= 0) {
			CommonTool.ErrorBox(Messages.getString("MSG.NOAUTHORITY"));
			return null;
		}
		createSShell();

		TableItem item;
		for (int i = 0, n = MainRegistry.Authinfo.size(); i < n; i++) {
			aurec = (AuthItem) MainRegistry.Authinfo.get(i);
			if (aurec.status == MainConstants.STATUS_START) {
				item = new TableItem(LIST_ACTIVEDB, SWT.NONE);
				item.setText(0, aurec.dbname);
				if (aurec.setinfo)
					item.setData(MainRegistry.getDBUserInfo(aurec.dbname));
				else
					item.setData(null);
			}
		}

		if (currentSelectionDBName != null) {
			for (int i = 0; i < LIST_ACTIVEDB.getItemCount(); i++) {
				if (currentSelectionDBName.equals(LIST_ACTIVEDB.getItem(i)
						.getText(0))) {
					CurrentLIST_ACTIVEDB = LIST_ACTIVEDB.getItem(i);
					LIST_ACTIVEDB.setSelection(i);
					if (CurrentLIST_ACTIVEDB.getData() != null) {
						if (!MainRegistry.isProtegoBuild()) {
							txtUser.setText(((DBUserInfo) CurrentLIST_ACTIVEDB
									.getData()).dbuser);
							txtPassword
									.setText(((DBUserInfo) CurrentLIST_ACTIVEDB
											.getData()).dbpassword);
						}
					}
					break;
				}
			}
			if (!MainRegistry.isProtegoBuild()) {
				if (txtUser.getText().length() < 1) {
					if (MainRegistry.UserID.equals("admin"))
						txtUser.setText("dba");
					else
						txtUser.setText("public");
				}
			}
		}

		CommonTool.centerShell(sShell);
		sShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return SelectedDB;
	}

	private void createSShell() {
		sShell = new Shell(getParent(), SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setText(Messages.getString("TITLE.ACTIVEDATABASESELECTDIALOG"));
		sShell.setLayout(new GridLayout());

		GridData gridData = new GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.ACTIVEDATABASE"));
		label1.setLayoutData(gridData);

		createTable1();
		if (!MainRegistry.isProtegoBuild())
			createTxtArea();
		createBtnArea();
		sShell.pack();
		sShell.setDefaultButton(BUTTON_OK);
	}

	private void createTable1() {
		GridData gridData1 = new GridData();
		gridData1.heightHint = 150;
		gridData1.widthHint = 200;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST_ACTIVEDB = new Table(sShell, SWT.FULL_SELECTION | SWT.SINGLE
				| SWT.BORDER);
		LIST_ACTIVEDB.setLinesVisible(true);
		LIST_ACTIVEDB.setHeaderVisible(false);
		LIST_ACTIVEDB.setLayoutData(gridData1);
		LIST_ACTIVEDB.pack();
		LIST_ACTIVEDB.addMouseListener(new org.eclipse.swt.events.MouseAdapter() {
					public void mouseDoubleClick(
							org.eclipse.swt.events.MouseEvent e) {
						if (MainRegistry.isProtegoBuild()) {
							if (CurrentLIST_ACTIVEDB != null) {
								SelectedDB = new DBUserInfo(
										CurrentLIST_ACTIVEDB.getText(0), "", "");
								sShell.dispose();
							}
						} else {
							if (CurrentLIST_ACTIVEDB != null
									&& txtUser.getText().length() > 0) {
								SelectedDB = new DBUserInfo(
										CurrentLIST_ACTIVEDB.getText(0),
										txtUser.getText(), txtPassword
												.getText());
								sShell.dispose();
							}
						}
					}
				});

		LIST_ACTIVEDB.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(SelectionEvent e) {
						CurrentLIST_ACTIVEDB = LIST_ACTIVEDB.getSelection()[0];
						if (!MainRegistry.isProtegoBuild()) {
							if (CurrentLIST_ACTIVEDB.getData() != null) {
								txtUser
										.setText(((DBUserInfo) CurrentLIST_ACTIVEDB
												.getData()).dbuser);
								txtPassword
										.setText(((DBUserInfo) CurrentLIST_ACTIVEDB
												.getData()).dbpassword);
							} else {
								if (txtUser.getText().length() < 1) {
									if (MainRegistry.UserID.equals("admin"))
										txtUser.setText("dba");
									else
										txtUser.setText("public");
								}
								txtPassword.setText("");
							}
						}
					}
				});

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(100, 30, true));
		LIST_ACTIVEDB.setLayout(tlayout);
		TableColumn tblcol = new TableColumn(LIST_ACTIVEDB, SWT.LEFT);
		tblcol.setText("database");
	}

	private void createTxtArea() {
		GridData gridData5 = new GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 2;
		cmpTxtArea = new Composite(sShell, SWT.BORDER);
		cmpTxtArea.setLayout(gridLayout1);
		cmpTxtArea.setLayoutData(gridData5);

		GridData gridData6 = new GridData();
		gridData6.widthHint = 80;
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		lblUser = new Label(cmpTxtArea, SWT.NONE);
		lblUser.setLayoutData(gridData6);
		lblUser.setText(Messages.getString("LBL.USERID"));

		GridData gridData7 = new GridData();
		gridData7.widthHint = 100;
		gridData7.grabExcessHorizontalSpace = true;
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		txtUser = new Text(cmpTxtArea, SWT.BORDER);
		txtUser.setLayoutData(gridData7);

		GridData gridData9 = new GridData();
		gridData9.widthHint = 80;
		lblPassword = new Label(cmpTxtArea, SWT.NONE);
		lblPassword.setLayoutData(gridData9);
		lblPassword.setText(Messages.getString("LBL.USERPASSWORD"));

		GridData gridData8 = new GridData();
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		txtPassword = new Text(cmpTxtArea, SWT.BORDER | SWT.PASSWORD);
		txtPassword.setLayoutData(gridData8);
	}

	private void createBtnArea() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.makeColumnsEqualWidth = true;
		gridLayout.numColumns = 2;
		GridData gridData2 = new GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		cmpBtnArea = new Composite(sShell, SWT.NONE);
		cmpBtnArea.setLayout(gridLayout);
		cmpBtnArea.setLayoutData(gridData2);

		GridData gridData3 = new GridData();
		gridData3.widthHint = 80;
		BUTTON_OK = new Button(cmpBtnArea, SWT.NONE);
		BUTTON_OK.setText(Messages.getString("BUTTON.OK"));
		BUTTON_OK.setLayoutData(gridData3);
		BUTTON_OK.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CurrentLIST_ACTIVEDB == null) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("MSG.SELECTDB"));
							return;
						}
						if (MainRegistry.isProtegoBuild()) {
							SelectedDB = new DBUserInfo(CurrentLIST_ACTIVEDB
									.getText(0), "", "");
						} else {
							if (txtUser.getText().length() <= 0) {
								CommonTool.ErrorBox(sShell, Messages
										.getString("MSG.INPUT_ID"));
								txtUser.setFocus();
								return;
							}
							SelectedDB = new DBUserInfo(CurrentLIST_ACTIVEDB
									.getText(0), txtUser.getText(), txtPassword
									.getText());
						}
						sShell.dispose();
					}
				});

		GridData gridData4 = new GridData();
		gridData4.widthHint = 80;
		BUTTON_CANCEL = new Button(cmpBtnArea, SWT.NONE);
		BUTTON_CANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		BUTTON_CANCEL.setLayoutData(gridData4);
		BUTTON_CANCEL.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						SelectedDB = null;
						sShell.dispose();
					}
				});
	}
}
