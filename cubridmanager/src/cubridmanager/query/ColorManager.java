package cubridmanager.query;

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.graphics.RGB;
import org.eclipse.swt.widgets.Display;

import cubridmanager.MainRegistry;

/**
 * This class manages the colors. It uses a lazy initialization approach, only
 * creating the actual color if it's requested.
 */
public class ColorManager {
	public static final RGB BACKGROUND = new RGB(255, 255, 255);
	public static final RGB COMMENT = new RGB(0, 128, 0);
	public static final RGB DEFAULT = new RGB(0, 0, 0);
	public static final RGB KEYWORD = new RGB(0, 128, 128);
	public static final RGB NUMBER = new RGB(255, 0, 255);
	public static final RGB STRING = new RGB(255, 0, 0);

	// Map to store created colors, with the corresponding RGB as key
	private Map colors = new HashMap();

	/**
	 * Gets a color
	 * 
	 * @param rgb
	 *            the corresponding rgb
	 * @return Color
	 */
	public Color getColor(RGB rgb) {
		// Get the color from the map
		RGB new_rgb = null;

		if (rgb.equals((Object) DEFAULT)
				&& (!MainRegistry.queryEditorOption.fontString.equals(""))) {
			new_rgb = new RGB(MainRegistry.queryEditorOption.fontColorRed,
					MainRegistry.queryEditorOption.fontColorGreen,
					MainRegistry.queryEditorOption.fontColorBlue);
		} else {
			new_rgb = rgb;
		}

		Color color = (Color) colors.get(new_rgb);
		if (color == null) {
			// Color hasn't been created yet; create and put in map
			color = new Color(Display.getCurrent(), new_rgb);
			colors.put(new_rgb, color);
		}
		return color;
	}

	/**
	 * Dispose any created colors
	 */
	public void dispose() {
		for (Iterator itr = colors.values().iterator(); itr.hasNext();)
			((Color) itr.next()).dispose();
	}
}
