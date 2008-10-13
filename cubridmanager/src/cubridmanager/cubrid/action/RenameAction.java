package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.DBUserInfo;
import cubridmanager.cubrid.dialog.RENAMEDBDialog;
import cubridmanager.cubrid.view.CubridView;

public class RenameAction extends Action {
	public static AuthItem ai = null;

	public RenameAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("RenameAction");
		if (img != null) {
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
			setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img.replaceFirst("icons",
							"disable_icons")));
		}
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		ai = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (ai == null) {
			CommonTool.ErrorBox(shell, Messages.getString("MSG.SELECTDB"));
			return;
		}
		if (ai.status != MainConstants.STATUS_STOP) {
			CommonTool.ErrorBox(shell, Messages
					.getString("ERROR.RUNNINGDATABASE"));
			return;
		}

		ClientSocket cs = new ClientSocket();
		if (cs.SendBackGround(shell, "dbname:" + ai.dbname, "dbspaceinfo",
				Messages.getString("WAITING.GETTINGVOLUMEINFO"))) {
			RENAMEDBDialog dlg = new RENAMEDBDialog(shell);
			if (dlg.doModal()) {
				// CommonTool.MsgBox(shell, Messages.getString("MSG.SUCCESS"),
				// Messages.getString("MSG.RENAMEDBSUCCESS"));
				AuthItem authrec = MainRegistry
						.Authinfo_find(CubridView.Current_db);
				DBUserInfo dbuser = MainRegistry
						.getDBUserInfo(CubridView.Current_db);
				CubridView.Current_db = "";
				ai.dbname = dlg.newname;
				ApplicationActionBarAdvisor.refreshAction.run();
				authrec.dbname = ai.dbname;
				dbuser.dbname = ai.dbname;
				MainRegistry.Authinfo_update(authrec);
				MainRegistry.DBUserInfo_update(dbuser);
				CubridView.refresh();
				CubridView.setViewDefault();
			}
		} else {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
	}
}
