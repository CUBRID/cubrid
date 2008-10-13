package cubridmanager.cubrid.dialog;

import java.text.NumberFormat;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.VerifyDigitListener;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Combo;
import cubridmanager.CommonTool;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.view.CubridView;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class ADDVOLDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Label label1 = null;
	private Text EDIT_ADDVOL_PATH = null;
	private Label label2 = null;
	private Text EDIT_ADDVOL_PAGE = null;
	private Text EDIT_ADDVOL_MB = null;
	private Label label3 = null;
	private Label label4 = null;
	private Combo COMBO_ADDVOL_PURPOSE = null;
	private Button btnOK = null;
	private Button IDCANCEL = null;
	private AuthItem ai = null;
	private int mb = 0;
	private Composite composite = null;

	public ADDVOLDialog(Shell parent) {
		super(parent);
	}

	public ADDVOLDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createDlgShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.open();

		setinfo();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createDlgShell() {
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.ADDVOLDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createSShell();
	}

	private void createSShell() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);

		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayout(new FillLayout());
		group1.setLayoutData(gridData);

		createComposite();

		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 70;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData3.grabExcessHorizontalSpace = true;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		btnOK = new Button(sShell, SWT.NONE);
		btnOK.setText(Messages.getString("BUTTON.OK"));
		btnOK.setEnabled(false);
		btnOK.setLayoutData(gridData3);
		btnOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CommonTool.atof(EDIT_ADDVOL_PAGE.getText()) < 3.0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("MSG.ADDVOLPAGEMIN"));
							return;
						}
						/*
						 * if (CommonTool.atoi(MainRegistry.TmpVolsize)<mb) {
						 * CommonTool.ErrorBox(dlgShell,
						 * Messages.getString("ERROR.VOLUMESIZETOOBIG"));
						 * return; }
						 */
						ClientSocket cs = new ClientSocket();
						String requestMsg = "dir:" + EDIT_ADDVOL_PATH.getText();
						if (!cs
								.SendBackGround(
										dlgShell,
										requestMsg,
										"checkdir",
										Messages
												.getString("WAITING.CHECKINGDIRECTORY"))) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}
						dlgShell.update();
						if (MainRegistry.Tmpchkrst.size() > 0) { // create
																	// directory
																	// confirm
							NEWDIRECTORYDialog newdlg = new NEWDIRECTORYDialog(
									dlgShell);
							if (newdlg.doModal() == 0)
								return; // cancel
						}

						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "volname:\n";
						msg += "purpose:" + COMBO_ADDVOL_PURPOSE.getText()
								+ "\n";
						msg += "path:" + EDIT_ADDVOL_PATH.getText() + "\n";
						msg += "numberofpages:" + EDIT_ADDVOL_PAGE.getText()
								+ "\n";
						msg += "size_need_mb:" + EDIT_ADDVOL_MB.getText()
								+ "\n";
						cs = new ClientSocket();
						if (!cs.SendBackGround(dlgShell, msg, "addvoldb",
								Messages.getString("WAITING.ADDVOL"))) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}
						dlgShell.update();
						CommonTool.MsgBox(dlgShell, Messages
								.getString("MSG.SUCCESS"), Messages
								.getString("MSG.ADDVOLSUCCESS"));
						cs = new ClientSocket();
						if (!cs.SendClientMessage(dlgShell, "dbname:"
								+ CubridView.Current_db, "dbspaceinfo")) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}
						CubridView.myNavi.createModel();
						CubridView.viewer.refresh();
						dlgShell.dispose();
					}
				});

		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 70;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData4.grabExcessHorizontalSpace = true;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData4);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	/**
	 * This method initializes composite
	 * 
	 */
	private void createComposite() {
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.widthHint = 114;

		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 3;
		composite = new Composite(group1, SWT.NONE);
		composite.setLayout(gridLayout1);

		label1 = new Label(composite, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.PATH"));
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.horizontalSpan = 2;
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData7.widthHint = 184;
		EDIT_ADDVOL_PATH = new Text(composite, SWT.BORDER);
		EDIT_ADDVOL_PATH.setLayoutData(gridData7);

		label2 = new Label(composite, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.PAGES"));
		EDIT_ADDVOL_PAGE = new Text(composite, SWT.BORDER | SWT.RIGHT);
		EDIT_ADDVOL_PAGE.setLayoutData(gridData8);
		EDIT_ADDVOL_PAGE.addListener(SWT.Verify, new VerifyDigitListener());
		EDIT_ADDVOL_PAGE
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						if (ai == null)
							return;
						String inpage = EDIT_ADDVOL_PAGE.getText();
						double pages = (inpage == null) ? 0 : CommonTool
								.atof(inpage);
						double mbs = pages * ai.pagesize
								/ MainConstants.MEGABYTES;
						mb = (int) (pages * ai.pagesize / MainConstants.MEGABYTES);
						NumberFormat nf = NumberFormat.getInstance();
						nf.setMaximumFractionDigits(2);
						nf.setGroupingUsed(false);
						EDIT_ADDVOL_MB.setText(nf.format(mbs) + "(MB)");
						btnOK.setEnabled(true);
					}
				});

		label3 = new Label(composite, SWT.LEFT | SWT.WRAP);
		ai = MainRegistry.Authinfo_find(CubridView.Current_db);
		String msg = "(" + Messages.getString("LABEL.PAGESIZE") + ai.pagesize
				+ " Bytes)";
		label3.setText(msg);

		label3 = new Label(composite, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.VOLUMESIZE"));

		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.widthHint = 148;
		gridData9.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		EDIT_ADDVOL_MB = new Text(composite, SWT.BORDER | SWT.RIGHT);
		EDIT_ADDVOL_MB.setEditable(false);
		EDIT_ADDVOL_MB.setLayoutData(gridData9);

		label3 = new Label(composite, SWT.LEFT | SWT.WRAP);
		// msg = "(" +
		// Messages.getString("LABEL.FREESPACE")+MainRegistry.TmpVolsize+" MB)";
		// label3.setText(msg);
		label3.setText("");
		label4 = new Label(composite, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.PURPOSE"));
		createCombo1();
	}

	private void createCombo1() {
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.horizontalSpan = 2;
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData11.widthHint = 154;
		COMBO_ADDVOL_PURPOSE = new Combo(composite, SWT.DROP_DOWN
				| SWT.READ_ONLY);
		COMBO_ADDVOL_PURPOSE.setLayoutData(gridData11);
	}

	private void setinfo() {
		EDIT_ADDVOL_PATH.setText(MainRegistry.TmpVolpath);
		COMBO_ADDVOL_PURPOSE.add("data", 0);
		COMBO_ADDVOL_PURPOSE.add("generic", 1);
		COMBO_ADDVOL_PURPOSE.add("index", 2);
		COMBO_ADDVOL_PURPOSE.add("temp", 3);
		COMBO_ADDVOL_PURPOSE.select(0);
		EDIT_ADDVOL_PATH.setToolTipText(Messages
				.getString("TOOLTIP.ADDVOLEDITPATH"));
		EDIT_ADDVOL_PAGE.setToolTipText(Messages
				.getString("TOOLTIP.ADDVOLEDITPAGE"));
		COMBO_ADDVOL_PURPOSE.setToolTipText(Messages
				.getString("TOOLTIP.ADDVOLCOMBOPURPOSE"));
		EDIT_ADDVOL_MB.setToolTipText(Messages
				.getString("TOOLTIP.ADDVOLEDITMBYTES"));

		EDIT_ADDVOL_PAGE.setText("10000");
		EDIT_ADDVOL_PAGE.setFocus();
	}

	private boolean checkPage() {
		if (CommonTool.atoi(EDIT_ADDVOL_PAGE.getText()) > 3
				&& CommonTool.atoi(MainRegistry.TmpVolsize) > mb)
			return true;
		else
			return false;
	}
}
