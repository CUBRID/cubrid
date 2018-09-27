/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */



#ifndef _TEST_LOG_GENERATOR_HPP_
#define _TEST_LOG_GENERATOR_HPP_

namespace test_replication
{

/* disable log generator tests 
 * since interface of log_generator changed to high-level objects, it is not possible to simulate master node state 
 */
#if 0
  int test_log_generator1 (void);

  int test_log_generator2 (void);
#endif /* disable unit test code */

}

#endif /* _TEST_LOG_GENERATOR_HPP_ */
