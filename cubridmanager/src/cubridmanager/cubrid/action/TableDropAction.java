package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cas.view.CASView;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBSchema;

public class TableDropAction extends Action {
	public TableDropAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("TableDropAction");
		if (img != null) {
			setImageDescriptor(cubridmanager.CubridmanagerPlugin.getImageDescriptor(img));
		}
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		if (DBSchema.CurrentObj == null) {
			return;
		}
		if (CommonTool.WarnYesNo(DBSchema.CurrentObj.concat(" - ")
				.concat(Messages.getString("WARNYESNO.DELETE"))) != SWT.YES) {
			return;
		}
		ClientSocket cs = new ClientSocket();

		if (!cs.SendBackGround(shell, "dbname:" + CubridView.Current_db
				+ "\nclassname:" + DBSchema.CurrentObj + "\n", "dropclass",
				Messages.getString("WAIT.DELETE"))) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}

		ClientSocket csClassInfo = new ClientSocket();
		if (!csClassInfo.SendClientMessage(shell, "dbname:"
				+ CubridView.Current_db + "\ndbstatus:on", "classinfo")) {
			CommonTool.ErrorBox(shell, csClassInfo.ErrorMsg);
			return;
		}

		CubridView.myNavi.createModel();
		CubridView.viewer.refresh();

		ApplicationActionBarAdvisor.refreshAction.run();
		if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID) {
			CubridView.myNavi.SelectDB_UpdateView(CubridView.Current_db);
		} else if (MainRegistry.Current_Navigator == MainConstants.NAVI_CAS) {
			CASView.myNavi.SelectBroker_UpdateView(CASView.Current_broker);
		}
	}
}
