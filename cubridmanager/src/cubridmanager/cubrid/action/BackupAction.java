package cubridmanager.cubrid.action;

import java.util.ArrayList;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.MainConstants;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.BACKUPDBDialog;
import cubridmanager.cubrid.dialog.BACKUPVOLINFODialog;
import cubridmanager.cubrid.view.CubridView;

public class BackupAction extends Action {
	public static AuthItem ai = null;
	public static ArrayList backinfo = new ArrayList();
	public static String dbdir = "";
	public static String free_space = "";

	public BackupAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("BackupAction");
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
		if (cs.SendClientMessage(shell, "dbname:" + ai.dbname, "backupdbinfo")) {
			BACKUPDBDialog dlg = new BACKUPDBDialog(shell);
			if (dlg.doModal()) { // if database is active, don't display volume information.
				if (ai.status == MainConstants.STATUS_START) {
					CommonTool.MsgBox(shell, Messages.getString("MSG.SUCCESS"),
							Messages.getString("MSG.BACKUPDBSUCCESS"));
				} else {
					shell.update();
					cs = new ClientSocket();
					if (cs.SendBackGround(shell, "dbname:" + ai.dbname,
							"backupvolinfo", Messages
									.getString("WAITING.GETTINGBACKUPINFO"))) {
						BACKUPVOLINFODialog bdlg = new BACKUPVOLINFODialog(
								shell);
						bdlg.doModal();
					} else
						CommonTool.ErrorBox(shell, cs.ErrorMsg);
				}
			}
		} else
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
	}
}
