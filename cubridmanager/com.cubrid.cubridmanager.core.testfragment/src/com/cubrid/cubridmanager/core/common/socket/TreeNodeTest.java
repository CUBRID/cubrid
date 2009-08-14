package com.cubrid.cubridmanager.core.common.socket;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.Tool;

public class TreeNodeTest extends SetupEnvTestCase {
	public void testOrder(){
		String msg="task:getdbmtuserinfo\n"+
				"open:dbauth\n"+
				"dbname:testdb\n"+
				"dbid:dba1\n"+
				"dbpasswd:1\n"+
				"dbname:demodb\n"+
				"dbid:dba2\n"+
				"dbpasswd:2\n"+
				"close:dbauth\n";
		TreeNode node = MessageUtil.parseResponse(msg);
		node=node.getChildren().get(0);
		
		String[] dbnames=node.getValues("dbname");
		String[] dbids=node.getValues("dbid");
		String[] dbpasswords=node.getValues("dbpasswd");
		assertEquals("testdb", dbnames[0]);
		assertEquals("demodb", dbnames[1]);
		assertEquals("dba1", dbids[0]);
		assertEquals("dba2", dbids[1]);
		assertEquals("1", dbpasswords[0]);
		assertEquals("2", dbpasswords[1]);
	}

	public void testDIAG_DEL() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/receive.msg/DIAG_DEL-getdiagdata");
		String msg = Tool.getFileContent(filepath);	
		

		TreeNode node = MessageUtil.parseResponse(msg, true);
		assertEquals("getdiagdata", node.getValue("task"));
		assertEquals("success", node.getValue("status"));
		// test child node
		node = node.getChildren().get(0);
		assertEquals("0", node.getValue("cas_mon_act_session"));
		assertEquals("0", node.getValue("cas_mon_tran"));
		assertEquals("0", node.getValue("cas_mon_req"));
	}

	public void testMultiValues() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/receive.msg/class");
		String msg = Tool.getFileContent(filepath);	

		TreeNode node = MessageUtil.parseResponse(msg);
		TreeNode node2 = node.getChildren().get(0);
		TreeNode node3 = node2.getChildren().get(10);
		assertEquals("constraint", node3.getValue("open"));
		String[] attributes = node3.getValues("attribute");
		String[] rules = node3.getValues("rule");
		assertEquals("class_of", attributes[0]);
		assertEquals("class_of ASC", rules[0]);
		node3.modifyValues("rule", new String[] { "1", "2", "3" });
		rules = node3.getValues("rule");
		assertEquals("1", rules[0]);
		assertEquals("2", rules[1]);
		assertEquals("3", rules[2]);
	}

	public void testMultiColon() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/receive.msg/analyzecaslog");
		String msg = Tool.getFileContent(filepath);	
		
		TreeNode node = MessageUtil.parseResponse(msg);
		assertEquals("C:\\CUBRID/tmp/analyzelog_5004.res", node.getValue("resultfile"));
	}

	public void testAddChild() throws Exception {
		String filepath=this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/task/attribute/test.message/addattribute_receive");
		String msg = Tool.getFileContent(filepath);

		TreeNode node = MessageUtil.parseResponse(msg);
		assertEquals("addattribute", node.getValue("task"));

		TreeNode node2 = node.getChildren().get(0);
		assertEquals("classinfo", node2.getValue("open"));
		assertEquals("demodb", node2.getValue("dbname"));

		TreeNode node3 = node2.getChildren().get(0);
		assertEquals("attribute", node3.getValue("open"));
		assertEquals("host_year", node3.getValue("name"));
	}

	public void testGet() {
		String msg = "task:authenticate\n" + "status:success\n" + "note:none\n"
		        + "token:8ec1ab8a91333c78ae2c9a2b32cbafd84585fd932e7e2fac88b7d31a95a5fadf7926f07dd201b6aa\n\n";

		TreeNode node = MessageUtil.parseResponse(msg);
		assertEquals("authenticate", node.getValue("task"));
		assertEquals("success", node.getValue("status"));
		assertEquals("none", node.getValue("note"));
		assertEquals("8ec1ab8a91333c78ae2c9a2b32cbafd84585fd932e7e2fac88b7d31a95a5fadf7926f07dd201b6aa", node
		        .getValue("token"));
	}

	public void testToString() {
		String msg = "task:authenticate\n" + "status:success\n" + "note:none\n"
		        + "token:8ec1ab8a91333c78ae2c9a2b32cbafd84585fd932e7e2fac88b7d31a95a5fadf7926f07dd201b6aa\n\n";

		TreeNode node = MessageUtil.parseResponse(msg);
		assertEquals(msg, node.toString());
	}

	public void testModifyValue() {
		String msg = "task:authenticate\n" + "status:success\n" + "note:none\n"
		        + "token:8ec1ab8a91333c78ae2c9a2b32cbafd84585fd932e7e2fac88b7d31a95a5fadf7926f07dd201b6aa\n\n";

		TreeNode node = MessageUtil.parseResponse(msg);
		node.modifyValue("note", "test");
		assertEquals("test", node.getValue("note"));
	}

}
