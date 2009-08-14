package com.cubrid.cubridmanager.core.common.xml;

import java.io.ByteArrayInputStream;

import junit.framework.TestCase;

import org.eclipse.core.runtime.Preferences;

import com.cubrid.cubridmanager.core.CubridManagerCorePlugin;

public class XMLMementoTest extends
		TestCase {
	private final String XML_CONTENT = "CUBRID_XML_CONTENT";

	public void testXMLMemento() {
		try {
			//save xml
			XMLMemento memento = XMLMemento.createWriteRoot("hosts");
			IXMLMemento child = memento.createChild("host");
			assertTrue(memento.getChild("host") != null);
			child.putString("id", "localhost");
			child.putString("name", "localhost");
			child.putInteger("port", 8001);
			child.putBoolean("isLocal", true);
			child.putString("address", "10.34.63.123");
			child.putString("user", "admin");
			String xmlString = memento.saveToString();
			Preferences prefs = CubridManagerCorePlugin.getDefault().getPluginPreferences();
			prefs.setValue(XML_CONTENT, xmlString);
			CubridManagerCorePlugin.getDefault().savePluginPreferences();
			//load xml
			Preferences preference = CubridManagerCorePlugin.getDefault().getPluginPreferences();
			xmlString = preference.getString(XML_CONTENT);
			if (xmlString != null && xmlString.length() > 0) {
				try {
					ByteArrayInputStream in = new ByteArrayInputStream(
							xmlString.getBytes("UTF-8"));
					IXMLMemento memento1 = XMLMemento.loadMemento(in);
					IXMLMemento[] children = memento1.getChildren("host");
					for (int i = 0; i < children.length; i++) {
						String id = children[i].getString("id");
						String name = children[i].getString("name");
						String address = children[i].getString("address");
						int port = children[i].getInteger("port");
						boolean isLocal = children[i].getBoolean("isLocal");
						String user = children[i].getString("user");
						assertEquals("localhost", id);
						assertEquals("localhost", name);
						assertEquals("8001", port + "");
						assertEquals("10.34.63.123", address);
						assertTrue(isLocal);
						assertEquals("admin", user);
					}
				} catch (Exception e) {
				}
			}
			assertTrue(child.getFloat("key") == null);
			child.putTextData("testTextData");
			assertTrue(child.getTextData() != null);
			assertTrue(child.getAttributeNames().size() > 0);
		} catch (Exception e) {
		}
	}

}
