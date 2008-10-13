package cubridmanager.query;

public class StructQueryExample implements Comparable {
	public String id;
	public int[] refNum;

	public StructQueryExample(String name, int[] num) {
		id = name;
		refNum = num;
	}

	public int compareTo(Object obj) {
		return id.compareTo(((StructQueryExample) obj).id);
	}
}
