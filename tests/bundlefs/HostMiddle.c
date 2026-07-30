/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* One level of host dependency, standing between the carried object and HostLeaf.
 *
 * This one the loader always found: it is named directly by a carried object's DT_NEEDED, and one
 * level is exactly how far the walk used to get. What it needs in turn did not arrive, so calling
 * through this function faulted -- or, sooner, the version check asserted on a DT_NEEDED with no map.
 *
 * Calling into the leaf rather than merely linking against it, so the check is that the chain works
 * and not just that it was mapped: an object can be loaded, left unrelocated, and still look present
 * to anything that only counts what is in the namespace.  */

extern int MinstHostLeaf(void);

int MinstHostMiddle(void)
{
    return MinstHostLeaf() * 7;
}

/* Where this side thinks the leaf is, so the other side can check it agrees.
   Two copies of a host library would both work and both answer 3; only the address says whether the
   fence shared one or duplicated it.  */
void* MinstHostMiddleLeafAddress(void)
{
    return (void*) MinstHostLeaf;
}
