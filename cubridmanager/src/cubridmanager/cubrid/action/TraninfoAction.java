package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.KILLTRANDialog;
import cubridmanager.cubrid.view.CubridView;

public class TraninfoAction extends Action {
	public static AuthItem ai = null;

	public TraninfoAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("TraninfoAction");
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
		if (ai.status != MainConstants.STATUS_START) {
			CommonTool.ErrorBox(shell, Messages
					.getString("ERROR.DATABASEISNOTRUNNING"));
			return;
		}
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(shell, "dbname:" + ai.dbname,
				"gettransactioninfo", Messages
						.getString("WAITING.GETTINGTRANSACTION"))) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		KILLTRANDialog dlg = new KILLTRANDialog(shell);
		dlg.doModal();
	}
}
