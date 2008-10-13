package cubridmanager.diag;

public class DiagActivityResult {
	private String EventClass = new String();
	private String TextData = new String();
	private String BinData = new String();
	private String IntegerData = new String();
	private String Time = new String();

	public DiagActivityResult() {
		EventClass = "";
		TextData = "";
		BinData = "";
		IntegerData = "";
		Time = "";
	}

	public DiagActivityResult(DiagActivityResult clone) {
		SetEventClass(clone.GetEventClass());
		SetTextData(clone.GetTextData());
		SetBinData(clone.GetBinData());
		SetIntegerData(clone.GetIntegerData());
		SetTimeData(clone.GetTimeData());
	}

	public void SetEventClass(String value) {
		EventClass = value;
	}

	public void SetTextData(String value) {
		TextData = value;
	}

	public void SetBinData(String value) {
		BinData = value;
	}

	public void SetIntegerData(String value) {
		IntegerData = value;
	}

	public void SetTimeData(String value) {
		Time = value;
	}

	public String GetEventClass() {
		return EventClass;
	}

	public String GetTextData() {
		return TextData;
	}

	public String GetBinData() {
		return BinData;
	}

	public String GetIntegerData() {
		return IntegerData;
	}

	public String GetTimeData() {
		return Time;
	}
}
