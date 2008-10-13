package cubridmanager.cas.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
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

public class ResumeBrokerAction extends Action {
	public ResumeBrokerAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ResumeBrokerAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (CASView.Current_broker.length() <= 0)
			return;
		CASItem casrec = MainRegistry.CASinfo_find(CASView.Current_broker);
		if (casrec == null || casrec.status != MainConstants.STATUS_WAIT)
			return;
		if (CommonTool.WarnYesNo(Messages.getString("WARNYESNO.BROKERRESUME")) != SWT.YES)
			return;

		Shell shell = new Shell();

		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(shell, "bname:" + CASView.Current_broker,
				"broker_resume", Messages.getString("WAIT.BROKERRESUME"))) {
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
