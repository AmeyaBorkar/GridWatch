/* dispatch.h — umbrella header for libdispatch.
 *
 * Include this single header to access the entire library.
 *
 * Pulls in every module-level header (heaps, trees, strings, randomized
 * structures, spatial indexes, misc DS, and the sim API). Convenience
 * for callers who don't want to enumerate each individual header. */
#ifndef DISPATCH_H
#define DISPATCH_H

#include "dispatch/common.h"
#include "dispatch/heaps.h"
#include "dispatch/trees.h"
#include "dispatch/strings.h"
#include "dispatch/randomized.h"
#include "dispatch/spatial.h"
#include "dispatch/misc.h"
#include "dispatch/sim.h"

#endif
