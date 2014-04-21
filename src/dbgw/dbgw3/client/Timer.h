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

#ifndef TIMER_H_
#define TIMER_H_

namespace dbgw
{

  class _AsyncWorkerJob;
  typedef boost::shared_ptr<_AsyncWorkerJob> _AsyncWorkerJobSharedPtr;

  class _TimerEvent;
  typedef std::vector<_TimerEvent *> _TimerEventList;

  class _TimerEvent
  {
  public:
    _TimerEvent(_AsyncWorkerJobSharedPtr pJob);
    virtual ~_TimerEvent() {}

    void wakeup();
    bool isDone();

  public:
    bool needWakeUp(unsigned long long int ulCurrTimeMilSec) const;

  private:
    unsigned long long int m_ulAbsTimeOutMilSec;
    _AsyncWorkerJobSharedPtr m_pJob;
    bool m_bIsDone;
  };

  class _Timer : public system::_ThreadEx
  {
  public:
    _Timer();
    virtual ~_Timer();

    void addEvent(_TimerEvent *pJob);

  private:
    class Impl;
    Impl *m_pImpl;
  };

}

#endif
