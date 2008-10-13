package cubridmanager.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.IntervalTimer;
import cubridmanager.cas.dialog.REFRESH_INTERVALDialog;
import cubridmanager.cas.view.CASView;

public class RefreshIntervalAction extends Action {
	public static int time_interval = 0;
	public static boolean prc_loop = false;
	static IntervalTimer prc = null;
	static RefreshIntervalAction myaction = null;
	public static String broker_name = new String("");
	public RefreshIntervalAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("RefreshIntervalAction");
		if (img != null) {
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
			setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img.replaceFirst("icons",
							"disable_icons")));
		}
		setToolTipText(text);
		myaction = this;
	}

	public void run() {
		Shell shell = new Shell();
		REFRESH_INTERVALDialog dlg = new REFRESH_INTERVALDialog(shell);
		dlg.time_interval = time_interval;
		dlg.doModal();
		if (time_interval >= REFRESH_INTERVALDialog.MIN_BROKER_REFRESH_SEC)
			settimer();
		else
			stoptimer();
	}

	public static void settimer() {
		if (CASView.Current_broker.length() > 0)
			broker_name = new String(CASView.Current_broker);
		if (prc != null && prc_loop)
			return;
		prc = new IntervalTimer(myaction);
		prc_loop = true;
		prc.start();
	}

	public static void stoptimer() {
		if (prc != null && prc_loop)
			prc.stop();
		prc = null;
		prc_loop = false;
	}

}
