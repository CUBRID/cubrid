package cubridmanager.query.action;

import org.eclipse.jface.action.Action;

import cubridmanager.MainRegistry;
import cubridmanager.query.view.QueryEditor;

public class CopyClipboardAction extends Action {
	public CopyClipboardAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("CopyClipboardAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText("TOOLTIP." + text);
	}

	public void run() {
		QueryEditor qe = MainRegistry.getCurrentQueryEditor();
		if (qe == null)
			return;
		qe.copy();
	}
}
