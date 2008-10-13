package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TreeItem;

import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.ProtegoReadCert;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.DBA_CONFIRMDialog;
import cubridmanager.cubrid.dialog.DELETEDB_CONFIRMDialog;
import cubridmanager.cubrid.view.CubridView;

public class DeleteAction extends Action {
	public static AuthItem ai = null;

	public static boolean deleteBackup = false;

	public DeleteAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("DeleteAction");
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
			CommonTool.ErrorBox(shell, Messages.getString("ERROR.RUNNINGDATABASE"));
			return;
		}
		ClientSocket cs = new ClientSocket();

		if (cs.SendBackGround(shell, "dbname:" + CubridView.Current_db,
				"dbspaceinfo", Messages.getString("WAITING.GETTINGVOLUMEINFO"))) {
			DELETEDB_CONFIRMDialog deldlg = new DELETEDB_CONFIRMDialog(shell);
			if (deldlg.doModal()) {
				if (MainRegistry.isCertLogin()) {
					String[] ret = null;
					ProtegoReadCert reader = new ProtegoReadCert();
					ret = reader.protegoSelectCert();
					if (ret == null) {
						return;
					}
					if (!(ret[0].equals(MainRegistry.UserID))) {
						CommonTool.ErrorBox(Messages.getString("ERROR.USERDNERROR"));
						return;
					}
				} else {
					DBA_CONFIRMDialog condlg = new DBA_CONFIRMDialog(shell);
					if (!condlg.doModal())
						return;
				}

				String requestMsg = "dbname:" + CubridView.Current_db + "\n";
				if (deleteBackup)
					requestMsg += "delbackup:y\n";
				else
					requestMsg += "delbackup:n\n";

				cs = new ClientSocket();
				if (cs.SendBackGround(shell, requestMsg, "deletedb", Messages
						.getString("WAITING.DELETEDB"))) {
					MainRegistry.Authinfo_remove(CubridView.Current_db);
					MainRegistry.DBUserInfo_remove(CubridView.Current_db);
					CubridView.Current_db = "";
					ApplicationActionBarAdvisor.refreshAction.run();
					CubridView.refresh();

					TreeItem[] itemArray = new TreeItem[1];
					itemArray[0] = (TreeItem) CubridView.viewer.getTree().getItems()[0];
					CubridView.viewer.getTree().setSelection(itemArray);
					CubridView.setViewDefault();
				} else {
					CommonTool.ErrorBox(shell, cs.ErrorMsg);
					return;
				}
			}
		} else {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
	}
}
