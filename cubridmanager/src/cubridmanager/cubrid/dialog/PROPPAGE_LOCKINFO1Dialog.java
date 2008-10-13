package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.Application;
import cubridmanager.Messages;
import cubridmanager.cubrid.LockTran;
import cubridmanager.cubrid.action.LockinfoAction;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class PROPPAGE_LOCKINFO1Dialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Composite comparent = null;
	private Label label1 = null;
	private Group group1 = null;
	private Table LIST_LOCKINFO1 = null;
	private Group group2 = null;

	public PROPPAGE_LOCKINFO1Dialog(Shell parent) {
		super(parent);
	}

	public PROPPAGE_LOCKINFO1Dialog(Shell parent, int style) {
		super(parent, style);
	}

	public Composite SetTabPart(TabFolder parent) {
		comparent = parent;
		createComposite();
		sShell.setParent(parent);
		return sShell;
	}

	public int doModal() {
		createSShell();
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		dlgShell = new Shell(Application.mainwindow.getShell(),
				SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.PROPPAGE_LOCKINFO1DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData36 = new org.eclipse.swt.layout.GridData();
		gridData36.heightHint = 30;
		gridData36.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData36.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData36.grabExcessHorizontalSpace = true;
		gridData36.grabExcessVerticalSpace = true;
		gridData36.widthHint = 450;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessVerticalSpace = true;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.grabExcessHorizontalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.grabExcessHorizontalSpace = true;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
		sShell.setLayout(new GridLayout());
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.THELOCKSETTINGS"));
		group2.setLayout(new GridLayout());
		group2.setLayoutData(gridData);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.CLIENTSCURRENTLY"));
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData1);
		createTable1();
		label1 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.STATIC"));
		label1.setLayoutData(gridData36);
		sShell.pack();
		setinfo();
	}

	private void createTable1() {
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.heightHint = 240;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.grabExcessVerticalSpace = true;
		gridData2.widthHint = 500;
		LIST_LOCKINFO1 = new Table(group1, SWT.FULL_SELECTION | SWT.BORDER);
		LIST_LOCKINFO1.setLinesVisible(true);
		LIST_LOCKINFO1.setLayoutData(gridData2);
		LIST_LOCKINFO1.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST_LOCKINFO1.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_LOCKINFO1, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.INDEX"));
		tblcol = new TableColumn(LIST_LOCKINFO1, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.PNAME"));
		tblcol = new TableColumn(LIST_LOCKINFO1, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.UID"));
		tblcol = new TableColumn(LIST_LOCKINFO1, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.HOST"));
		tblcol = new TableColumn(LIST_LOCKINFO1, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.PID"));
		tblcol = new TableColumn(LIST_LOCKINFO1, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.ISOLATIONLEVEL"));
		tblcol = new TableColumn(LIST_LOCKINFO1, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.TIMEOUT"));
	}

	public void setinfo() {
		label1.setText(Messages.getString("LABEL.LOCKESCALATION") + " : "
				+ LockinfoAction.linfo.esc + "      "
				+ Messages.getString("LABEL.RUNDEADLOCKINTERVAL") + " : "
				+ LockinfoAction.linfo.dlinterval);
		label1.setToolTipText(Messages
				.getString("TOOLTIP.EDITSQLXWANTTOMODIFY"));
		LIST_LOCKINFO1.removeAll();
		LockTran lt;
		for (int i = 0, n = LockinfoAction.linfo.locktran.size(); i < n; i++) {
			lt = (LockTran) LockinfoAction.linfo.locktran.get(i);
			TableItem item = new TableItem(LIST_LOCKINFO1, SWT.NONE);
			item.setText(0, lt.index);
			item.setText(1, lt.pname);
			item.setText(2, lt.uid);
			item.setText(3, lt.host);
			item.setText(4, lt.pid);
			item.setText(5, lt.isolevel);
			item.setText(6, lt.timeout);
		}
		for (int i = 0, n = LIST_LOCKINFO1.getColumnCount(); i < n; i++) {
			LIST_LOCKINFO1.getColumn(i).pack();
		}
	}
}
