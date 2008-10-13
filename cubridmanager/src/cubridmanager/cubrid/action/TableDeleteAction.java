package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;

import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBSchema;
import cubridmanager.query.action.QueryDeleteAllAction;

public class TableDeleteAction extends Action {
	public TableDeleteAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("TableDeleteAction");
		if (img != null) {
			setImageDescriptor(cubridmanager.CubridmanagerPlugin.getImageDescriptor(img));
		}
		setToolTipText(text);
	}

	public void run() {
		if (CubridView.Current_db == null || DBSchema.CurrentObj == null
			|| MainRegistry.Authinfo_find(CubridView.Current_db).dbuser.length() <= 0) {
			return;
		}
		if (CommonTool.WarnYesNo(Messages.getString("WARNYESNO.DELETEALL")) != SWT.YES)	{
			return;
		}
		QueryDeleteAllAction.deleteAll(CubridView.Current_db, DBSchema.CurrentObj);
	}
}
