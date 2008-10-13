package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.OPTIMIZEDBDialog;
import cubridmanager.cubrid.view.CubridView;

public class OptimizeAction extends Action {
	public static AuthItem ai = null;

	public OptimizeAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("OptimizeAction");
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
		Shell shell = new Shell(Application.mainwindow.getShell());
		ai = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (ai == null) {
			CommonTool.ErrorBox(shell, Messages.getString("MSG.SELECTDB"));
			return;
		}
		ClientSocket cs = new ClientSocket();
		if (cs.SendBackGround(shell, "dbname:" + ai.dbname + "\ndbstatus:off",
				"classinfo", Messages.getString("WAITING.CLASSINFO"))) {
			ai = MainRegistry.Authinfo_find(CubridView.Current_db); // info refresh
			OPTIMIZEDBDialog dlg = new OPTIMIZEDBDialog(shell);
			dlg.doModal();
		} else
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
	}
}
