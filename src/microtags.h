#ifndef MICROTAGS_H
#define MICROTAGS_H

#include <stdint.h>

#define MICROTAG_LEN	5			// longest tag, e.g. "DO-10"

// DEC's tag for each control store address, in flash; empty where the listing
// has no microword.
extern const char microTags[256][MICROTAG_LEN + 1];

#endif
