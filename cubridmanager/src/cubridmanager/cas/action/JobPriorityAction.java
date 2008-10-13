package cubridmanager.cas.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.WorkView;
import cubridmanager.cas.dialog.BROKER_JOB_PRIODialog;
import cubridmanager.cas.view.BrokerJob;
import cubridmanager.cas.view.CASView;

public class JobPriorityAction extends Action {
	public JobPriorityAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("JobPriorityAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		BROKER_JOB_PRIODialog dlg = new BROKER_JOB_PRIODialog(shell);
		if (dlg.doModal()) {
			ClientSocket cs = new ClientSocket();
			if (!cs.SendClientMessage(shell, "bname:" + CASView.Current_broker,
					"getbrokerstatus")) {
				CommonTool.ErrorBox(shell, cs.ErrorMsg);
				return;
			}
			WorkView
					.SetView(BrokerJob.ID, CASView.Current_broker, BrokerJob.ID);
		}
	}
}
