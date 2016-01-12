/*
 * cci_mutex.h
 *
 *  Created on: Nov 7, 2012
 *      Author: siwankim
 */

#ifndef CCI_MUTEX_H_
#define CCI_MUTEX_H_

#include "porting.h"

namespace cci
{
  class _Mutex
  {
  private:
    pthread_mutex_t mutex;

  public:
    _Mutex ()
    {
      pthread_mutex_init (&mutex, NULL);
    }

     ~_Mutex ()
    {
      pthread_mutex_destroy (&mutex);
    }

    int lock ()
    {
      return pthread_mutex_lock (&mutex);
    }

    int unlock ()
    {
      return pthread_mutex_unlock (&mutex);
    }
  };

  class _MutexAutolock
  {
  public:
    explicit _MutexAutolock (_Mutex * mutex):mutex (mutex), is_unlocked (true)
    {
      mutex->lock ();
    }

    virtual ~ _MutexAutolock ()
    {
      unlock ();
    }

    void unlock ()
    {
      if (is_unlocked)
	{
	  is_unlocked = false;
	  mutex->unlock ();
	}
    }

  private:
    _Mutex * mutex;
    bool is_unlocked;

    _MutexAutolock (const _MutexAutolock &);
    void operator= (const _MutexAutolock &);
  };
}


#endif /* CCI_MUTEX_H_ */
