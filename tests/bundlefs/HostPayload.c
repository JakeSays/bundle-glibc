/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* The program of the unsealed artifact: whether a carried object can reach the machine, and whether
 * what it reaches can reach further.
 *
 * Separate from Payload.c because the two artifacts contradict each other. That one is sealed and
 * checks that nothing resolves off the machine; this one is not, and checks that the right things do.
 * One artifact cannot answer both.
 *
 * Each check prints one line beginning "ok" or "FAIL", and the last line is a count, which is the
 * same contract the driver already reads.  */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

static void Check(int passed, const char* what)
{
    printf("%s %s\n", passed ? "ok  " : "FAIL", what);

    ++checks;

    if (!passed)
    {
        ++failures;
    }
}

/* 3 * 7 + 1, computed one function at a time across three objects: the leaf is a host library nothing
   carried names, the middle is a host library the carried object names, and the sum is returned by
   the carried object itself. A wrong answer means one of them did not run; no answer at all means one
   of them did not load. */
#define Expected 22

int main(void)
{
    /* By its carried path, the way a plugin is opened. The path exists in the artifact and nowhere on
       the machine, so a handle at all is the image answering. */
    void* plugin = dlopen("/minst/lib/libminst-carried-plugin.so", RTLD_NOW);

    Check(plugin != NULL, "a carried object opens by its path");

    if (plugin == NULL)
    {
        printf("     | %s\n", dlerror());
        printf("%d check(s), %d failure(s)\n", checks, failures);
        return 1;
    }

    /* RTLD_NOW above already required every relocation to resolve, which is most of the point: a
       dependency that never arrived is an unresolved symbol here rather than a fault later. */
    int (*entry)(void) = (int (*)(void)) dlsym(plugin, "MinstCarriedPlugin");

    Check(entry != NULL, "  and its entry point is there");

    if (entry != NULL)
    {
        int answer = entry();

        Check(answer == Expected,
            "  and calling it runs through both levels of host dependency");

        if (answer != Expected)
        {
            printf("     | expected %d, got %d\n", Expected, answer);
        }
    }

    /* Which of them is actually loaded, asked the only way that answers it.
       RTLD_NOLOAD returns a handle if the name is already open and NULL if it is not, so it
       distinguishes "loaded" from "loadable" -- and it crosses the fence, because a name the artifact
       does not carry is resolved in the host namespace whoever asks.

       The middle library is what the carried object named, so it was always found. The leaf is what
       the middle named, and that is the whole subject: it is reached only by walking a host object's
       own DT_NEEDED, which is the walk that used to stop. */
    void* middle = dlopen("libminst-host-middle.so", RTLD_NOW | RTLD_NOLOAD);

    Check(middle != NULL, "the host library the carried object names is already loaded");

    if (middle != NULL)
    {
        dlclose(middle);
    }

    /* The leaf, which nothing in this namespace ever named, so RTLD_NOLOAD would answer "not open
       here" however loaded it is -- there is no proxy standing for it on this side, and that is the
       right answer to the question NOLOAD asks. Opening it outright instead, and then asking whether
       the copy that arrives is the copy the chain already resolved against.

       That is the check worth making. Two copies of a host library would both load and both answer,
       and only the address distinguishes one shared library from two private ones. */
    void* leaf = dlopen("libminst-host-leaf.so", RTLD_NOW);

    Check(leaf != NULL, "  and the one that library names in turn opens by name");

    void* (*through_chain)(void) = (void* (*)(void)) dlsym(plugin, "MinstCarriedPluginLeafAddress");

    if (leaf != NULL && through_chain != NULL)
    {
        void* direct = dlsym(leaf, "MinstHostLeaf");

        Check(direct != NULL && direct == through_chain(),
            "  and it is the same copy the chain already resolved against");
    }
    else
    {
        Check(0, "  and it is the same copy the chain already resolved against");
    }

    if (leaf != NULL)
    {
        dlclose(leaf);
    }

    /* And they are loaded somewhere else, which is the point of putting them there.
       A host library resolves against other host libraries, in a namespace of its own; its symbols
       are not in the payload's. RTLD_DEFAULT searches the caller's namespace, so a definition that is
       demonstrably loaded is still not visible here -- and if it were, the fence would not be one. */
    Check(dlsym(RTLD_DEFAULT, "MinstHostMiddle") == NULL,
        "  and their symbols stay out of the payload's namespace");

    dlclose(plugin);

    printf("%d check(s), %d failure(s)\n", checks, failures);

    return failures == 0 ? 0 : 1;
}
