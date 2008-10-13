package cubridmanager.query.action;

import org.eclipse.jface.action.Action;

import cubridmanager.MainRegistry;
import cubridmanager.query.view.QueryEditor;

public class CommentDeleteAction extends Action {
	public CommentDeleteAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("CommentDeleteAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText("TOOLTIP." + text);
	}

	public void run() {
		QueryEditor qe = MainRegistry.getCurrentQueryEditor();
		if (qe == null)
			return;
		qe.uncomment();
	}
}
