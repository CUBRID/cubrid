package cubridmanager.action;

import org.eclipse.jface.action.Action;

import cubridmanager.dialog.PROPAGE_ProtegoAPIDManagementDialog;

public class ProtegoAPIDChangeAction extends Action {
	public ProtegoAPIDChangeAction(String text) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoMTUserChangeAction");
	}

	public void run() {
		// TODO : change APID info
		PROPAGE_ProtegoAPIDManagementDialog.refresh();
	}
}
