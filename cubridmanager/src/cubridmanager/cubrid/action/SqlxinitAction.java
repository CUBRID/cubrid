package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.SqlxinitEditor;
import cubridmanager.cubrid.view.CubridView;

public class SqlxinitAction extends Action {
	public SqlxinitAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("SqlxinitAction");
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
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(shell, "confname:cubridconf", "getallsysparam",
				Messages.getString("WAITING.GETTINGSERVERINFO"))) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		SqlxinitEditor dlg = new SqlxinitEditor(shell);
		dlg.doModal();
	}
}
