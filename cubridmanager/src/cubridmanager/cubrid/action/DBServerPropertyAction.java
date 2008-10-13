package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.DATABASE_SERVERPROPERTYDialog;
import cubridmanager.cubrid.view.CubridView;

public class DBServerPropertyAction extends Action {
	public DBServerPropertyAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("DBServerPropertyAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		AuthItem ai = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (ai == null) {
			CommonTool.ErrorBox(shell, Messages.getString("MSG.SELECTDB"));
			return;
		}
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(shell, "dbname:" + ai.dbname, "getallsysparam",
				Messages.getString("WAITING.GETTINGSERVERINFO"))) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		DATABASE_SERVERPROPERTYDialog dlg = new DATABASE_SERVERPROPERTYDialog(
				shell);
		dlg.doModal();
	}
}
