package cubridmanager;

import org.eclipse.jface.viewers.LabelProvider;
import org.eclipse.swt.graphics.Image;

public class ViewLabelProvider extends LabelProvider {
	public String getText(Object obj) {
		return obj.toString();
	}

	public Image getImage(Object obj) {
		String iconpath = ((TreeObject) obj).getImageName();

		if (iconpath == null)
			return null;
		return CubridmanagerPlugin.getImageDescriptor(iconpath).createImage();
	}
}
