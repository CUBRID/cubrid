package nbench.engine;

import nbench.common.NBenchException;
import javax.script.Bindings;
import javax.script.Compilable;
import javax.script.ScriptEngine;
import javax.script.CompiledScript;
import java.io.Reader;
import java.io.StringReader;

class ScriptRun {
	private String name;
	private CompiledScript bin;
	private Reader rdr;

	ScriptRun(String name, ScriptEngine se, String common_script, CharSequence script)
			throws NBenchException {
		this.name = name;
		this.rdr = new StringReader(common_script + script.toString());

		try {
			Compilable comp = (Compilable) se;
			bin = comp.compile(rdr);
		} catch (Exception e) {
			throw new NBenchException(e);
		}
	}
	
	public String getName() {
		return name;
	}

	void run(Bindings binds) throws Exception {
		bin.eval(binds);
	}
}
