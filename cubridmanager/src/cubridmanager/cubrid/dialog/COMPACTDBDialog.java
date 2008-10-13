package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.cubrid.action.CompactAction;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.FillLayout;

public class COMPACTDBDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Label label1 = null;
	private Label label2 = null;
	private Text EDIT_COMPACTDB_DBNAME = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;
	private CLabel cLabel = null;
	private CLabel cLabel1 = null;

	public COMPACTDBDialog(Shell parent) {
		super(parent);
	}

	public COMPACTDBDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void createSShell() {
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.COMPACTDBDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData57 = new org.eclipse.swt.layout.GridData();
		gridData57.widthHint = 346;
		gridData57.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData57.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData56 = new org.eclipse.swt.layout.GridData();
		gridData56.widthHint = 80;
		GridData gridData55 = new org.eclipse.swt.layout.GridData();
		gridData55.widthHint = 80;
		gridData55.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData55.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData54 = new org.eclipse.swt.layout.GridData();
		gridData54.grabExcessHorizontalSpace = true;
		GridData gridData53 = new org.eclipse.swt.layout.GridData();
		gridData53.grabExcessHorizontalSpace = true;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 2;
		gridData2.widthHint = 140;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 4;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData);
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.THISUTILITYRECLAIMS"));
		label1.setLayoutData(gridData57);
		label2 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.DATABASENAME1"));
		label2.setLayoutData(gridData1);
		EDIT_COMPACTDB_DBNAME = new Text(sShell, SWT.BORDER);
		EDIT_COMPACTDB_DBNAME.setEditable(false);
		EDIT_COMPACTDB_DBNAME.setLayoutData(gridData2);
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData53);
		cLabel1 = new CLabel(sShell, SWT.NONE);
		cLabel1.setLayoutData(gridData54);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData55);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CommonTool.WarnYesNo(dlgShell, Messages
								.getString("WARNYESNO.COMPACTDB")) == SWT.YES) {
							ClientSocket cs = new ClientSocket();
							if (cs.SendBackGround(dlgShell, "dbname:"
									+ CompactAction.ai.dbname, "compactdb",
									Messages.getString("WAITING.COMPACTDB"))) {
								ret = true;
								dlgShell.dispose();
							} else
								CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
						}
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData56);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
		setinfo();
	}

	private void setinfo() {
		EDIT_COMPACTDB_DBNAME.setText(CompactAction.ai.dbname);
	}

}
