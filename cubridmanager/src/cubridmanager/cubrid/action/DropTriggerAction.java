package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBTriggers;

import org.eclipse.swt.widgets.Shell;

public class DropTriggerAction extends Action {
	public DropTriggerAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("DropTriggerAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (DBTriggers.Current_select.length() <= 0)
			return;
		if (CommonTool.WarnYesNo(Messages.getString("WARNYESNO.DROPTRIGGER")) != SWT.YES)
			return;

		Shell dlgShell = new Shell();
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(dlgShell, "dbname:" + CubridView.Current_db
				+ "\ntriggername:" + DBTriggers.Current_select, "droptrigger",
				Messages.getString("WAITING.DELTRIGGER"))) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			return;
		}

		CommonTool.MsgBox(dlgShell, Messages.getString("MSG.SUCCESS"), Messages
				.getString("MSG.DELTRIGGERSUCCESS"));

		cs = new ClientSocket();
		if (!cs.SendClientMessage(dlgShell, "dbname:" + CubridView.Current_db,
				"gettriggerinfo")) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			return;
		}
		WorkView.DeleteView(DBTriggers.ID);
		CubridView.myNavi.createModel();
		CubridView.viewer.refresh();
	}
}
