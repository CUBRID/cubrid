package cubridmanager.cas.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cas.CASItem;
import cubridmanager.cas.view.CASView;

public class DeleteBrokerAction extends Action {
	public DeleteBrokerAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("DeleteBrokerAction");
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
		if (CommonTool.WarnYesNo(Messages.getString("WARNYESNO.DELETE")) != SWT.YES)
			return;

		Shell shell = new Shell();

		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(shell, "bname:" + CASView.Current_broker,
				"deletebroker", Messages.getString("WAITING.DELBROKER"))) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		MainRegistry.DeletedBrokers.add(CASView.Current_broker);
		cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "", "getbrokersinfo")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		if (MainRegistry.IsCASStart) {
			CommonTool
					.WarnBox(shell, Messages.getString("MSG.AFTERRESTARTCAS"));
		}
		CASView.Current_broker = "";
		CASView.myNavi.createModel();
		CASView.viewer.refresh();
	}
}
