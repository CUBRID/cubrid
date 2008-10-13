package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;

import cubridmanager.MainRegistry;
import cubridmanager.WorkView;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBSchema;
import cubridmanager.query.view.QueryEditor;

public class TableSelectAllAction extends Action {
	public TableSelectAllAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("TableSelectAllAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (CubridView.Current_db == null || DBSchema.CurrentObj == null)
			return;
		WorkView.SetView(QueryEditor.ID, MainRegistry
				.getDBUserInfo(CubridView.Current_db), DBSchema.CurrentObj
				+ ":SELECTALL");
	}
}
