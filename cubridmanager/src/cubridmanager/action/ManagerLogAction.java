package cubridmanager.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.ui.IWorkbenchWindow;
import java.util.ArrayList;

import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.dialog.DBMT_ACCESS_ERRORDialog;

public class ManagerLogAction extends Action {
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="5,8"
	private TabFolder tabFolder = null;
	private Composite composite = null;
	private Composite composite2 = null;
	private Button IDOK = null;
	private Button IDD_DELETE = null;
	public static ArrayList Accesslog = new ArrayList();
	public static ArrayList Errorlog = new ArrayList();
	DBMT_ACCESS_ERRORDialog part1 = null;
	DBMT_ACCESS_ERRORDialog part2 = null;

	public ManagerLogAction(String text, IWorkbenchWindow window) {
		super(text);
		// this.window = window;
		// The id is used to refer to the action in a menu or toolbar
		setId("ManagerLogAction");
		setActionDefinitionId("ManagerLogAction");
		setImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/logs.png"));
	}

	public void run() {
		// sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell = new Shell(Application.mainwindow.getShell(),
				SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setText(Messages.getString("TITLE.MANAGERLOG"));
		GridLayout gridLayout = new GridLayout(2, false);
		gridLayout.marginHeight = 10;
		sShell.setLayout(gridLayout);

		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(Application.mainwindow.getShell(), "",
				"loadaccesslog")) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), cs.ErrorMsg);
			return;
		}

		GridData gridTabFolder = new GridData();
		gridTabFolder.horizontalSpan = 2;
		gridTabFolder.widthHint = 550;
		gridTabFolder.heightHint = 600;
		tabFolder = new TabFolder(sShell, SWT.NONE);
		tabFolder.setLayoutData(gridTabFolder);

		part1 = new DBMT_ACCESS_ERRORDialog(sShell, "access");
		part2 = new DBMT_ACCESS_ERRORDialog(sShell, "error");

		composite = part1.SetTabPart(tabFolder);
		composite2 = part2.SetTabPart(tabFolder);

		TabItem tabItem = new TabItem(tabFolder, SWT.NONE);
		tabItem.setControl(composite);
		tabItem.setText(Messages.getString("TITLE.ACCESSLOG"));
		TabItem tabItem2 = new TabItem(tabFolder, SWT.NONE);
		tabItem2.setControl(composite2);
		tabItem2.setText(Messages.getString("TITLE.ERRORLOG"));

		GridData gridDeleteBtn = new GridData(GridData.GRAB_HORIZONTAL
				| GridData.HORIZONTAL_ALIGN_END);
		gridDeleteBtn.widthHint = 75;
		IDD_DELETE = new Button(sShell, SWT.NONE);
		IDD_DELETE.setText(Messages.getString("BUTTON.DELETEALL"));
		IDD_DELETE.setLayoutData(gridDeleteBtn);
		IDD_DELETE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CommonTool.WarnYesNo(sShell, Messages
								.getString("WARNYESNO.DELETE")) != SWT.YES)
							return;
						ClientSocket cs = new ClientSocket();
						if (tabFolder.getSelectionIndex() == 0) {
							if (!cs.SendClientMessage(Application.mainwindow
									.getShell(), "", "deleteaccesslog")) {
								CommonTool.ErrorBox(Application.mainwindow
										.getShell(), cs.ErrorMsg);
								return;
							}
						} else {
							if (!cs.SendClientMessage(Application.mainwindow
									.getShell(), "", "deleteerrorlog")) {
								CommonTool.ErrorBox(Application.mainwindow
										.getShell(), cs.ErrorMsg);
								return;
							}
						}
						ClientSocket cs2 = new ClientSocket();
						if (!cs2.SendClientMessage(Application.mainwindow
								.getShell(), "", "loadaccesslog")) {
							CommonTool.ErrorBox(Application.mainwindow
									.getShell(), cs2.ErrorMsg);
							return;
						}
						part1.updateTable();
						part2.updateTable();
					}
				});

		GridData gridOkBtn = new GridData();
		gridOkBtn.widthHint = 75;
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.CLOSE"));
		IDOK.setLayoutData(gridOkBtn);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});

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
