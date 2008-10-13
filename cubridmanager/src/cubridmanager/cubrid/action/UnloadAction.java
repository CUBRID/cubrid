package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WaitingMsgBox;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.UNLOADDBDialog;
import cubridmanager.cubrid.dialog.UNLOADRESULTDialog;
import cubridmanager.cubrid.view.CubridView;

public class UnloadAction extends Action {
	public static AuthItem ai = null;

	public UnloadAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("UnloadAction");
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
		ClientSocket cs = new ClientSocket();

		if (cs.Connect()) {
			if (cs.Send(shell, "dbname:" + CubridView.Current_db
					+ "\ndbstatus:off", "classinfo")) {
				WaitingMsgBox wdlg = new WaitingMsgBox(shell);
				wdlg.run(Messages.getString("WAITING.CLASSINFO"));
				if (cs.ErrorMsg != null) {
					CommonTool.ErrorBox(shell, cs.ErrorMsg);
					return;
				}

				UNLOADDBDialog dlg = new UNLOADDBDialog(shell);
				if (dlg.doModal()) { // check dir & check files success
					if (dlg.isSchemaOnly) {
						CommonTool.MsgBox(shell, Messages
								.getString("MSG.SUCCESS"), Messages
								.getString("MSG.SCHEMAUNLOADOK"));
					} else {
						UNLOADRESULTDialog udlg = new UNLOADRESULTDialog(shell);
						udlg.doModal();
					}
				}
			} else {
				CommonTool.ErrorBox(shell, cs.ErrorMsg);
				return;
			}
		} else {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
	}
}
