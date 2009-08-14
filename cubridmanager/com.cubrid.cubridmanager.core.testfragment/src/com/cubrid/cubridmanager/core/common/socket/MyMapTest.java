package com.cubrid.cubridmanager.core.common.socket;

import junit.framework.TestCase;

import com.cubrid.cubridmanager.core.common.socket.MessageMap;

public class MyMapTest extends TestCase {
	public void testGroup() {
		String[] orders =
			{
			        "task",
			        "token",
			        "dbname",
			        "classname",
			        "type",
			        "name",
			        "attributecount",
			        "{attribute,attribute_order}",
			        "attribute_order",
			        "category",
			};
		MessageMap map = new MessageMap(orders);
		String[] attributes =
			{
			        "testkey1", "testkey2", "testkey3", "testkey4"
			};
		String[] attribute_orders =
			{
			        "ASC1", "DESC2", "ASC3", "DESC4"
			};
		for (int i = 0; i < attribute_orders.length; i++) {
			map.add("attribute_order", attribute_orders[i]);
		}
		for (int i = 0; i < attributes.length; i++) {
			map.add("attribute", attributes[i]);
		}
		map.add("attributecount", "" + attributes.length);
		map.add("category", "class");
	}

	public void testMultiValues() {
		String multivalueMSG = "task:addconstraint\n"
		        + "token:8ec1ab8a91333c7838c459d4e9e829644ea86d4c6c404cdbf4cd8b6186f50bf27926f07dd201b6aa\n"
		        + "dbname:test\n" + "classname:test2\n" + "type:PRIMARY KEY\n" + "name:\n" + "attributecount:3\n"
		        + "attribute:type_id\n" + "attribute:type_name\n" + "attribute:object_of\n" + "category:instance\n\n";

		String[] sendMSGOrder = new String[]
			{
			        "task", "token", "dbname", "classname", "type", "name", "attributecount", "attribute", "category",
			};
		MessageMap map = new MessageMap(sendMSGOrder);

		String[] lines = multivalueMSG.split("\n");
		for (String str : lines) {
			if (str.equals(""))
				continue;
			int index = str.indexOf(":");
			String key = str.substring(0, index);
			String value = str.substring(index + 1);
			map.add(key, value);
		}
		assertEquals(multivalueMSG, map.toString());

		MessageMap map2 = new MessageMap(sendMSGOrder);
		for (int i = lines.length - 1; i >= 0; i--) {
			String str = lines[i];
			if (str.equals(""))
				continue;
			int index = str.indexOf(":");
			String key = str.substring(0, index);
			String value = str.substring(index + 1);
			map2.add(key, value);
		}
		String multivalueMSG2 = "task:addconstraint\n"
		        + "token:8ec1ab8a91333c7838c459d4e9e829644ea86d4c6c404cdbf4cd8b6186f50bf27926f07dd201b6aa\n"
		        + "dbname:test\n" + "classname:test2\n" + "type:PRIMARY KEY\n" + "name:\n" + "attributecount:3\n"
		        + "attribute:object_of\n" + "attribute:type_name\n" + "attribute:type_id\n" + "category:instance\n\n";
		System.out.println(map.toString());
		assertEquals(multivalueMSG2, map2.toString());
	}

	public void test2Newline() {
		String multivalueMSG = "task:addconstraint\n"
		        + "token:8ec1ab8a91333c7838c459d4e9e829644ea86d4c6c404cdbf4cd8b6186f50bf27926f07dd201b6aa\n"
		        + "dbname:test\n" + "classname:test2\n" + "type:PRIMARY KEY\n" + "name:\n" + "attributecount:3\n"
		        + "attribute:type_id\n" + "attribute:type_name\n" + "attribute:object_of\n" + "category:instance\n\n";

		String[] sendMSGOrder = new String[]
			{
			        "task", "token", "dbname", "classname", "type", "name", "attributecount", "attribute", "category",
			};
		MessageMap map = new MessageMap(sendMSGOrder);

		String[] lines = multivalueMSG.split("\n");
		for (String str : lines) {
			if (str.equals(""))
				continue;
			int index = str.indexOf(":");
			String key = str.substring(0, index);
			String value = str.substring(index + 1);
			map.add(key, value);
		}
		assertTrue(map.toString().endsWith("\n\n"));
		assertFalse(map.toString().endsWith("\n\n\n"));
	}
}
