package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;

import cubridmanager.MainRegistry;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBSchema;
import cubridmanager.query.action.QueryCountAllAction;

public class TableSelectCountAction extends Action {
	public TableSelectCountAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("TableSelectCountAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (CubridView.Current_db == null
				|| DBSchema.CurrentObj == null
				|| MainRegistry.Authinfo_find(CubridView.Current_db).dbuser
						.length() <= 0)
			return;
		QueryCountAllAction.selectCountAll(CubridView.Current_db,
				DBSchema.CurrentObj);
	}
}
