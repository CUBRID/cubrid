package cubridmanager.cas.dialog;

import java.util.ArrayList;

import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;
import cubridmanager.Messages;
import cubridmanager.CommonTool;
import cubridmanager.cas.BrokerAS;
import cubridmanager.cas.view.BrokerJob;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class BROKER_APPRESTARTDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Combo EDIT_ASNUM = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private int ret = -1;
	private Label lblWarning = null;

	public BROKER_APPRESTARTDialog(Shell parent) {
		super(parent);
	}

	public BROKER_APPRESTARTDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		IDOK.setEnabled(false);

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void createSShell() {
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TOOL.RESTARTAPSERVERACTION"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		gridLayout.makeColumnsEqualWidth = true;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);

		GridData gridLblWarning = new GridData();
		gridLblWarning.grabExcessHorizontalSpace = true;
		gridLblWarning.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridLblWarning.horizontalSpan = 2;
		lblWarning = new Label(sShell, SWT.WRAP | SWT.BORDER | SWT.CENTER);
		lblWarning.setText(Messages.getString("LABEL.RESTARTAP"));
		lblWarning.setLayoutData(gridLblWarning);

		GridData gridLabel1 = new GridData();
		gridLabel1.grabExcessHorizontalSpace = true;
		gridLabel1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.APPLICATIONSERVER"));
		label1.setLayoutData(gridLabel1);

		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.widthHint = 70;
		EDIT_ASNUM = new Combo(sShell, SWT.BORDER | SWT.READ_ONLY);
		EDIT_ASNUM.setLayoutData(gridData);
		ArrayList asinfo = BrokerJob.asinfo;

		for (int i = 0, n = asinfo.size(); i < n; i++) {
			String id = ((BrokerAS) asinfo.get(i)).ID;
			EDIT_ASNUM.add(id);
		}

		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.widthHint = 80;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData1);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = CommonTool.atoi(EDIT_ASNUM.getText());
						dlgShell.dispose();
					}
				});

		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 80;
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData2);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = -1;
						dlgShell.dispose();
					}
				});

		EDIT_ASNUM
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						IDOK.setEnabled(true);
					}
				});

		dlgShell.pack();
	}

}
