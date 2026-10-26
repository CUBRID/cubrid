/*
 *
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


/*
 * system_metadata_version.h - System metadata version constant.
 *
 * Increment on release when system metadata (system catalog, information_schema,
 * etc.) changes. Stored in LOG_HEADER (UINT16); boot mismatch requires
 * 'cubrid upgradedb'. Version 0 marks databases created before 11.5.0;
 * they must be recreated.
 */

#ifndef _SYSTEM_METADATA_VERSION_H_
#define _SYSTEM_METADATA_VERSION_H_

#define SYSTEM_METADATA_VERSION 1

#endif /* _SYSTEM_METADATA_VERSION_H_ */
