package cubridmanager.cas.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.cas.dialog.BROKER_SETDialog;
import cubridmanager.cas.view.CASView;

public class SetParameterUsingChangerAction extends Action {
	public SetParameterUsingChangerAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("SetParameterUsingChangerAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "bname:" + CASView.Current_broker,
				"getbrokeronconf")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		BROKER_SETDialog dlg = new BROKER_SETDialog(shell);
		if (dlg.doModal() == SWT.OK)
			CASView.myNavi.GetBrokerInfo(CASView.Current_broker);
	}
}
