package com.cubrid.cubridmanager.core;

import com.cubrid.cubridmanager.core.common.socket.MyMapTest;
import com.cubrid.cubridmanager.core.common.socket.TaskTest;

import junit.framework.Test;
import junit.framework.TestSuite;

public class AllTest {
	public static Test suite() {

		TestSuite suite = new TestSuite("Test for com.cubrid.cubridmanager.core");
		// $JUnit-BEGIN$
//		suite.addTestSuite(ClientSocketTest.class);
		suite.addTestSuite(MyMapTest.class);
		suite.addTestSuite(TaskTest.class);
//		suite.addTestSuite(TreeNodeTest.class);
//
//		suite.addTestSuite(GetdiagdataTaskTest.class);
//		suite.addTestSuite(UpdateAttributeTaskTest.class);
		// $JUnit-END$
		return suite;
		
	}

}
