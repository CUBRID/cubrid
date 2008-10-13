package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.MainRegistry;
import cubridmanager.cubrid.LockTran;
import cubridmanager.cubrid.view.CubridView;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class KILLTRANDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Text EDIT_KILLTRAN_DBNAME = null;
	private Group group1 = null;
	private Table LIST_TRANSACTIONS = null;
	private Button BUTTON_KILLTRAN = null;
	private Button IDCANCEL = null;
	private Button REFRESH = null;
	private CLabel cLabel = null;

	public KILLTRANDialog(Shell parent) {
		super(parent);
	}

	public KILLTRANDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.KILLTRANDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.widthHint = 140;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.widthHint = 140;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 140;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.grabExcessHorizontalSpace = true;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 4;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		gridData1.widthHint = 200;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.ACTIVETRANSACTIONS"));
		label1.setLayoutData(gridData);
		EDIT_KILLTRAN_DBNAME = new Text(sShell, SWT.NONE);
		EDIT_KILLTRAN_DBNAME.setEditable(false);
		EDIT_KILLTRAN_DBNAME.setLayoutData(gridData1);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.TRANSACTIONS"));
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData2);
		createTable1();
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData3);
		REFRESH = new Button(sShell, SWT.NONE);
		REFRESH.setText(Messages.getString("BUTTON.REFRESH"));
		REFRESH.setLayoutData(gridData4);
		REFRESH
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ClientSocket cs = new ClientSocket();
						if (!cs
								.SendBackGround(
										dlgShell,
										"dbname:" + CubridView.Current_db,
										"gettransactioninfo",
										Messages
												.getString("WAITING.GETTINGTRANSACTION"))) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}
						setinfo();
					}
				});
		BUTTON_KILLTRAN = new Button(sShell, SWT.NONE);
		BUTTON_KILLTRAN.setText(Messages.getString("BUTTON.KILLTRANSACTION"));
		BUTTON_KILLTRAN.setLayoutData(gridData5);
		BUTTON_KILLTRAN
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int ltidx = LIST_TRANSACTIONS.getSelectionIndex();
						if (ltidx >= 0 && ltidx < MainRegistry.Tmpchkrst.size()) {
							KILLTRAN_CONFIRMDialog dlg = new KILLTRAN_CONFIRMDialog(
									dlgShell);
							dlg.worktran = (LockTran) MainRegistry.Tmpchkrst
									.get(ltidx);
							if (dlg.doModal())
								setinfo();
						}
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CLOSE"));
		IDCANCEL.setLayoutData(gridData6);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
		setinfo();
	}

	private void createTable1() {
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.heightHint = 220;
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData7.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData7.widthHint = 550;
		LIST_TRANSACTIONS = new Table(group1, SWT.FULL_SELECTION | SWT.SINGLE
				| SWT.BORDER);
		LIST_TRANSACTIONS.setLinesVisible(true);
		LIST_TRANSACTIONS.setLayoutData(gridData7);
		LIST_TRANSACTIONS.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST_TRANSACTIONS.setLayout(tlayout);
		LIST_TRANSACTIONS.setToolTipText(Messages
				.getString("TOOLTIP.KILLTRANLIST"));

		TableColumn tblcol = new TableColumn(LIST_TRANSACTIONS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.TRAN_INDEX"));
		tblcol = new TableColumn(LIST_TRANSACTIONS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.USERNAME"));
		tblcol = new TableColumn(LIST_TRANSACTIONS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.HOST"));
		tblcol = new TableColumn(LIST_TRANSACTIONS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.PROCESSID"));
		tblcol = new TableColumn(LIST_TRANSACTIONS, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.PROGRAMNAME"));
	}

	public void setinfo() {
		LIST_TRANSACTIONS.removeAll();
		LockTran lt = null;
		for (int i = 0, n = MainRegistry.Tmpchkrst.size(); i < n; i++) {
			lt = (LockTran) MainRegistry.Tmpchkrst.get(i);
			TableItem item = new TableItem(LIST_TRANSACTIONS, SWT.NONE);
			item.setText(0, lt.index);
			item.setText(1, lt.uid);
			item.setText(2, lt.host);
			item.setText(3, lt.pid);
			item.setText(4, lt.pname);
		}
		for (int i = 0, n = LIST_TRANSACTIONS.getColumnCount(); i < n; i++) {
			LIST_TRANSACTIONS.getColumn(i).pack();
		}
		EDIT_KILLTRAN_DBNAME.setText(CubridView.Current_db);
	}
}
