package cubridmanager.cas.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.cas.dialog.BROKER_ENV_SETDialog;
import cubridmanager.cas.view.CASView;

public class SetSourceEnvAction extends Action {
	public SetSourceEnvAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("SetSourceEnvAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "bname:" + CASView.Current_broker,
				"getbrokerenvinfo")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		BROKER_ENV_SETDialog dlg = new BROKER_ENV_SETDialog(shell);
		dlg.doModal();
	}
}
