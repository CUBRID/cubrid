package com.cubrid.cubridmanager.core.mock;

public class MockServerRunner extends Thread {

	private IMockServer server = null;
	
	public static void main(String[] args) {

		new MockServerRunner(new MockAuthServer()).start();
		new MockServerRunner(new MockTaskServer()).start();
		
		for (;;) {
			try {
				Thread.sleep(0);
			} catch (Exception ignored) {
				
			}
		}
		
	}
	
	public MockServerRunner(IMockServer aServer) {
		
		this.server = aServer;
		
	}
	
	public void run() {
		
		try {
			this.server.initialize();
		} catch (Exception e) {
			e.printStackTrace();
		}
		
	}

}
