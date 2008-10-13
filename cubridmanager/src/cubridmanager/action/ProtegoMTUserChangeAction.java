package cubridmanager.action;

import org.eclipse.jface.action.Action;

import cubridmanager.dialog.PROPAGE_ProtegoMTUserManagementDialog;

public class ProtegoMTUserChangeAction extends Action {
	public ProtegoMTUserChangeAction(String text) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoMTUserChangeAction");
	}

	public void run() {
		// TODO : change MT info
		PROPAGE_ProtegoMTUserManagementDialog.refresh();
	}
}
