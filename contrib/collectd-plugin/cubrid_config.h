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
 * cubrid_config.h - define for bit model
 */

#ifndef _CUBRID_CONFIG_H_
#define _CUBRID_CONFIG_H_

#if defined (ARCH_32)

#define SIZEOF_CHAR 		1
#define SIZEOF_INT		4
#define SIZEOF_LONG		4
#define SIZEOF_LONG_LONG	8
#define SIZEOF_SHORT		2
#define SIZEOF_VOID_P		4

#elif defined (ARCH_LP64)

#define SIZEOF_CHAR 		1
#define SIZEOF_INT		4
#define SIZEOF_LONG		8
#define SIZEOF_LONG_LONG	8
#define SIZEOF_SHORT		2
#define SIZEOF_VOID_P		8

#else

#error "choose correct bit_model in Makefile"

#endif

#endif /* _CUBRID_CONFIG_H */
