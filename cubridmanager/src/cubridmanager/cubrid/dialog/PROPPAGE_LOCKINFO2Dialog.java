package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.SWT;

import cubridmanager.Application;
import cubridmanager.Messages;
import cubridmanager.cubrid.LockEntry;
import cubridmanager.cubrid.action.LockinfoAction;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class PROPPAGE_LOCKINFO2Dialog extends Dialog {
	private Shell dlgShell = null; // @jve:decl-index=0:visual-constraint="10,61"
	private Composite comparent = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Label label1 = null;
	private Table LIST_LOCKINFO2 = null;
	private Button BUTTON_LOCKINFO2 = null;

	public PROPPAGE_LOCKINFO2Dialog(Shell parent) {
		super(parent);
	}

	public PROPPAGE_LOCKINFO2Dialog(Shell parent, int style) {
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
		dlgShell.setText(Messages.getString("TITLE.PROPPAGE_LOCKINFO2DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData37 = new org.eclipse.swt.layout.GridData();
		gridData37.heightHint = 50;
		gridData37.grabExcessHorizontalSpace = true;
		gridData37.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData37.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData37.grabExcessVerticalSpace = true;
		gridData37.widthHint = 500;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.widthHint = 100;
		gridData1.grabExcessVerticalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessVerticalSpace = true;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
		sShell.setLayout(new GridLayout());
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.THECONTENTSOF1"));
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData);
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.STATIC"));
		label1.setLayoutData(gridData37);
		createTable1();
		BUTTON_LOCKINFO2 = new Button(sShell, SWT.NONE);
		BUTTON_LOCKINFO2.setText(Messages.getString("BUTTON.DETAIL"));
		BUTTON_LOCKINFO2.setLayoutData(gridData1);
		BUTTON_LOCKINFO2
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int ti = LIST_LOCKINFO2.getSelectionIndex();
						if (ti >= 0 && ti < LockinfoAction.lockobj.entry.size()) {
							LOCKINFO_DETAILDialog dlg = new LOCKINFO_DETAILDialog(
									sShell.getShell());
							dlg.le = (LockEntry) LockinfoAction.lockobj.entry
									.get(ti);
							dlg.doModal();
						}
					}
				});
		sShell.pack();
		setinfo();
	}

	private void createTable1() {
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.heightHint = 200;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.grabExcessVerticalSpace = true;
		gridData2.widthHint = 500;
		LIST_LOCKINFO2 = new Table(group1, SWT.FULL_SELECTION | SWT.BORDER);
		LIST_LOCKINFO2.setLinesVisible(true);
		LIST_LOCKINFO2.setLayoutData(gridData2);
		LIST_LOCKINFO2.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST_LOCKINFO2.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_LOCKINFO2, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.OID"));
		tblcol = new TableColumn(LIST_LOCKINFO2, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.OBJECT_TYPE"));
		tblcol = new TableColumn(LIST_LOCKINFO2, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NUM_HOLDERS"));
		tblcol = new TableColumn(LIST_LOCKINFO2, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NUM_BLOCKED_HOLDERS"));
		tblcol = new TableColumn(LIST_LOCKINFO2, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NUM_WAITERS"));
	}

	public void setinfo() {
		label1.setText(Messages.getString("LABEL.CURRENTLOCKEDOBJECT") + " = "
				+ LockinfoAction.lockobj.entrynum + " \n"
				+ Messages.getString("LABEL.MAXIMUMLOCKEDOBJECT") + " = "
				+ LockinfoAction.lockobj.maxentrynum);

		LockEntry le;
		LIST_LOCKINFO2.removeAll();
		for (int i = 0, n = LockinfoAction.lockobj.entry.size(); i < n; i++) {
			le = (LockEntry) LockinfoAction.lockobj.entry.get(i);
			TableItem item = new TableItem(LIST_LOCKINFO2, SWT.NONE);
			item.setText(0, le.Oid);
			item.setText(1, le.ObjectType);
			item.setText(2, le.NumHolders);
			item.setText(3, le.Num_B_Holders);
			item.setText(4, le.NumWaiters);
		}
		for (int i = 0, n = LIST_LOCKINFO2.getColumnCount(); i < n; i++) {
			LIST_LOCKINFO2.getColumn(i).pack();
		}
	}
}
