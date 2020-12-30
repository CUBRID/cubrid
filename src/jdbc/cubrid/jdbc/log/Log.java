/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */
package cubrid.jdbc.log;

public interface Log {
	void logDebug(String msg);
	void logDebug(String msg, Throwable thrown);
	void logError(String msg);
	void logError(String msg, Throwable thrown);
	void logFatal(String msg);
	void logFatal(String msg, Throwable thrown);
	void logInfo(String msg);
	void logInfo(String msg, Throwable thrown);
	void logTrace(String msg);
	void logTrace(String msg, Throwable thrown);
	void logWarn(String msg);
	void logWarn(String msg, Throwable thrown);
}
