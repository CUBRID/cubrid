package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.ADD_QUERYPLANDialog;
import cubridmanager.cubrid.view.CubridView;

public class QueryPlanAction extends Action {
	public QueryPlanAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("QueryPlanAction");
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
		ADD_QUERYPLANDialog dlg = new ADD_QUERYPLANDialog(shell);
		dlg.gubun = "add";
		dlg.dbname = ai.dbname;
		dlg.doModal();
	}
}
