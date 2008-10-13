package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.cubrid.dialog.RENAME_CLASSDialog;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBSchema;

public class TableRenameAction extends Action {
	public TableRenameAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("TableRenameAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (DBSchema.CurrentObj == null)
			return;
		Shell shell = new Shell();
		RENAME_CLASSDialog dlg = new RENAME_CLASSDialog(shell);
		if (dlg.doModal()) {
			if (DBSchema.CurrentObj.equals(dlg.classname))
				return;
			ClientSocket cs = new ClientSocket();

			if (!cs.SendBackGround(shell, "dbname:" + CubridView.Current_db
					+ "\noldclassname:" + DBSchema.CurrentObj
					+ "\nnewclassname:" + dlg.classname, "renameclass",
					Messages.getString("WAITING.RENAMECLASS"))) {
				CommonTool.ErrorBox(shell, cs.ErrorMsg);
				return;
			}

			ClientSocket csClassInfo = new ClientSocket();
			if (!csClassInfo.SendClientMessage(shell, "dbname:"
					+ CubridView.Current_db + "\ndbstatus:on", "classinfo")) {
				CommonTool.ErrorBox(shell, csClassInfo.ErrorMsg);
				return;
			}

			CubridView.myNavi.createModel();
			CubridView.viewer.refresh();
		}
	}
}
