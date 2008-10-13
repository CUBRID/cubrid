package cubridmanager.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IWorkbenchWindow;

import cubridmanager.dialog.SECURITYDialog;

public class UserAdminAction extends Action {
	// private final IWorkbenchWindow window;

	public UserAdminAction(String text, IWorkbenchWindow window) {
		super(text);
		// this.window = window;
		// The id is used to refer to the action in a menu or toolbar
		setId("UserAdminAction");
		setActionDefinitionId("UserAdminAction");
		setImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/usermng.png"));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		SECURITYDialog dlg = new SECURITYDialog(shell);
		dlg.doModal();

	}

}
