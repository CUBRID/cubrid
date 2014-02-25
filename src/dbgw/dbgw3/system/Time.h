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

#ifndef TIME_H_
#define TIME_H_

namespace dbgw
{

  namespace system
  {

    namespace pool
    {
      static const int INFINITE_TIMEOUT = -1;
      static const int NOWAIT_TIMEOUT = 0;
    }

    static const long UNDEFINED_TIMEOUT = -2;
    static const unsigned long INFINITE_TIMEOUT = 0;

    unsigned long long int getCurrTimeMilSec();
    unsigned long long int getdifftimeofday(struct timeval &begin);
    std::string getTimeStrFromMilSec(unsigned long long int ulMilSec);

  }

}

#endif
