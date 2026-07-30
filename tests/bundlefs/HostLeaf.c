/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* The far end of a host dependency chain: two levels below the artifact, and named by nothing the
 * artifact carries.
 *
 * Built into the scratch directory rather than into the image, which is what makes it a host library
 * as far as the loader is concerned -- the artifact does not carry the name, so the placement rule
 * sends it next door.
 *
 * Nothing reaches this except through HostMiddle, which is the point: it is only ever loaded because
 * something else's DT_NEEDED named it, and that is the level the loader used to stop at.  */

int MinstHostLeaf(void)
{
    return 3;
}
