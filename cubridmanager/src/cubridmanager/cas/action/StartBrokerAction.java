package cubridmanager.cas.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cas.CASItem;
import cubridmanager.cas.view.BrokerJob;
import cubridmanager.cas.view.CASView;

public class StartBrokerAction extends Action {
	public StartBrokerAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("StartBrokerAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (CASView.Current_broker.length() <= 0)
			return;
		CASItem casrec = MainRegistry.CASinfo_find(CASView.Current_broker);
		if (casrec == null || casrec.status != MainConstants.STATUS_STOP)
			return;

		Shell shell = new Shell();

		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(shell, "bname:" + CASView.Current_broker,
				"broker_start", Messages.getString("WAIT.BROKERSTART"))) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}

		cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "", "getbrokersinfo")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}

		cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "bname:" + CASView.Current_broker
				+ "\n", "getbrokerstatus")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}

		cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "broker:" + CASView.Current_broker
				+ "\n", "getlogfileinfo")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}

		cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "bname:" + CASView.Current_broker
				+ "\n", "getaslimit")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}

		CASView.myNavi.createModel();
		CASView.viewer.refresh();
		CASView.myNavi.updateWorkView();
		WorkView.SetView(BrokerJob.ID, CASView.Current_broker, BrokerJob.ID);
		ApplicationActionBarAdvisor.AdjustToolbar(MainConstants.NAVI_CAS);
	}
}
