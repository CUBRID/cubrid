package cubridmanager.cas.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.SWT;
import cubridmanager.Messages;
import cubridmanager.cas.action.SetParameterAction;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class PROPPAGE1_BROKER_OFFSETDialog extends WizardPage {
	public static final String PAGE_NAME = "PROPPAGE1_BROKER_OFFSETDialog";
	private Composite comparent = null;
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Group group1 = null;
	private Label label2 = null;
	public static Text EDIT_BROKER_OFFSET_BNAME = null;
	private Label label3 = null;
	public static Text EDIT_BROKER_OFFSET_PORT = null;
	private CLabel clabel1 = null;
	private Label label4 = null;
	public static Text EDIT_BROKER_OFFSET_ASTYPE = null;
	private Label label5 = null;
	public static Text EDIT_BROKER_OFFSET_ASMIN = null;
	private Label label6 = null;
	public static Text EDIT_BROKER_OFFSET_ASMAX = null;
	private Label label7 = null;
	public static Text EDIT_BROKER_OFFSET_APPL_ROOT = null;
	private Group group2 = null;

	public static boolean isready = false;

	public PROPPAGE1_BROKER_OFFSETDialog() {
		super(PAGE_NAME, Messages
				.getString("TITLE.PROPPAGE1_BROKER_OFFSETDIALOG"), null);
	}

	public void createControl(Composite parent) {
		comparent = parent;
		createComposite();
		sShell.setParent(parent);
		setControl(sShell);
		setPageComplete(true);
		setinfo();
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
				.getString("TITLE.PROPPAGE1_BROKER_OFFSETDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData33 = new org.eclipse.swt.layout.GridData();
		gridData33.horizontalSpan = 3;
		gridData33.widthHint = 300;
		GridData gridData32 = new org.eclipse.swt.layout.GridData();
		gridData32.widthHint = 100;
		GridData gridData31 = new org.eclipse.swt.layout.GridData();
		gridData31.widthHint = 100;
		GridData gridData30 = new org.eclipse.swt.layout.GridData();
		gridData30.widthHint = 100;
		gridData30.horizontalSpan = 3;
		GridData gridData29 = new org.eclipse.swt.layout.GridData();
		gridData29.horizontalSpan = 4;
		gridData29.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData29.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData29.heightHint = 3;
		gridData29.grabExcessHorizontalSpace = true;
		GridData gridData28 = new org.eclipse.swt.layout.GridData();
		gridData28.widthHint = 100;
		gridData28.grabExcessHorizontalSpace = true;
		GridData gridData27 = new org.eclipse.swt.layout.GridData();
		gridData27.grabExcessHorizontalSpace = true;
		gridData27.widthHint = 100;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.heightHint = 100;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.grabExcessVerticalSpace = false;
		gridData2.widthHint = 400;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessHorizontalSpace = true;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.grabExcessVerticalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.grabExcessVerticalSpace = true;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
		sShell.setLayout(new GridLayout());
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.BASICINFORMATION"));
		group1.setLayout(gridLayout);
		group1.setLayoutData(gridData);
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.DESCRIPTION"));
		group2.setLayout(new GridLayout());
		group2.setLayoutData(gridData1);
		label1 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label1.setLayoutData(gridData2);
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.BROKERNAME"));
		EDIT_BROKER_OFFSET_BNAME = new Text(group1, SWT.BORDER);
		EDIT_BROKER_OFFSET_BNAME.setLayoutData(gridData27);
		EDIT_BROKER_OFFSET_BNAME.setEditable(false);
		EDIT_BROKER_OFFSET_BNAME
				.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusGained(org.eclipse.swt.events.FocusEvent e) {
						label1.setText(Messages
								.getString("TOOLTIP.EDITBROKERADDBNAME"));
					}
				});

		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.BROKERPORT"));
		EDIT_BROKER_OFFSET_PORT = new Text(group1, SWT.BORDER);
		EDIT_BROKER_OFFSET_PORT.setTextLimit(5);
		EDIT_BROKER_OFFSET_PORT.setLayoutData(gridData28);
		EDIT_BROKER_OFFSET_PORT
				.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusGained(org.eclipse.swt.events.FocusEvent e) {
						label1.setText(Messages
								.getString("TOOLTIP.EDITBROKERADDPORT"));
					}
				});
		clabel1 = new CLabel(group1, SWT.SHADOW_IN);
		clabel1.setLayoutData(gridData29);
		label4 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.ASTYPE"));
		EDIT_BROKER_OFFSET_ASTYPE = new Text(group1, SWT.BORDER);
		EDIT_BROKER_OFFSET_ASTYPE.setEditable(false);
		EDIT_BROKER_OFFSET_ASTYPE.setLayoutData(gridData30);
		label5 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.ASMINIMUM"));
		EDIT_BROKER_OFFSET_ASMIN = new Text(group1, SWT.BORDER);
		EDIT_BROKER_OFFSET_ASMIN.setTextLimit(4);
		EDIT_BROKER_OFFSET_ASMIN.setLayoutData(gridData31);
		EDIT_BROKER_OFFSET_ASMIN
				.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusGained(org.eclipse.swt.events.FocusEvent e) {
						label1.setText(Messages
								.getString("TOOLTIP.EDITBROKERADDASMIN"));
					}
				});
		label6 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.ASMAXIMUM"));
		EDIT_BROKER_OFFSET_ASMAX = new Text(group1, SWT.BORDER);
		EDIT_BROKER_OFFSET_ASMAX.setTextLimit(4);
		EDIT_BROKER_OFFSET_ASMAX.setLayoutData(gridData32);
		EDIT_BROKER_OFFSET_ASMAX
				.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusGained(org.eclipse.swt.events.FocusEvent e) {
						label1.setText(Messages
								.getString("TOOLTIP.EDITBROKERADDASMAX"));
					}
				});
		label7 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label7.setText(Messages.getString("LABEL.APPLROOT"));
		EDIT_BROKER_OFFSET_APPL_ROOT = new Text(group1, SWT.BORDER);
		EDIT_BROKER_OFFSET_APPL_ROOT.setLayoutData(gridData33);
		EDIT_BROKER_OFFSET_APPL_ROOT
				.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusGained(org.eclipse.swt.events.FocusEvent e) {
						label1.setText(Messages
								.getString("TOOLTIP.EDITBROKERADDAPPLROOT"));
					}
				});
		sShell.pack();
	}

	private void setinfo() {
		EDIT_BROKER_OFFSET_BNAME.setText(SetParameterAction.bpi.broker_name);
		EDIT_BROKER_OFFSET_PORT.setText(SetParameterAction.bpi.broker_port);
		EDIT_BROKER_OFFSET_ASMIN.setText(SetParameterAction.bpi.min_as_num);
		EDIT_BROKER_OFFSET_ASMAX.setText(SetParameterAction.bpi.max_as_num);

		EDIT_BROKER_OFFSET_ASTYPE.setText(SetParameterAction.bpi.server_type);
		if (SetParameterAction.bpi.server_type.equals("CAS")) {
			EDIT_BROKER_OFFSET_APPL_ROOT.setEnabled(false);
		} else {
			EDIT_BROKER_OFFSET_APPL_ROOT
					.setText(SetParameterAction.bpi.appl_root);
		}
	}
}
