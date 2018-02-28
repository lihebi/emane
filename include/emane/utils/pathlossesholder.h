/**
 * @author Zehua Li
 */

#ifndef EMANEUTILSPATHLOSSESHOLDER_HEADER_
#define EMANEUTILSPATHLOSSESHOLDER_HEADER_

#include "emane/events/pathloss.h"


namespace EMANE
{
  namespace Utils
  {
    extern Events::Pathlosses pathlossesHolder = {};
    extern bool initialized = false;
  }
}

#endif // EMANEUTILSPATHLOSSESHOLDER_HEADER_
