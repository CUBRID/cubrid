package cubridmanager.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.dialog.PROPAGE_ProtegoMTUserManagementDialog;
import cubridmanager.dialog.ProtegoMTUserAddDialog;

public class ProtegoMTUserAddAction extends Action {
	public ProtegoMTUserAddAction(String text) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoMTUserAddAction");
	}

	public void run() {
		Shell shell = new Shell();
		ProtegoMTUserAddDialog dlg = new ProtegoMTUserAddDialog(shell);
		dlg.doModal();
		PROPAGE_ProtegoMTUserManagementDialog.refresh();
	}
}
