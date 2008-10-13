package cubridmanager.cas.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.SWT;
import cubridmanager.Messages;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Label;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class PROPPAGE3_BROKER_OFFSETDialog extends WizardPage {
	public static final String PAGE_NAME = "PROPPAGE3_BROKER_OFFSETDialog";
	private Composite comparent = null;
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Text EDIT_BROKER_OFFSET_SUM = null;
	private Label label1 = null;

	public PROPPAGE3_BROKER_OFFSETDialog() {
		super(PAGE_NAME, Messages
				.getString("TITLE.PROPPAGE3_BROKER_OFFSETDIALOG"), null);
	}

	public void createControl(Composite parent) {
		comparent = parent;
		createComposite();
		sShell.setParent(parent);
		setControl(sShell);
		setPageComplete(false); // last page is false, others are true.
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
		dlgShell.setText(Messages
				.getString("TITLE.PROPPAGE3_BROKER_OFFSETDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData35 = new org.eclipse.swt.layout.GridData();
		gridData35.heightHint = 250;
		gridData35.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData35.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData35.grabExcessHorizontalSpace = true;
		gridData35.grabExcessVerticalSpace = true;
		gridData35.widthHint = 400;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessVerticalSpace = true;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
		sShell.setLayout(new GridLayout());
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.REPORT"));
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData);
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.YOUCOMPLETED1"));
		EDIT_BROKER_OFFSET_SUM = new Text(group1, SWT.BORDER | SWT.MULTI);
		EDIT_BROKER_OFFSET_SUM.setEditable(false);
		EDIT_BROKER_OFFSET_SUM.setLayoutData(gridData35);
		sShell.pack();
	}

	public void setinfo() {
		String summary = "================"
				+ Messages.getString("MSG.ADDBROKERBASICOPTION")
				+ "====================\r\n\r\n";
		String chkstr = null;
		chkstr = PROPPAGE1_BROKER_OFFSETDialog.EDIT_BROKER_OFFSET_BNAME
				.getText().trim();
		summary += Messages.getString("MSG.ADDBROKERBROKERNAMEIS") + " ";
		if (chkstr.length() > 0)
			summary += chkstr + "\r\n";
		else
			summary += Messages.getString("MSG.NOTSPECIFIED") + "\r\n";

		chkstr = PROPPAGE1_BROKER_OFFSETDialog.EDIT_BROKER_OFFSET_ASTYPE
				.getText();
		summary += Messages.getString("MSG.ADDBROKERBROKERASTYPEIS") + chkstr
				+ "\r\n";

		if (!chkstr.equals("CAS")) {
			chkstr = PROPPAGE1_BROKER_OFFSETDialog.EDIT_BROKER_OFFSET_APPL_ROOT
					.getText().trim();
			summary += Messages.getString("MSG.ADDBROKERASROOTDIRECTORYIS")
					+ " ";
			if (chkstr.length() > 0)
				summary += chkstr + "\r\n";
			else
				summary += Messages.getString("MSG.NOTSPECIFIED") + "\r\n";
		}

		chkstr = PROPPAGE1_BROKER_OFFSETDialog.EDIT_BROKER_OFFSET_ASMIN
				.getText().trim();
		summary += Messages.getString("MSG.ADDBROKERMINIMUMASNUMBERIS") + " ";
		if (chkstr.length() > 0)
			summary += chkstr + "\r\n";
		else
			summary += Messages.getString("MSG.NOTSPECIFIED") + "\r\n";

		chkstr = PROPPAGE1_BROKER_OFFSETDialog.EDIT_BROKER_OFFSET_ASMAX
				.getText().trim();
		summary += Messages.getString("MSG.ADDBROKERMAXIMUMASNUMBERIS") + " ";
		if (chkstr.length() > 0)
			summary += chkstr + "\r\n";
		else
			summary += Messages.getString("MSG.NOTSPECIFIED") + "\r\n";

		chkstr = PROPPAGE1_BROKER_OFFSETDialog.EDIT_BROKER_OFFSET_PORT
				.getText().trim();
		summary += Messages.getString("MSG.ADDBROKERBROKERPORTNUMBERIS") + " ";
		if (chkstr.length() > 0)
			summary += chkstr + "\r\n";
		else
			summary += Messages.getString("MSG.NOTSPECIFIED") + "\r\n";

		summary += "================"
				+ Messages.getString("MSG.ADDBROKERADVANCEDOPTION")
				+ "====================\r\n\r\n";
		for (int i = 0; i < PROPPAGE2_BROKER_OFFSETDialog.LIST_BROKER_OFFSET_LIST1
				.getItemCount(); i++) {
			TableItem ti = PROPPAGE2_BROKER_OFFSETDialog.LIST_BROKER_OFFSET_LIST1
					.getItem(i);
			summary += ti.getText(0) + ":" + ti.getText(1) + "\r\n";
		}
		EDIT_BROKER_OFFSET_SUM.setText(summary);
	}
}
