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
}


#endif /* CCI_MUTEX_H_ */
