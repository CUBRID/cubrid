package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.cubrid.LockEntry;
import cubridmanager.cubrid.LockHolders;
import cubridmanager.cubrid.Lock_B_Holders;
import cubridmanager.cubrid.LockWaiters;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class LOCKINFO_DETAILDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Group group1 = null;
	private Table LIST1_LOCKINFO_DETAIL = null;
	private Group group2 = null;
	private Table LIST2_LOCKINFO_DETAIL = null;
	private Group group3 = null;
	private Table LIST3_LOCKINFO_DETAIL = null;
	private Button IDOK = null;
	public LockEntry le = null;

	public LOCKINFO_DETAILDialog(Shell parent) {
		super(parent);
	}

	public LOCKINFO_DETAILDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.LOCKINFO_DETAILDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData4.widthHint = 100;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.widthHint = 440;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(new GridLayout());
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.STATIC1"));
		label1.setLayoutData(gridData);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.LOCKHOLDERS"));
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData1);
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.BLOCKEDLOCKHOLDERS"));
		group2.setLayout(new GridLayout());
		group2.setLayoutData(gridData2);
		group3 = new Group(sShell, SWT.NONE);
		group3.setText(Messages.getString("GROUP.LOCKWAITERS"));
		group3.setLayout(new GridLayout());
		group3.setLayoutData(gridData3);
		createTable1();
		createTable2();
		createTable3();
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.CLOSE"));
		IDOK.setLayoutData(gridData4);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
		setinfo();
		dlgShell.pack();
	}

	private void createTable1() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.heightHint = 100;
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.widthHint = 600;
		LIST1_LOCKINFO_DETAIL = new Table(group1, SWT.FULL_SELECTION
				| SWT.BORDER);
		LIST1_LOCKINFO_DETAIL.setLinesVisible(true);
		LIST1_LOCKINFO_DETAIL.setLayoutData(gridData5);
		LIST1_LOCKINFO_DETAIL.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST1_LOCKINFO_DETAIL.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST1_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.TRAN_INDEX"));
		tblcol = new TableColumn(LIST1_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.GRANTEDMODE"));
		tblcol = new TableColumn(LIST1_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.COUNT"));
		tblcol = new TableColumn(LIST1_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NSUBGRANULES"));
	}

	private void createTable2() {
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData6.heightHint = 100;
		gridData6.widthHint = 600;
		gridData6.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST2_LOCKINFO_DETAIL = new Table(group2, SWT.FULL_SELECTION
				| SWT.BORDER);
		LIST2_LOCKINFO_DETAIL.setLinesVisible(true);
		LIST2_LOCKINFO_DETAIL.setLayoutData(gridData6);
		LIST2_LOCKINFO_DETAIL.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST2_LOCKINFO_DETAIL.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST2_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.TRAN_INDEX"));
		tblcol = new TableColumn(LIST2_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.GRANTEDMODE"));
		tblcol = new TableColumn(LIST2_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.COUNT"));
		tblcol = new TableColumn(LIST2_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.BLOCKEDMODE"));
		tblcol = new TableColumn(LIST2_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.STARTWAITINGAT"));
		tblcol = new TableColumn(LIST2_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.WAITFORNSECS"));
	}

	private void createTable3() {
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData7.heightHint = 100;
		gridData7.widthHint = 600;
		gridData7.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST3_LOCKINFO_DETAIL = new Table(group3, SWT.FULL_SELECTION
				| SWT.BORDER);
		LIST3_LOCKINFO_DETAIL.setLinesVisible(true);
		LIST3_LOCKINFO_DETAIL.setLayoutData(gridData7);
		LIST3_LOCKINFO_DETAIL.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST3_LOCKINFO_DETAIL.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST3_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.TRAN_INDEX"));
		tblcol = new TableColumn(LIST3_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.BLOCKEDMODE"));
		tblcol = new TableColumn(LIST3_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.STARTWAITINGAT"));
		tblcol = new TableColumn(LIST3_LOCKINFO_DETAIL, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.WAITFORNSECS"));
	}

	private void setinfo() {
		label1.setText(Messages.getString("LABEL.OBJECTID") + " : " + le.Oid
				+ " \n" + Messages.getString("LABEL.OBJECTTYPE") + " : "
				+ le.ObjectType);

		LockHolders lh;
		for (int i = 0, n = le.LockHolders.size(); i < n; i++) {
			lh = (LockHolders) le.LockHolders.get(i);
			TableItem item = new TableItem(LIST1_LOCKINFO_DETAIL, SWT.NONE);
			item.setText(0, lh.TranIndex);
			item.setText(1, lh.GrantMode);
			item.setText(2, lh.Count);
			item.setText(3, lh.NSubgranules);
		}
		for (int i = 0, n = LIST1_LOCKINFO_DETAIL.getColumnCount(); i < n; i++) {
			LIST1_LOCKINFO_DETAIL.getColumn(i).pack();
		}

		Lock_B_Holders lbh;
		for (int i = 0, n = le.Lock_B_Holders.size(); i < n; i++) {
			lbh = (Lock_B_Holders) le.Lock_B_Holders.get(i);
			TableItem item = new TableItem(LIST2_LOCKINFO_DETAIL, SWT.NONE);
			item.setText(0, lbh.TranIndex);
			item.setText(1, lbh.GrantedMode);
			item.setText(2, lbh.Count);
			item.setText(3, lbh.BlockedMode);
			item.setText(4, lbh.StartAt);
			item.setText(5, lbh.WaitForSec);
		}
		for (int i = 0, n = LIST2_LOCKINFO_DETAIL.getColumnCount(); i < n; i++) {
			LIST2_LOCKINFO_DETAIL.getColumn(i).pack();
		}

		LockWaiters lw;
		for (int i = 0, n = le.LockWaiters.size(); i < n; i++) {
			lw = (LockWaiters) le.LockWaiters.get(i);
			TableItem item = new TableItem(LIST3_LOCKINFO_DETAIL, SWT.NONE);
			item.setText(0, lw.TranIndex);
			item.setText(1, lw.BlockedMode);
			item.setText(2, lw.StartAt);
			item.setText(3, lw.WaitForSec);
		}
		for (int i = 0, n = LIST3_LOCKINFO_DETAIL.getColumnCount(); i < n; i++) {
			LIST3_LOCKINFO_DETAIL.getColumn(i).pack();
		}
	}
}
