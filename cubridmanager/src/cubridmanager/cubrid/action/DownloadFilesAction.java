package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.action.BackupDownThread;
import cubridmanager.cubrid.dialog.BACKFILE_DOWNDialog;
import cubridmanager.cubrid.dialog.FILEDOWN_PROGRESSDialog;
import cubridmanager.cubrid.view.CubridView;

public class DownloadFilesAction extends Action {
	public static BackupDownThread backwork = null;

	public DownloadFilesAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("DownloadFilesAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		if (FILEDOWN_PROGRESSDialog.isdownloading) { // another download
														// exist
			CommonTool.ErrorBox(shell, Messages
					.getString("ERROR.DOWNLOADINPROGRESS"));
			return;
		}
		AuthItem ai = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (ai == null) {
			CommonTool.ErrorBox(shell, Messages.getString("MSG.SELECTDB"));
			return;
		}
		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "dbname:" + ai.dbname, "backupdbinfo")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		BACKFILE_DOWNDialog dlg = new BACKFILE_DOWNDialog(shell);
		if (dlg.doModal() && backwork != null) {
			backwork.run(); // file down thread run
		}
	}
}
