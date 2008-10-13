package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.cubrid.dialog.PROPPAGE_CLASS_PAGE4Dialog;
import cubridmanager.cubrid.view.CubridView;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class ADD_QUERYDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Text EDIT_ADD_QUERY_SPEC = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private Text EDIT_ADD_QUERY_DESC = null;
	private boolean ret = false;
	private CLabel cLabel = null;

	public ADD_QUERYDialog(Shell parent) {
		super(parent);
	}

	public ADD_QUERYDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
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
		return ret;
	}

	private void createSShell() {
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.ADD_QUERYDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.heightHint = 248;
		gridData5.widthHint = 406;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 100;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 100;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.grabExcessHorizontalSpace = true;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 3;
		gridData.widthHint = 432;
		gridData.heightHint = 24;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		EDIT_ADD_QUERY_DESC = new Text(sShell, SWT.NONE);
		EDIT_ADD_QUERY_DESC.setEditable(false);
		EDIT_ADD_QUERY_DESC.setLayoutData(gridData);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.ENTERAQUERYSPECIFICATION"));
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData1);
		EDIT_ADD_QUERY_SPEC = new Text(group1, SWT.BORDER | SWT.MULTI
				| SWT.V_SCROLL | SWT.H_SCROLL | SWT.WRAP);
		EDIT_ADD_QUERY_SPEC.setLayoutData(gridData5);
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData2);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData3);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String msg = "dbname:" + CubridView.Current_db + "\n";
						String cmds = "", wmsg = "";
						msg += "vclassname:"
								+ PROPPAGE_CLASS_PAGE4Dialog.si.name + "\n";
						if (!PROPPAGE_CLASS_PAGE4Dialog.isadd) {
							msg += "querynumber:"
									+ (PROPPAGE_CLASS_PAGE4Dialog.CurrentLIST_CLASS_QUERYSPECSidx + 1)
									+ "\n";
							cmds = "changequeryspec";
							wmsg = Messages.getString("WAITING.CHGQUERYSPEC");
						} else {
							cmds = "addqueryspec";
							wmsg = Messages.getString("WAITING.ADDQUERYSPEC");
						}
						String qspec = EDIT_ADD_QUERY_SPEC.getText().trim();
						qspec = qspec.replaceAll("\r\n", "\n");
						qspec = qspec.replaceAll("\n", " ");
						msg += "queryspec:" + qspec + "\n";

						ClientSocket cs = new ClientSocket();

						if (!cs.SendBackGround(dlgShell, msg, cmds, wmsg)) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}

						ret = true;
						dlgShell.dispose();
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData4);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	private void setinfo() {
		if (PROPPAGE_CLASS_PAGE4Dialog.isadd) {
			EDIT_ADD_QUERY_SPEC.setText("");
			EDIT_ADD_QUERY_DESC.setText(Messages
					.getString("LABEL.ADDQUERYSPEC"));
		} else {
			EDIT_ADD_QUERY_SPEC
					.setText(PROPPAGE_CLASS_PAGE4Dialog.CurrentLIST_CLASS_QUERYSPECS
							.getText(0));
			EDIT_ADD_QUERY_DESC.setText(Messages
					.getString("LABEL.EDITQUERYSPEC"));
		}
	}
}
