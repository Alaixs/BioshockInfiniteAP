#include "ap_client.h"

#include <windows.h>

namespace bsap::apclient {

// Not in ap_client.cpp: the loop test links its own clock instead of this file.
unsigned long NowMs() {
    return GetTickCount();
}

}  // namespace bsap::apclient
