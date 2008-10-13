package cubridmanager.action;

import org.eclipse.jface.action.Action;
import org.eclipse.ui.IWorkbenchWindow;

import cubridmanager.Application;
import cubridmanager.dialog.UserManagementDialog;

public class UserManagementAction extends Action {
	// private final IWorkbenchWindow window;

	public UserManagementAction(String text, IWorkbenchWindow window) {
		super(text);
		// this.window = window;
		// The id is used to refer to the action in a menu or toolbar
		setId("UserManagement");
		setActionDefinitionId("UserManagement");
		setImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/usermng.png"));
		setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/disable_icons/usermng.png"));
		setToolTipText(text);
	}

	public void run() {
		UserManagementDialog dlg = new UserManagementDialog(
				Application.mainwindow.getShell());
		dlg.doModal();
	}

}
