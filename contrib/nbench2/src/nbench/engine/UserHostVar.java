package nbench.engine;

import java.util.HashMap;

public class UserHostVar {
	public String name;
	public String class_name;
	public HashMap<String, String> map;
	
	public UserHostVar(String name) {
		this.name = name;
		this.class_name = null;
		this.map = new HashMap<String,String>();
	}
}
