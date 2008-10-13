package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.SWT;

import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.VerifyDigitListener;
import cubridmanager.cubrid.action.LoadAction;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class LOADDB_PAGE1Dialog extends WizardPage {
	public static final String PAGE_NAME = "LOADDB_PAGE1Dialog";
	private Composite comparent = null;
	private Shell dlgShell = null; // @jve:decl-index=0:visual-constraint="10,61"
	private Composite sShell = null;
	private Label label1 = null;
	public Text EDIT_LOADDB_DBNAME = null;
	private Group group1 = null;
	public Button RADIO_LOADDB_BOTH = null;
	public Button RADIO_LOADDB_LOADONLY = null;
	public Button RADIO_LOADDB_DATAFILEONLY = null;
	public Button CHECK_COMMIT_PERIOD = null;
	public Text EDIT_LOADDB_PERIOD = null;
	private Label label3 = null;
	public Text EDIT_LOADDB_USER = null;
	private CLabel label5 = null;
	public Button CHECK_ESTIMATED_SIZE = null;
	public Text EDIT_ESTIMATED_SIZE = null;
	public Button CHECK_OID_IS_NOT_USE = null;
	public Button CHECK_NO_LOG = null;

	public LOADDB_PAGE1Dialog() {
		super(PAGE_NAME, Messages.getString("PAGE.LOADDBPAGE1"), null);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.LOADDB_PAGE1DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData75 = new org.eclipse.swt.layout.GridData();
		gridData75.horizontalIndent = 40;
		GridData gridData74 = new org.eclipse.swt.layout.GridData();
		gridData74.horizontalIndent = 40;
		GridData gridData73 = new org.eclipse.swt.layout.GridData();
		gridData73.horizontalSpan = 2;
		gridData73.horizontalAlignment = GridData.FILL;
		gridData73.grabExcessHorizontalSpace = true;
		GridData gridData72 = new org.eclipse.swt.layout.GridData();
		gridData72.widthHint = 150;
		gridData72.grabExcessVerticalSpace = true;
		GridData gridData71 = new org.eclipse.swt.layout.GridData();
		gridData71.widthHint = 150;
		gridData71.grabExcessVerticalSpace = true;
		GridData gridData70 = new org.eclipse.swt.layout.GridData();
		gridData70.horizontalSpan = 2;
		gridData70.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData70.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData70.grabExcessHorizontalSpace = true;
		gridData70.heightHint = 3;
		GridData gridData69 = new org.eclipse.swt.layout.GridData();
		gridData69.horizontalSpan = 2;
		gridData69.horizontalIndent = 20;
		gridData69.grabExcessVerticalSpace = true;
		GridData gridData68 = new org.eclipse.swt.layout.GridData();
		gridData68.horizontalSpan = 2;
		gridData68.horizontalIndent = 20;
		gridData68.grabExcessVerticalSpace = true;
		GridData gridData67 = new org.eclipse.swt.layout.GridData();
		gridData67.horizontalSpan = 2;
		gridData67.horizontalIndent = 20;
		gridData67.grabExcessVerticalSpace = true;
		GridLayout gridLayout66 = new GridLayout();
		gridLayout66.numColumns = 2;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 2;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData2.grabExcessVerticalSpace = true;
		gridData2.grabExcessHorizontalSpace = true;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessHorizontalSpace = true;
		gridData1.grabExcessVerticalSpace = true;
		gridData1.widthHint = 150;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData.grabExcessHorizontalSpace = true;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out for VE
		sShell.setLayout(gridLayout);
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.TARGETDATABASE"));
		label1.setLayoutData(gridData);
		EDIT_LOADDB_DBNAME = new Text(sShell, SWT.BORDER);
		EDIT_LOADDB_DBNAME.setEditable(false);
		EDIT_LOADDB_DBNAME.setLayoutData(gridData1);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.LOADOPTION"));
		group1.setLayout(gridLayout66);
		group1.setLayoutData(gridData2);
		RADIO_LOADDB_BOTH = new Button(group1, SWT.RADIO);
		RADIO_LOADDB_BOTH.setText(Messages.getString("RADIO.DATAFILECHECKAND"));
		RADIO_LOADDB_BOTH.setLayoutData(gridData67);
		RADIO_LOADDB_LOADONLY = new Button(group1, SWT.RADIO);
		RADIO_LOADDB_LOADONLY.setText(Messages.getString("RADIO.LOADONLY"));
		RADIO_LOADDB_LOADONLY.setLayoutData(gridData68);
		RADIO_LOADDB_DATAFILEONLY = new Button(group1, SWT.RADIO);
		RADIO_LOADDB_DATAFILEONLY.setText(Messages
				.getString("RADIO.DATAFILECHECKONLY"));
		RADIO_LOADDB_DATAFILEONLY.setLayoutData(gridData69);
		label5 = new CLabel(group1, SWT.SHADOW_IN);
		label5.setLayoutData(gridData70);
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.USERNAME"));
		label3.setLayoutData(gridData75);
		EDIT_LOADDB_USER = new Text(group1, SWT.BORDER);
		EDIT_LOADDB_USER.setLayoutData(gridData72);
		CHECK_COMMIT_PERIOD = new Button(group1, SWT.CHECK);
		CHECK_COMMIT_PERIOD.setText(Messages.getString("CHECK.COMMITPERIOD"));
		CHECK_COMMIT_PERIOD.setLayoutData(gridData74);
		CHECK_COMMIT_PERIOD.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (CHECK_COMMIT_PERIOD.getSelection()) {
					EDIT_LOADDB_PERIOD.setText("10000");
					EDIT_LOADDB_PERIOD.setEnabled(true);
				} else {
					EDIT_LOADDB_PERIOD.setText("");
					EDIT_LOADDB_PERIOD.setEnabled(false);
				}

			}
		});
		EDIT_LOADDB_PERIOD = new Text(group1, SWT.BORDER);
		EDIT_LOADDB_PERIOD.addListener(SWT.Verify, new VerifyDigitListener());
		EDIT_LOADDB_PERIOD.setTextLimit(9); // String.valueOf(Integer.MAX_VALUE).length()-1
											// = 9
		EDIT_LOADDB_PERIOD.setLayoutData(gridData71);
		CHECK_ESTIMATED_SIZE = new Button(group1, SWT.CHECK);
		CHECK_ESTIMATED_SIZE.setText(Messages.getString("CHECK.ESTIMATEDSIZE"));
		CHECK_ESTIMATED_SIZE.setLayoutData(gridData75);
		CHECK_ESTIMATED_SIZE.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (CHECK_ESTIMATED_SIZE.getSelection()) {
					EDIT_ESTIMATED_SIZE.setText("5000");
					EDIT_ESTIMATED_SIZE.setEnabled(true);
				} else {
					EDIT_ESTIMATED_SIZE.setText("");
					EDIT_ESTIMATED_SIZE.setEnabled(false);
				}

			}
		});
		EDIT_ESTIMATED_SIZE = new Text(group1, SWT.BORDER);
		EDIT_ESTIMATED_SIZE.addListener(SWT.Verify, new VerifyDigitListener());
		EDIT_ESTIMATED_SIZE.setTextLimit(9);
		EDIT_ESTIMATED_SIZE.setLayoutData(gridData72);
		CHECK_OID_IS_NOT_USE = new Button(group1, SWT.CHECK);
		CHECK_OID_IS_NOT_USE.setText(Messages.getString("CHECK.OIDISNOTUSE"));
		CHECK_OID_IS_NOT_USE.setLayoutData(gridData73);

		GridData gridData76 = new org.eclipse.swt.layout.GridData();
		gridData76.horizontalSpan = 2;
		gridData76.horizontalAlignment = GridData.FILL;
		gridData76.grabExcessHorizontalSpace = true;
		CHECK_NO_LOG = new Button(group1, SWT.CHECK);
		CHECK_NO_LOG.setText(Messages.getString("CHECK.NOLOG"));
		CHECK_NO_LOG.setLayoutData(gridData76);
		CHECK_NO_LOG.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (CHECK_NO_LOG.getSelection())
					CommonTool.WarnBox(CHECK_NO_LOG.getShell(), Messages
							.getString("MSG.NOLOG"));
			}
		});

		setinfo();
		sShell.pack();
	}

	private void setinfo() {
		EDIT_LOADDB_DBNAME.setText(LoadAction.ai.dbname);
		RADIO_LOADDB_BOTH.setSelection(true);
		EDIT_LOADDB_USER.setText("dba");
		EDIT_LOADDB_USER.setEnabled(false);
		EDIT_LOADDB_PERIOD.setEnabled(false);
		EDIT_ESTIMATED_SIZE.setEnabled(false);

		EDIT_LOADDB_DBNAME.setToolTipText(Messages
				.getString("TOOLTIP.EDITDBNAME"));
		EDIT_LOADDB_PERIOD.setToolTipText(Messages
				.getString("TOOLTIP.EDITPERIOD"));
		EDIT_LOADDB_USER.setToolTipText(Messages
				.getString("TOOLTIP.EDITUSERNAME"));
		EDIT_ESTIMATED_SIZE.setToolTipText(Messages
				.getString("TOOLTIP.ESTIMATEDSIZE"));
		CHECK_OID_IS_NOT_USE.setToolTipText(Messages
				.getString("TOOLTIP.OIDISNOTUSE"));
		CHECK_NO_LOG.setToolTipText(Messages.getString("TOOLTIP.NOLOG"));
	}

	public void createControl(Composite parent) {
		comparent = parent;
		createComposite();
		sShell.setParent(parent);
		setControl(sShell);
		setPageComplete(true);
	}
}
