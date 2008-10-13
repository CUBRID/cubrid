package cubridmanager.action;

import java.io.File;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Shell;

import cubrid.upa.UpaClient;
import cubrid.upa.UpaException;
import cubrid.upa.UpaUserInfo;
import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.dialog.PROPAGE_ProtegoUserManagementDialog;
import cubridmanager.dialog.ProtegoUserAddDialog;
import cubridmanager.dialog.ProtegoUserAddResultDialog;

public class ProtegoUserAddAction extends Action {
	public boolean bulkAdd = false;

	Shell shell = new Shell();

	public ProtegoUserAddAction(String text) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoUserAddAction");
	}

	public ProtegoUserAddAction(String text, boolean addFromFile) {
		super(text);
		bulkAdd = addFromFile;
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoUserAddAction");
	}

	public void run() {
		if (bulkAdd) {
			FileDialog dlg = new FileDialog(Application.mainwindow.getShell(),
					SWT.OPEN | SWT.APPLICATION_MODAL);
			dlg.setFilterExtensions(new String[] { "*.txt", "*.*" });
			dlg.setFilterNames(new String[] { "Txt file", "All file" });
			File curdir = new File(".");
			try {
				dlg.setFilterPath(curdir.getCanonicalPath());
			} catch (Exception e) {
				dlg.setFilterPath(".");
			}

			String fileName = dlg.open();
			if (fileName == null)
				return;

			UpaUserInfo[] failedList = null;
			try {
				failedList = UpaClient.admUserAddFile(
						ProtegoUserManagementAction.dlg.upaKey, fileName);
			} catch (UpaException ee) {
				CommonTool.ErrorBox(Application.mainwindow.getShell(), Messages
						.getString("TEXT.FAILEDADDUSERAUTH"));
				return;
			}

			if (failedList != null && failedList.length != 0) {
				ProtegoUserAddResultDialog addResultDialog = new ProtegoUserAddResultDialog(
						shell);
				addResultDialog.failedList = failedList;
				addResultDialog.doModal();
			}
		} else {
			ProtegoUserAddDialog dlg = new ProtegoUserAddDialog(shell);
			dlg.doModal();
		}

		PROPAGE_ProtegoUserManagementDialog.refresh();
	}
}
