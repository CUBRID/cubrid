package cubridmanager.action;

import org.eclipse.jface.action.Action;

import cubridmanager.dialog.PROPAGE_ProtegoUserManagementDialog;

public class ProtegoUserChangeAction extends Action {
	public ProtegoUserChangeAction(String text) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoUserChangeAction");
	}

	public void run() {
		// Not supported.
		PROPAGE_ProtegoUserManagementDialog.refresh();
	}
}
