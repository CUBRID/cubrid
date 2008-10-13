package cubridmanager.action;

import org.eclipse.jface.action.Action;

import cubridmanager.dialog.PROPAGE_ProtegoUserManagementDialog;

public class ProtegoUserSaveAsFileAction extends Action {
	public ProtegoUserSaveAsFileAction(String text) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoUserSaveAsFileAction");
	}

	public void run() {
		PROPAGE_ProtegoUserManagementDialog.saveAsFile();
	}
}
