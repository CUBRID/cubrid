package cubridmanager.cubrid.dialog;

import java.text.NumberFormat;
import java.util.*;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.CommonTool;
import cubridmanager.cubrid.VolumeInfo;
import cubridmanager.cubrid.action.RenameAction;
import cubridmanager.cubrid.view.CubridView;

import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class RENAMEDBDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Text EDIT_RENAMEDB_NEWNAME = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private Label label1 = null;
	private boolean ret = false;
	public String newname = null;
	private Text EDIT_RENAMEDB_EXVOLDIR = null;
	private Button CHECK_RENAMEDB_ADVANCED = null;
	private Table LIST_RENAMEDB_VOLLIST = null;
	public static boolean isChanged = false;
	public static String prop_path = Messages
			.getString("TABLE.NEWDIRECTORYPATH");

	public static final String[] PROPS = { "a", "b", prop_path };
	private EditVolumeList listener = null;
	private Button CHECK_RENAMEDB_EXVOLDIR;

	private Button CHECK_FORCE_DELETE_BACKUP;

	public RENAMEDBDialog(Shell parent) {
		super(parent);
	}

	public RENAMEDBDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		MainRegistry.Tmpchkrst.clear();

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
		dlgShell.setText(Messages.getString("TITLE.RENAMEDBDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 100;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData1.widthHint = 100;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.widthHint = 150;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);

		GridData gdLabel1 = new org.eclipse.swt.layout.GridData();
		gdLabel1.widthHint = 150;
		gdLabel1.grabExcessHorizontalSpace = true;
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.NEWDBNAME"));
		label1.setLayoutData(gdLabel1);
		EDIT_RENAMEDB_NEWNAME = new Text(sShell, SWT.BORDER);
		EDIT_RENAMEDB_NEWNAME.setLayoutData(gridData);
		EDIT_RENAMEDB_NEWNAME
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						String tmp = EDIT_RENAMEDB_NEWNAME.getText();
						if (tmp == null || tmp.length() <= 0)
							IDOK.setEnabled(false);
						else
							IDOK.setEnabled(true);
						if (!isChanged) {
							String newpath = MainRegistry.envCUBRID_DATABASES
									+ "/" + tmp;
							EDIT_RENAMEDB_EXVOLDIR.setText(newpath);
							LIST_RENAMEDB_VOLLIST.getItem(0).setText(1, tmp);
							LIST_RENAMEDB_VOLLIST.getItem(0)
									.setText(2, newpath);
							NumberFormat nf = NumberFormat.getInstance();
							nf.setMinimumIntegerDigits(3);
							for (int i = 1, n = LIST_RENAMEDB_VOLLIST
									.getItemCount(); i < n; i++) {
								TableItem ti = LIST_RENAMEDB_VOLLIST.getItem(i);
								ti.setText(1, tmp + "_x" + nf.format(i));
								ti.setText(2, newpath);
							}
							isChanged = false;
						}
					}
				});

		GridData gdCheck = new org.eclipse.swt.layout.GridData();
		gdCheck.horizontalSpan = 2;
		gdCheck.horizontalAlignment = GridData.FILL;
		CHECK_FORCE_DELETE_BACKUP = new Button(sShell, SWT.CHECK);
		CHECK_FORCE_DELETE_BACKUP.setText(Messages
				.getString("CHECK.FORCEDELETE"));
		CHECK_FORCE_DELETE_BACKUP.setLayoutData(gdCheck);

		CHECK_RENAMEDB_EXVOLDIR = new Button(sShell, SWT.CHECK);
		CHECK_RENAMEDB_EXVOLDIR.setText(Messages
				.getString("LABEL.EXTENDEDVOLUME"));
		CHECK_RENAMEDB_EXVOLDIR.setLayoutData(gdLabel1);
		CHECK_RENAMEDB_EXVOLDIR
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_RENAMEDB_EXVOLDIR.getSelection()) {
							EDIT_RENAMEDB_EXVOLDIR.setEnabled(true);
							// if (!isChanged) {
							// EDIT_RENAMEDB_EXVOLDIR.setText(MainRegistry.envCUBRID_DATABASES
							// + "/" + EDIT_RENAMEDB_NEWNAME.getText());
							// isChanged = false;
							// }
							CHECK_RENAMEDB_ADVANCED.setSelection(false);
							LIST_RENAMEDB_VOLLIST.setEnabled(false);
						} else {
							EDIT_RENAMEDB_EXVOLDIR.setEnabled(false);
						}
					}
				});
		EDIT_RENAMEDB_EXVOLDIR = new Text(sShell, SWT.BORDER);
		EDIT_RENAMEDB_EXVOLDIR.setLayoutData(gridData);
		EDIT_RENAMEDB_EXVOLDIR
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						isChanged = true;
					}
				});

		CHECK_RENAMEDB_ADVANCED = new Button(sShell, SWT.CHECK);
		CHECK_RENAMEDB_ADVANCED.setText(Messages
				.getString("CHECK.RENAMEINDIVIDUAL"));
		CHECK_RENAMEDB_ADVANCED.setLayoutData(gdCheck);
		CHECK_RENAMEDB_ADVANCED
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_RENAMEDB_ADVANCED.getSelection()) {
							LIST_RENAMEDB_VOLLIST.setEnabled(true);
							CHECK_RENAMEDB_EXVOLDIR.setSelection(false);
							EDIT_RENAMEDB_EXVOLDIR.setEnabled(false);
						} else
							LIST_RENAMEDB_VOLLIST.setEnabled(false);
					}
				});
		createTable1();

		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData1);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						newname = EDIT_RENAMEDB_NEWNAME.getText();
						if (newname == null || newname.length() <= 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INPUTNEWDBNAME"));
							return;
						}
						if (!CommonTool.isValidDBName(newname)) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INVALIDDBNAME"));
							return;
						}
						if (MainRegistry.Authinfo_find(newname) != null) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.DATABASEALREADYEXIST"));
							return;
						}
						if (CommonTool.WarnYesNo(dlgShell, Messages
								.getString("WARNYESNO.RENAMEDB")
								+ " " + newname) == SWT.YES) {
							ClientSocket cs = new ClientSocket();
							if (!CheckDirs(cs)) {
								CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
								return;
							}
							dlgShell.update();
							if (MainRegistry.Tmpchkrst.size() > 0) { // create directory confirm
								NEWDIRECTORYDialog newdlg = new NEWDIRECTORYDialog(
										dlgShell);
								if (newdlg.doModal() == 0)
									return;
							}

							cs = new ClientSocket();
							if (!SendRename(cs)) {
								CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
								return;
							}
							ret = true;
							dlgShell.dispose();
						}
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData2);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
		setinfo();
	}

	private void createTable1() {
		LIST_RENAMEDB_VOLLIST = new Table(sShell, SWT.FULL_SELECTION
				| SWT.BORDER);
		LIST_RENAMEDB_VOLLIST.setLinesVisible(true);
		LIST_RENAMEDB_VOLLIST.setHeaderVisible(true);
		GridData gridDatazz = new org.eclipse.swt.layout.GridData();
		gridDatazz.horizontalAlignment = GridData.FILL;
		gridDatazz.heightHint = 168;
		gridDatazz.horizontalSpan = 2;
		LIST_RENAMEDB_VOLLIST.setLayoutData(gridDatazz);

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 100, true));
		tlayout.addColumnData(new ColumnWeightData(50, 100, true));
		tlayout.addColumnData(new ColumnWeightData(50, 200, true));
		LIST_RENAMEDB_VOLLIST.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_RENAMEDB_VOLLIST, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.CURRENTVOLNAME"));
		tblcol = new TableColumn(LIST_RENAMEDB_VOLLIST, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NEWVOLNAME"));
		tblcol = new TableColumn(LIST_RENAMEDB_VOLLIST, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NEWDIRECTORYPATH"));

		listener = new EditVolumeList(LIST_RENAMEDB_VOLLIST);
		LIST_RENAMEDB_VOLLIST.addListener(SWT.MouseUp, listener);
	}

	class VolOrderComparator implements Comparator {
		public int compare(Object obj1, Object obj2) {
			int order1 = ((VolumeInfo) obj1).order;
			int order2 = ((VolumeInfo) obj2).order;
			return order1 < order2 ? -1 : (order1 == order2 ? 0 : 1);
		}
	}

	private void setinfo() {
		VolumeInfo vi;
		TableItem item;
		Collections.sort(RenameAction.ai.Volinfo, new VolOrderComparator());
		for (int i = 0, n = RenameAction.ai.Volinfo.size(); i < n; i++) {
			vi = (VolumeInfo) RenameAction.ai.Volinfo.get(i);
			if (!vi.type.equals("GENERIC") && !vi.type.equals("DATA")
					&& !vi.type.equals("INDEX") && !vi.type.equals("TEMP"))
				continue;
			item = new TableItem(LIST_RENAMEDB_VOLLIST, SWT.NONE);
			item.setText(0, vi.spacename);
			item.setText(1, vi.spacename);
			item.setText(2, MainRegistry.envCUBRID_DATABASES);
		}
		for (int i = 0, n = LIST_RENAMEDB_VOLLIST.getColumnCount(); i < n; i++) {
			LIST_RENAMEDB_VOLLIST.getColumn(i).pack();
		}
		Collections.sort(RenameAction.ai.Volinfo);
		CHECK_RENAMEDB_EXVOLDIR.setSelection(false);
		EDIT_RENAMEDB_EXVOLDIR.setText(MainRegistry.envCUBRID_DATABASES);
		EDIT_RENAMEDB_EXVOLDIR.setEnabled(false);
		CHECK_RENAMEDB_ADVANCED.setSelection(false);
		LIST_RENAMEDB_VOLLIST.setEnabled(false);
		isChanged = false;
	}

	private boolean CheckDirs(ClientSocket cs) {
		String requestMsg = "";
		String tmp = "";
		if (CHECK_RENAMEDB_ADVANCED.getSelection()) {
			for (int i = 0, n = LIST_RENAMEDB_VOLLIST.getItemCount(); i < n; i++) {
				TableItem ti = LIST_RENAMEDB_VOLLIST.getItem(i);
				tmp = "dir:" + ti.getText(2) + "\n";
				if (requestMsg.indexOf(tmp) < 0)
					requestMsg += tmp;
			}
		} else if (CHECK_RENAMEDB_EXVOLDIR.getSelection()) {
			requestMsg += "dir:" + EDIT_RENAMEDB_EXVOLDIR.getText() + "\n";
		} else
			return true; // if selected item in checkbox is null (rename with default option)

		if (cs.SendBackGround(dlgShell, requestMsg, "checkdir", Messages
				.getString("WAITING.CHECKINGDIRECTORY"))) {
			return true;
		}
		return false;
	}

	private boolean SendRename(ClientSocket cs) {
		String requestMsg = "";
		requestMsg += "dbname:" + CubridView.Current_db + "\n";
		requestMsg += "rename:" + newname + "\n";

		if (CHECK_RENAMEDB_EXVOLDIR.getSelection()) {
			requestMsg += "exvolpath:" + EDIT_RENAMEDB_EXVOLDIR.getText()
					+ "\n";
			requestMsg += "advanced:off\n";
		} else if (CHECK_RENAMEDB_ADVANCED.getSelection()) {
			requestMsg += "exvolpath:none\n";
			requestMsg += "advanced:on\n";
			requestMsg += "open:volume\n";

			String oldVolName, newVolName, oldVolDir, newVolDir;
			for (int i = 0, n = LIST_RENAMEDB_VOLLIST.getItemCount(); i < n; i++) {
				TableItem ti = LIST_RENAMEDB_VOLLIST.getItem(i);
				oldVolDir = ((VolumeInfo) RenameAction.ai.Volinfo.get(i)).location;
				oldVolName = ti.getText(0);
				newVolName = ti.getText(1);
				newVolDir = ti.getText(2);
				oldVolDir = oldVolDir.replaceAll(":", "|");
				newVolDir = newVolDir.replaceAll(":", "|");
				requestMsg += oldVolDir + "/" + oldVolName + ":" + newVolDir
						+ "/" + newVolName + "\n";
			}
			requestMsg += "close:volume\n";
		}

		if (CHECK_FORCE_DELETE_BACKUP.getSelection())
			requestMsg += "forcedel:y\n";
		else
			requestMsg += "forcedel:n\n";

		if (cs.SendBackGround(dlgShell, requestMsg, "renamedb", Messages
				.getString("WAITING.RENAMEDB"))) {
			return true;
		}
		return false;
	}

}
