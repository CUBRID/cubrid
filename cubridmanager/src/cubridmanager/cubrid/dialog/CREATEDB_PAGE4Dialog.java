package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.SWT;
import cubridmanager.Messages;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class CREATEDB_PAGE4Dialog extends WizardPage {
	public static final String PAGE_NAME = "CREATEDB_PAGE4Dialog";
	private Composite comparent = null;
	private Shell dlgShell = null; // @jve:decl-index=0:visual-constraint="10,61"
	private Composite sShell = null;
	private Label label1 = null;
	public Text EDIT_CREATEDB_SUMMARY = null;

	public CREATEDB_PAGE4Dialog() {
		super(PAGE_NAME, Messages.getString("PAGE.CREATEDBPAGE4"), null);
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
		dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.CREATEDB_PAGE4DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		sShell = new Composite(comparent, SWT.NONE);
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.heightHint = 200;
		gridData1.grabExcessHorizontalSpace = true;
		gridData1.grabExcessVerticalSpace = true;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.widthHint = 350;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.widthHint = 300;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessVerticalSpace = true;
		gridData.grabExcessHorizontalSpace = true;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(new GridLayout());
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.YOUHAVECOMPLETED"));
		label1.setLayoutData(gridData);
		EDIT_CREATEDB_SUMMARY = new Text(sShell, SWT.BORDER | SWT.MULTI
				| SWT.WRAP | SWT.V_SCROLL);
		EDIT_CREATEDB_SUMMARY.setEditable(false);
		EDIT_CREATEDB_SUMMARY.setLayoutData(gridData1);
		sShell.getParent().pack();
	}

	public void createControl(Composite parent) {
		comparent = parent;
		createComposite();
		sShell.setParent(parent);
		setControl(sShell);
		setPageComplete(false);
	}

	public boolean isActive() {
		return isCurrentPage();
	}
}
