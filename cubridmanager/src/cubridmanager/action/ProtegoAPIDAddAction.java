package cubridmanager.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.dialog.PROPAGE_ProtegoAPIDManagementDialog;
import cubridmanager.dialog.ProtegoAPIDAddDialog;

public class ProtegoAPIDAddAction extends Action {
	public ProtegoAPIDAddAction(String text) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoMTUserAddAction");
	}

	public void run() {
		Shell shell = new Shell();
		ProtegoAPIDAddDialog dlg = new ProtegoAPIDAddDialog(shell);
		dlg.doModal();
		PROPAGE_ProtegoAPIDManagementDialog.refresh();
	}
}
