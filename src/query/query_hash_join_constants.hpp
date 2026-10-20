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

/*
 * query_hash_join_constants.hpp - sizes and limits of the hash join split
 */

#ifndef _QUERY_HASH_JOIN_CONSTANTS_HPP_
#define _QUERY_HASH_JOIN_CONSTANTS_HPP_

#include "memory_alloc.h"	/* DB_ALIGN, MAX_ALIGNMENT */
#include "storage_common.h"	/* DB_PAGESIZE */

/* part of max_hash_list_scan_size a partition aims at, so that a partition sized by estimate still fits */
#define PARTITION_FILL_FACTOR 0.8

/* most partitions of a split, the partition of NULL join keys included; a larger partition is split again before
 * the join (hjoin_resplit_partitions) */
#define HASHJOIN_PART_MAX_CNT 1024

/* splits a context may come from: the single context is split, and a partition of it is split again */
#define HASHJOIN_SPLIT_MAX_LEVEL 2

/* landing buffer of a split writer, shared by all partitions, so its size does not grow with the partition count */
#define HASHJOIN_SPLIT_LANDING_BUFFER_SIZE (64 * DB_PAGESIZE)
#define HASHJOIN_SPLIT_LANDING_ENTRY_HEADER_SIZE DB_ALIGN (sizeof (int), MAX_ALIGNMENT)

#endif /* _QUERY_HASH_JOIN_CONSTANTS_HPP_ */
