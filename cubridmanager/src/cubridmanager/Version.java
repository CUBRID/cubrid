package cubridmanager;

import java.util.MissingResourceException;
import java.util.ResourceBundle;

public class Version {
	public static final boolean cmDebugMode = true;

	private static final String BUNDLE_NAME = "version";
	private static final ResourceBundle RESOURCE_BUNDLE = ResourceBundle.getBundle(BUNDLE_NAME);

	private Version() {
	}

	public static String getString(String key) {
		try {
			return RESOURCE_BUNDLE.getString(key);
		} catch (MissingResourceException e) {
			return '!' + key + '!';
		}
	}
}
