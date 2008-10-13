package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;

import cubridmanager.cubrid.*;
import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.PROPPAGE_LOCKINFO1Dialog;
import cubridmanager.cubrid.dialog.PROPPAGE_LOCKINFO2Dialog;
import cubridmanager.cubrid.view.CubridView;

public class LockinfoAction extends Action {
	public static AuthItem ai = null;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="5,8"
	private TabFolder tabFolder = null;
	private Composite composite = null;
	private Composite composite2 = null;
	private Button IDCANCEL = null;
	private Button IDREFRESH = null;
	private Label label1 = null;
	public static LockInfo linfo = null;
	public static LockObject lockobj = null;

	public LockinfoAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("LockinfoAction");
		if (img != null) {
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
			setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img.replaceFirst("icons",
							"disable_icons")));
		}
		setToolTipText(text);
	}

	public void run() {
		sShell = new Shell(Application.mainwindow.getShell(),
				SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell.setLayout(gridLayout);
		ai = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (ai == null) {
			CommonTool.ErrorBox(sShell, Messages.getString("MSG.SELECTDB"));
			return;
		}
		if (ai.status != MainConstants.STATUS_START) {
			CommonTool.ErrorBox(sShell, Messages
					.getString("ERROR.DATABASEISNOTRUNNING"));
			return;
		}
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(sShell, "dbname:" + ai.dbname, "lockdb",
				Messages.getString("WAIT.GETTINGLOCKINFO"))) {
			CommonTool.ErrorBox(sShell, cs.ErrorMsg);
			return;
		}

		tabFolder = new TabFolder(sShell, SWT.NONE);
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		tabFolder.setLayoutData(gridData1);

		final PROPPAGE_LOCKINFO1Dialog part1 = new PROPPAGE_LOCKINFO1Dialog(
				sShell);
		final PROPPAGE_LOCKINFO2Dialog part2 = new PROPPAGE_LOCKINFO2Dialog(
				sShell);

		composite = part1.SetTabPart(tabFolder);
		composite2 = part2.SetTabPart(tabFolder);

		sShell.setText(Messages.getString("TITLE.LOCKINFO"));

		TabItem tabItem = new TabItem(tabFolder, SWT.NONE);
		tabItem.setControl(composite);
		tabItem.setText(Messages.getString("TITLE.PROPPAGE_LOCKINFO1DIALOG"));
		TabItem tabItem2 = new TabItem(tabFolder, SWT.NONE);
		tabItem2.setControl(composite2);
		tabItem2.setText(Messages.getString("TITLE.PROPPAGE_LOCKINFO2DIALOG"));

		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.grabExcessHorizontalSpace = true;
		label1.setLayoutData(gridData2);
		IDREFRESH = new Button(sShell, SWT.NONE);
		IDREFRESH.setText(Messages.getString("BUTTON.REFRESH"));
		IDREFRESH
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ClientSocket cs = new ClientSocket();
						if (!cs.SendBackGround(sShell, "dbname:" + ai.dbname,
								"lockdb", Messages
										.getString("WAIT.GETTINGLOCKINFO"))) {
							CommonTool.ErrorBox(sShell, cs.ErrorMsg);
						} else {
							part1.setinfo();
							part2.setinfo();
						}
					}
				});
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData3.widthHint = 100;
		IDREFRESH.setLayoutData(gridData3);
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CLOSE"));
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 100;
		IDCANCEL.setLayoutData(gridData4);

		sShell.pack();
		CommonTool.centerShell(sShell);
		sShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
	}

}
