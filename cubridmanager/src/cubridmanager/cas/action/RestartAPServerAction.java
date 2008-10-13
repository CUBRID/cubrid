package cubridmanager.cas.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cas.CASItem;
import cubridmanager.cas.dialog.BROKER_APPRESTARTDialog;
import cubridmanager.cas.view.BrokerJob;
import cubridmanager.cas.view.CASView;

public class RestartAPServerAction extends Action {
	public RestartAPServerAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("RestartAPServerAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (CASView.Current_broker.length() <= 0)
			return;
		CASItem casrec = MainRegistry.CASinfo_find(CASView.Current_broker);
		if (casrec == null
				|| (casrec.status != MainConstants.STATUS_WAIT && casrec.status != MainConstants.STATUS_START))
			return;
		Shell shell = new Shell(Application.mainwindow.getShell());
		BROKER_APPRESTARTDialog dlg = new BROKER_APPRESTARTDialog(shell);
		int asnum = dlg.doModal();
		if (asnum < 0)
			return;

		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(shell, "bname:" + CASView.Current_broker
				+ "\nasnum:" + asnum, "broker_restart", Messages
				.getString("WAITING.RESTARTBROKER"))) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "", "getbrokersinfo")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		CASView.myNavi.createModel();
		CASView.viewer.refresh();
		WorkView.SetView(BrokerJob.ID, CASView.Current_broker, BrokerJob.ID);
		ApplicationActionBarAdvisor.AdjustToolbar(MainConstants.NAVI_CAS);
	}
}
