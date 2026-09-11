/* Real functions for any `#pragma weak ALIAS = TARGET` pairs the game uses
 * (the pragma is IDO-only and the port build neutralises it). The sequel is
 * built with KMC GCC 2.7.2 and uses none in its own sources; libultra's
 * gu/sinf.c and gu/cosf.c alias sinf/fsin and cosf/fcos onto __sinf/__cosf,
 * and the host's libm supplies sinf/cosf, so only the f-named pair is left.
 * Compiled with the game's flags so the game headers apply. */
#include "common.h"

extern f32 __sinf(f32);
extern f32 __cosf(f32);

f32 fsin(f32 x) {
    return __sinf(x);
}

f32 fcos(f32 x) {
    return __cosf(x);
}
