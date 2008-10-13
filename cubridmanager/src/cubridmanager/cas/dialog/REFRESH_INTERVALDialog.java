package cubridmanager.cas.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;
import cubridmanager.Messages;
import cubridmanager.CommonTool;
import cubridmanager.action.RefreshIntervalAction;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class REFRESH_INTERVALDialog extends Dialog {
	public static int MIN_BROKER_REFRESH_SEC = 1;
	public int time_interval = 0;
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Button RADIO_INTERVAL_ON = null;
	private Button RADIO_INTERVAL_OFF = null;
	private Text EDIT_INTERVAL_SECOND = null;
	private Label label1 = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private Group group1 = null;
	private Label label2 = null;
	private boolean ret = false;

	public REFRESH_INTERVALDialog(Shell parent) {
		super(parent);
	}

	public REFRESH_INTERVALDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		if (time_interval >= MIN_BROKER_REFRESH_SEC) {
			RADIO_INTERVAL_ON.setSelection(true);
			EDIT_INTERVAL_SECOND.setText(Integer.toString(time_interval));
			EDIT_INTERVAL_SECOND.setEnabled(true);
		} else {
			RADIO_INTERVAL_OFF.setSelection(true);
			EDIT_INTERVAL_SECOND.setEnabled(false);
		}
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
		dlgShell.setText(Messages.getString("TOOL.REFRESHINTERVAL"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData51 = new org.eclipse.swt.layout.GridData();
		gridData51.horizontalSpan = 3;
		GridData gridData50 = new org.eclipse.swt.layout.GridData();
		gridData50.widthHint = 130;
		gridData50.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData50.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridLayout gridLayout49 = new GridLayout();
		gridLayout49.numColumns = 3;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 100;
		gridData3.grabExcessHorizontalSpace = true;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.widthHint = 100;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		label2 = new Label(sShell, SWT.CENTER | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.REFRESHINTERVALOF"));
		label2.setLayoutData(gridData);

		group1 = new Group(sShell, SWT.NONE);
		group1.setLayoutData(gridData1);
		group1.setLayout(gridLayout49);
		RADIO_INTERVAL_ON = new Button(group1, SWT.RADIO);
		RADIO_INTERVAL_ON.setText(Messages.getString("RADIO.ON"));
		RADIO_INTERVAL_ON
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						EDIT_INTERVAL_SECOND.setEnabled(true);
					}
				});
		EDIT_INTERVAL_SECOND = new Text(group1, SWT.BORDER);
		EDIT_INTERVAL_SECOND.setLayoutData(gridData50);
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.SEC"));
		RADIO_INTERVAL_OFF = new Button(group1, SWT.RADIO);
		RADIO_INTERVAL_OFF.setText(Messages.getString("RADIO.OFF"));
		RADIO_INTERVAL_OFF.setLayoutData(gridData51);
		RADIO_INTERVAL_OFF
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						EDIT_INTERVAL_SECOND.setEnabled(false);
					}
				});
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData2);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int tmpint = CommonTool.atoi(EDIT_INTERVAL_SECOND
								.getText());
						if (tmpint < MIN_BROKER_REFRESH_SEC
								&& RADIO_INTERVAL_ON.getSelection()) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INTERVALISTOOSMALL"));
						} else {
							if (!RADIO_INTERVAL_ON.getSelection())
								RefreshIntervalAction.time_interval = 0;
							else
								RefreshIntervalAction.time_interval = tmpint;
							dlgShell.dispose();
						}
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData3);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

}
