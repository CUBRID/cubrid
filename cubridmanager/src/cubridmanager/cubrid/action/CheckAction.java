package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.CHECKDBDialog;
import cubridmanager.cubrid.view.CubridView;

public class CheckAction extends Action {
	public static AuthItem ai = null;

	public CheckAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("CheckAction");
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
		CHECKDBDialog dlg = new CHECKDBDialog(shell);
		if (dlg.doModal()) {
			CommonTool.MsgBox(shell, Messages.getString("MSG.SUCCESS"),
					Messages.getString("MSG.CHECKDBSUCCESS"));
		}
	}
}
