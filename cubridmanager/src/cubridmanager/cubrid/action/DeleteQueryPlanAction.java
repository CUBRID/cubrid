package cubridmanager.cubrid.action;

import java.util.ArrayList;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.jface.action.Action;
import cubridmanager.Application;
import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cas.view.CASView;
import cubridmanager.cubrid.AutoQuery;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.JobAutomation;

import org.eclipse.swt.SWT;

public class DeleteQueryPlanAction extends Action {
	public DeleteQueryPlanAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("DeleteQueryPlanAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell dlgShell = new Shell(Application.mainwindow.getShell());
		String msg = "";

		if (JobAutomation.objaq == null)
			return;
		if (CommonTool.WarnYesNo(Application.mainwindow.getShell(), Messages
				.getString("WARNYESNO.DELETEQUERYPLAN")) != SWT.YES)
			return;
		msg = "dbname:" + CubridView.Current_db + "\n";
		msg = msg + "open:planlist\n";
		ArrayList jobinfo = AutoQuery.AutoQueryInfo_get(CubridView.Current_db);
		for (int i = 0, n = jobinfo.size(); i < n; i++) {
			AutoQuery tmp = (AutoQuery) jobinfo.get(i);
			if (JobAutomation.objaq.QueryID.equals(tmp.QueryID)) {
				continue; // skip selected
			}
			msg += "open:queryplan\n";
			msg += "query_id:" + tmp.QueryID + "\n";
			msg += "period:" + tmp.Period + "\n";
			msg += "detail:" + tmp.TimeDetail + "\n";
			msg += "query_string:" + tmp.QueryString + "\n";
			msg += "close:queryplan\n";
		}
		msg += "close:planlist\n";

		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(dlgShell, msg, "setautoexecquery", Messages
				.getString("WAITING.UPDATEQUERYPLAN"))) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			return;
		}
		cs = new ClientSocket();
		if (!cs.SendClientMessage(dlgShell, "dbname:" + CubridView.Current_db,
				"getautoexecquery")) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
		}
		WorkView.DeleteView(JobAutomation.ID);
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
