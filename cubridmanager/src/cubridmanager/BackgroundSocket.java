package cubridmanager;

import org.eclipse.swt.widgets.Shell;

public class BackgroundSocket extends Thread {
	Shell tsh = null;
	String tmsg = null;
	String ttaskType = null;
	public ClientSocket sc = new ClientSocket();
	public boolean result = false;
	public boolean isrunning = true;

	public BackgroundSocket(Shell sh, String msg, String taskType) {
		tsh = new Shell(sh);
		tmsg = new String(msg);
		ttaskType = new String(taskType);
	}

	public void run() {
		isrunning = true;
		result = sc.SendClientMessage(tsh, tmsg, ttaskType);
		isrunning = false;
	}
}
