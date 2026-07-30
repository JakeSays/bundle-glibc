/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* A shared object the artifact carries, whose dependencies come off the machine.
 *
 * The shape that found three loader defects at once, and the only shape that reaches it. Everything
 * else an artifact loads from the machine gets there through dlopen from inside a bundled library,
 * which goes through dl_open_worker and walks the whole closure. A carried object naming a host
 * library by DT_NEEDED is a different path: the fence maps the named library and returns, and until
 * the finishing passes run, nothing walks what that library needs, gives it thread-local storage, or
 * relocates it.
 *
 * Opened by the payload with dlopen and by its carried path, because that is what a plugin is. Qt's
 * GTK platform theme is the real instance; this is the same thing with nothing else attached.  */

extern int MinstHostMiddle(void);
extern void* MinstHostMiddleLeafAddress(void);

int MinstCarriedPlugin(void)
{
    return MinstHostMiddle() + 1;
}

/* Passed straight through, so the payload can compare what the far side of the chain resolved to
   against what it resolves to itself.  */
void* MinstCarriedPluginLeafAddress(void)
{
    return MinstHostMiddleLeafAddress();
}
