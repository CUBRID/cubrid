package cubridmanager.cubrid.dialog;

import java.util.ArrayList;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.Messages;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.CommonTool;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.ProgressBar;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class FILEDOWN_PROGRESSDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Button IDCANCEL = null;
	public ProgressBar PROGRESS_BACKUPFILE_DOWN = null;
	private Label label1 = null;
	private Label label2 = null;
	private Text EDIT_FILEDOWN_SOURCENAME = null;
	public Text EDIT_FILEDOWN_DESTNAME = null;
	public Label label3 = null;
	private Group group1 = null;
	private Button CHECK_DOWNLOAD_MESSAGE = null;
	public ArrayList sfiles = null;
	public ArrayList dfiles = null;
	public boolean compress = false;
	public static boolean isdownloading = false;
	public static boolean isfiledowncontinue = false;
	public static FILEDOWN_PROGRESSDialog actdlg = null;
	public Label label = null;

	public FILEDOWN_PROGRESSDialog(Shell parent) {
		super(parent);
		actdlg = this;
	}

	public FILEDOWN_PROGRESSDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		setinfo();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		isdownloading = false;
		return 0;
	}

	private void createSShell() {
		// dlgShell = new Shell(SWT.MODELESS | SWT.DIALOG_TRIM | SWT.ON_TOP);
		dlgShell = new Shell(getParent(), SWT.MODELESS | SWT.DIALOG_TRIM
				| SWT.ON_TOP);
		dlgShell.setText(Messages.getString("TITLE.FILEDOWN_PROGRESSDIALOG"));
		dlgShell.setLayout(new FillLayout());
		dlgShell.addShellListener(new org.eclipse.swt.events.ShellAdapter() {
			public void shellClosed(org.eclipse.swt.events.ShellEvent e) {
				e.doit = false;
				if (CommonTool.WarnYesNo(dlgShell, Messages
						.getString("WARNYESNO.STOPRECEIVINGBACKUP")) == SWT.YES) {
					canceldown();
				}
			}
		});
		dlgShell
				.addDisposeListener(new org.eclipse.swt.events.DisposeListener() {
					public void widgetDisposed(
							org.eclipse.swt.events.DisposeEvent e) {
						isdownloading = false;
					}
				});

		createComposite();
	}

	private void createComposite() {
		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.verticalSpan = 1;
		gridData9.widthHint = 390;
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.widthHint = 390;
		GridLayout gridLayout7 = new GridLayout();
		gridLayout7.numColumns = 2;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalSpan = 2;
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData5.widthHint = 100;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.grabExcessHorizontalSpace = true;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData3.grabExcessHorizontalSpace = true;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 2;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData2.widthHint = 400;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.FILEINFORMATION"));
		group1.setLayout(gridLayout7);
		group1.setLayoutData(gridData);
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.SOURCEFILE"));
		EDIT_FILEDOWN_SOURCENAME = new Text(group1, SWT.NONE);
		EDIT_FILEDOWN_SOURCENAME.setEditable(false);
		EDIT_FILEDOWN_SOURCENAME.setLayoutData(gridData8);
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.DESTFILE"));
		CHECK_DOWNLOAD_MESSAGE = new Button(sShell, SWT.CHECK);
		CHECK_DOWNLOAD_MESSAGE.setText(Messages
				.getString("CHECK.DONTDISPLAYEACH"));
		CHECK_DOWNLOAD_MESSAGE.setLayoutData(gridData1);
		PROGRESS_BACKUPFILE_DOWN = new ProgressBar(sShell, SWT.BORDER);
		PROGRESS_BACKUPFILE_DOWN.setLayoutData(gridData2);
		EDIT_FILEDOWN_DESTNAME = new Text(group1, SWT.NONE);
		EDIT_FILEDOWN_DESTNAME.setEditable(false);
		EDIT_FILEDOWN_DESTNAME.setLayoutData(gridData9);
		label3 = new Label(sShell, SWT.RIGHT | SWT.WRAP);
		label3.setLayoutData(gridData3);
		label = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label.setLayoutData(gridData4);
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL1"));
		IDCANCEL.setLayoutData(gridData5);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CommonTool.WarnYesNo(dlgShell, Messages
								.getString("WARNYESNO.STOPRECEIVINGBACKUP")) == SWT.YES) {
							canceldown();
						}
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	private void setinfo() {
		CHECK_DOWNLOAD_MESSAGE.setSelection(false);
		isdownloading = true;
		for (int i = 0, n = sfiles.size(); i < n; i++) {
			EDIT_FILEDOWN_SOURCENAME.setText((String) sfiles.get(i));
			EDIT_FILEDOWN_DESTNAME.setText((String) dfiles.get(i));
			dlgShell.update();
			ClientSocket cs = new ClientSocket();
			String msg = "dbname:" + CubridView.Current_db + "\n";
			msg += "file_num:1\n";
			msg += "file_name:" + (String) sfiles.get(i) + "\n";
			msg += "compress:" + ((compress) ? "y" : "n") + "\n";
			isfiledowncontinue = true;
			if (!cs.SendClientMessage(dlgShell, msg, "getfile", false)) {
				CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
				isdownloading = false;
				dlgShell.dispose();
				return;
			}
			if (!CHECK_DOWNLOAD_MESSAGE.getSelection())
				CommonTool.MsgBox(dlgShell, Messages.getString("MSG.SUCCESS"),
						Messages.getString("MSG.DOWNLOADBACKUPSUCCESS"));
		}
		isdownloading = false;
		dlgShell.dispose();
	}

	private void canceldown() {
		isfiledowncontinue = false;
	}
}
