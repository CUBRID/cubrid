package com.cubrid.cubridmanager.core.mock;

import com.cubrid.cubridmanager.core.common.socket.ClientSocket;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;

public class TestTask {

	/**
	 * TODO: how to write comments
	 * What and why the member function does what it does
	 * What a member function must be passed as parameters
	 * What a member function re turns
	 * Known bugs
	 * Any exceptions that a member function throws
	 * Visibility decisions
	 * How a member function changes the object
	 * Include a history of any code changes
	 * Examples of how to invoke th e member function if appropriate
	 * Applicable preconditions and postconditions
	 * Document all concurrency
	 * 
	 * @param args
	 */
	public static void main(String[] args) {
		ClientSocket hostsocket = new ClientSocket("localhost", 8001);

		String message = "id:admin\npassword:1111\n"
				+ "clientver:8.1.4\n\n";
		hostsocket.sendRequest(message); // send login message
		// get the latest token
		TreeNode node = hostsocket.getResponse();
		String token = node.getValue("token");
		
		System.out.println(token);

	}

}
