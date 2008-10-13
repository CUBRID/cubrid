package cubridmanager.cas.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.SWT;
import cubridmanager.Messages;
import cubridmanager.MainRegistry;
import cubridmanager.cas.CASItem;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class BROKERADD_PAGE1Dialog extends WizardPage {
	public static final String PAGE_NAME = "BROKERADD_PAGE1Dialog";
	private Composite comparent = null;
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Label label1 = null;
	public static Text EDIT_BROKER_ADD_BNAME = null;
	private Label label2 = null;
	public static Text EDIT_BROKER_ADD_PORT = null;
	private CLabel clabel1 = null;
	private Label label3 = null;
	public static Combo COMBO_BROKER_ADD_ASTYPE = null;
	private Label label4 = null;
	public static Text EDIT_BROKER_ADD_ASMIN = null;
	private Label label5 = null;
	public static Text EDIT_BROKER_ADD_ASMAX = null;
	private Label label6 = null;
	public static Text EDIT_BROKER_ADD_APPL_ROOT = null;
	private Group group2 = null;
	private Label label7 = null;
	public static boolean isready = false;
	public static int SHMID = 0;

	public BROKERADD_PAGE1Dialog() {
		super(PAGE_NAME, Messages.getString("TITLE.BROKERADD_PAGE1DIALOG"),
				null);
		isready = false;
	}

	public void createControl(Composite parent) {
		comparent = parent;
		createComposite();
		sShell.setParent(parent);
		setControl(sShell);
		setPageComplete(true); // <-last page is false, others true. 
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
		dlgShell.setText(Messages.getString("TITLE.BROKERADD_PAGE1DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData24 = new org.eclipse.swt.layout.GridData();
		gridData24.horizontalSpan = 3;
		gridData24.widthHint = 320;
		GridData gridData23 = new org.eclipse.swt.layout.GridData();
		gridData23.widthHint = 110;
		GridData gridData22 = new org.eclipse.swt.layout.GridData();
		gridData22.widthHint = 100;
		gridData22.grabExcessHorizontalSpace = false;
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.horizontalSpan = 4;
		gridData21.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData21.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData21.heightHint = 3;
		gridData21.grabExcessHorizontalSpace = true;
		GridData gridData20 = new org.eclipse.swt.layout.GridData();
		gridData20.widthHint = 110;
		gridData20.grabExcessHorizontalSpace = true;
		GridData gridData19 = new org.eclipse.swt.layout.GridData();
		gridData19.grabExcessHorizontalSpace = true;
		gridData19.widthHint = 100;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		GridData gridData18 = new org.eclipse.swt.layout.GridData();
		gridData18.widthHint = 400;
		gridData18.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData18.grabExcessHorizontalSpace = true;
		gridData18.heightHint = 100;
		gridData18.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessHorizontalSpace = true;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.grabExcessVerticalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.grabExcessHorizontalSpace = true;
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
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.BROKERNAME"));
		EDIT_BROKER_ADD_BNAME = new Text(group1, SWT.BORDER);
		EDIT_BROKER_ADD_BNAME.setLayoutData(gridData19);
		EDIT_BROKER_ADD_BNAME
				.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusGained(org.eclipse.swt.events.FocusEvent e) {
						label7.setText(Messages
								.getString("TOOLTIP.EDITBROKERADDBNAME"));
					}
				});
		EDIT_BROKER_ADD_BNAME
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						String bname = EDIT_BROKER_ADD_BNAME.getText();
						if (bname.length() > 0) {
							EDIT_BROKER_ADD_PORT.setEnabled(true);
						} else {
							EDIT_BROKER_ADD_PORT.setEnabled(false);
						}
					}
				});
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.BROKERPORT"));
		EDIT_BROKER_ADD_PORT = new Text(group1, SWT.BORDER);
		EDIT_BROKER_ADD_PORT.setTextLimit(5);
		EDIT_BROKER_ADD_PORT.setLayoutData(gridData20);
		EDIT_BROKER_ADD_PORT
				.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusGained(org.eclipse.swt.events.FocusEvent e) {
						label7.setText(Messages
								.getString("TOOLTIP.EDITBROKERADDPORT"));
					}
				});
		clabel1 = new CLabel(group1, SWT.SHADOW_IN);
		clabel1.setLayoutData(gridData21);
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.ASTYPE"));
		createCombo1();
		label4 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.ASMINIMUM"));
		EDIT_BROKER_ADD_ASMIN = new Text(group1, SWT.BORDER);
		EDIT_BROKER_ADD_ASMIN.setTextLimit(4);
		EDIT_BROKER_ADD_ASMIN.setLayoutData(gridData22);
		EDIT_BROKER_ADD_ASMIN
				.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusGained(org.eclipse.swt.events.FocusEvent e) {
						label7.setText(Messages
								.getString("TOOLTIP.EDITBROKERADDASMIN"));
					}
				});
		label5 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.ASMAXIMUM"));
		EDIT_BROKER_ADD_ASMAX = new Text(group1, SWT.BORDER);
		EDIT_BROKER_ADD_ASMAX.setTextLimit(4);
		EDIT_BROKER_ADD_ASMAX.setLayoutData(gridData23);
		EDIT_BROKER_ADD_ASMAX
				.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusGained(org.eclipse.swt.events.FocusEvent e) {
						label7.setText(Messages
								.getString("TOOLTIP.EDITBROKERADDASMAX"));
					}
				});
		label6 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.APPLROOT"));
		EDIT_BROKER_ADD_APPL_ROOT = new Text(group1, SWT.BORDER);
		EDIT_BROKER_ADD_APPL_ROOT.setLayoutData(gridData24);
		EDIT_BROKER_ADD_APPL_ROOT
				.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusGained(org.eclipse.swt.events.FocusEvent e) {
						label7.setText(Messages
								.getString("TOOLTIP.EDITBROKERADDAPPLROOT"));
					}
				});
		label7 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label7.setLayoutData(gridData18);
		sShell.pack();
	}

	private void createCombo1() {
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 100;
		gridData2.horizontalSpan = 3;
		COMBO_BROKER_ADD_ASTYPE = new Combo(group1, SWT.DROP_DOWN);
		COMBO_BROKER_ADD_ASTYPE.setLayoutData(gridData2);
		COMBO_BROKER_ADD_ASTYPE
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						String ap = COMBO_BROKER_ADD_ASTYPE.getText();
						if (ap.equals("CAS")) {
							EDIT_BROKER_ADD_APPL_ROOT.setEnabled(false);
							EDIT_BROKER_ADD_APPL_ROOT.setText("");
						} else {
							EDIT_BROKER_ADD_APPL_ROOT.setEnabled(true);
							EDIT_BROKER_ADD_APPL_ROOT.setText("$BROKER/script/"
									+ ap.toLowerCase());
						}
					}
				});
		COMBO_BROKER_ADD_ASTYPE
				.addFocusListener(new org.eclipse.swt.events.FocusAdapter() {
					public void focusGained(org.eclipse.swt.events.FocusEvent e) {
						label7.setText(Messages
								.getString("TOOLTIP.COMBOBROKERADDASTYPE"));
					}
				});
	}

	private void setinfo() {
		COMBO_BROKER_ADD_ASTYPE.add("CAS", 0);
		COMBO_BROKER_ADD_ASTYPE.add("VAS", 1);
		COMBO_BROKER_ADD_ASTYPE.add("WAS", 2);
		COMBO_BROKER_ADD_ASTYPE.add("ULS", 3);
		COMBO_BROKER_ADD_ASTYPE.add("AMS", 4);
		COMBO_BROKER_ADD_ASTYPE.select(0);

		EDIT_BROKER_ADD_APPL_ROOT.setEnabled(false);
		EDIT_BROKER_ADD_ASMIN.setText("5");
		EDIT_BROKER_ADD_ASMAX.setText("20");

		CASItem casrec;
		int maxport = 0;
		for (int i = 0, n = MainRegistry.CASinfo.size(); i < n; i++) {
			casrec = (CASItem) MainRegistry.CASinfo.get(i);
			if (casrec.broker_port > maxport)
				maxport = casrec.broker_port;
		}
		SHMID = maxport + 1000;
		EDIT_BROKER_ADD_PORT.setText(Integer.toString(maxport + 1000));
	}
}
