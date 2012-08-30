#ifndef _EXACTLYONE_H_
#define _EXACTLYONE_H_

/**
 * @class ExactlyOne
 * @brief Creates a singleton of type CLASS, accessed through ().
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */

namespace Hoard {

template <class CLASS>
class ExactlyOne {
public:
  
  inline CLASS& operator() (void) {
    static CLASS theOneTrueInstance;
    return theOneTrueInstance;
  }

};

};

#endif

