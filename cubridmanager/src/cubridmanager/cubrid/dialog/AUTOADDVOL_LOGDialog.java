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
import cubridmanager.cubrid.AddVols;
import cubridmanager.cubrid.view.CubridView;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.FillLayout;

public class AUTOADDVOL_LOGDialog extends Dialog {
	private Shell dlgShell = null; // @jve:decl-index=0:visual-constraint="10,51"
	private Composite sShell = null;
	private Group group1 = null;
	private Table LIST_AUTOADDVOL_LOG = null;
	private Button BUTTON__AUTOADDVOL_LOG_REFRESH = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private CLabel cLabel = null;

	public AUTOADDVOL_LOGDialog(Shell parent) {
		super(parent);
	}

	public AUTOADDVOL_LOGDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		setinfo();

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
		dlgShell.setText(Messages.getString("TITLE.AUTOADDVOL_LOGDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData17 = new org.eclipse.swt.layout.GridData();
		gridData17.widthHint = 100;
		gridData17.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData17.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData16 = new org.eclipse.swt.layout.GridData();
		gridData16.widthHint = 100;
		gridData16.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData16.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData15 = new org.eclipse.swt.layout.GridData();
		gridData15.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData15.widthHint = 100;
		gridData15.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData14 = new org.eclipse.swt.layout.GridData();
		gridData14.grabExcessHorizontalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 4;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.LOGS"));
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData);
		createTable1();
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData14);
		BUTTON__AUTOADDVOL_LOG_REFRESH = new Button(sShell, SWT.NONE);
		BUTTON__AUTOADDVOL_LOG_REFRESH.setText(Messages
				.getString("BUTTON.REFRESH"));
		BUTTON__AUTOADDVOL_LOG_REFRESH.setLayoutData(gridData15);
		BUTTON__AUTOADDVOL_LOG_REFRESH
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setinfo();
					}
				});
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData16);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData17);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	private void createTable1() {
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.heightHint = 356;
		gridData1.widthHint = 520;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST_AUTOADDVOL_LOG = new Table(group1, SWT.FULL_SELECTION | SWT.BORDER);
		LIST_AUTOADDVOL_LOG.setLinesVisible(true);
		LIST_AUTOADDVOL_LOG.setLayoutData(gridData1);
		LIST_AUTOADDVOL_LOG.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		LIST_AUTOADDVOL_LOG.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_AUTOADDVOL_LOG, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.DATABASE"));
		tblcol = new TableColumn(LIST_AUTOADDVOL_LOG, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.VOLUMENAME"));
		tblcol = new TableColumn(LIST_AUTOADDVOL_LOG, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.PURPOSE"));
		tblcol = new TableColumn(LIST_AUTOADDVOL_LOG, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NUMPAGES"));
		tblcol = new TableColumn(LIST_AUTOADDVOL_LOG, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.TIME"));
		tblcol = new TableColumn(LIST_AUTOADDVOL_LOG, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.STATUS"));
	}

	private void setinfo() {
		LIST_AUTOADDVOL_LOG.removeAll();
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(dlgShell, "", "getautoaddvollog", Messages
				.getString("WAITING.GETTINGLOGINFORM"))) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			return;
		}
		for (int i = 0, n = MainRegistry.Tmpchkrst.size(); i < n; i++) {
			AddVols av = (AddVols) MainRegistry.Tmpchkrst.get(i);
			if (!av.DbName.equals(CubridView.Current_db))
				continue;
			TableItem item = new TableItem(LIST_AUTOADDVOL_LOG, SWT.NONE);
			item.setText(0, av.DbName);
			item.setText(1, av.Volname);
			item.setText(2, av.Purpose);
			item.setText(3, av.Pages);
			item.setText(4, av.Time);
			item.setText(5, av.Status);
		}
		for (int i = 0, n = LIST_AUTOADDVOL_LOG.getColumnCount(); i < n; i++) {
			LIST_AUTOADDVOL_LOG.getColumn(i).pack();
		}
	}
}
