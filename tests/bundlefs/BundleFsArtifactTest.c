/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* End to end: build an artifact out of the glibc in this tree and run it.
 *
 * The routing this proves lives in libc, but none of it can be reached from an ordinary program. A
 * process only has a bundle filesystem because the loader opened an artifact and __libc_early_init
 * mounted what the view note described -- so a test that does not go through the whole chain tests
 * nothing, however carefully it calls open.
 *
 * Every bug worth finding so far has been in the seams: a routine the loader pulls out of libc_pic.a,
 * a generated stub quietly winning over a C file, a stale library that made a change appear to work.
 * A unit test around one function would have caught none of them.
 *
 * The bundler is not built here and is not copied in. Its path is given, and missing it is a refusal
 * rather than a search: a stale copy of a build tool produces an artifact that passes while testing
 * something else, which is the same failure as a stale libc and just as quiet.
 *
 * Usage:
 *   bundlefs-artifact-test --bundler P --staged P --source P --scratch P --compiler P
 *
 *   --bundler   bundler-glibc, from the minst tree
 *   --staged    a DESTDIR install of the glibc under test
 *   --source    this directory, holding the manifests and the tree to carry
 *   --scratch   somewhere to build
 *   --compiler  a compiler for the payload
 */

#define _GNU_SOURCE

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static int failures;

static void Check(bool passed, const char* what)
{
    printf("%s %s\n", passed ? "ok  " : "FAIL", what);

    if (!passed)
    {
        ++failures;
    }
}

/* Runs a command, collecting both streams. Failure to start is a result like any other, so a check
   that expected output still gets an answer rather than the test disappearing underneath it. */
static int Run(char* output, size_t length, const char* format, ...)
{
    char command[8192];

    va_list arguments;
    va_start(arguments, format);
    vsnprintf(command, sizeof command, format, arguments);
    va_end(arguments);

    strncat(command, " 2>&1", sizeof command - strlen(command) - 1);

    if (output != NULL)
    {
        output[0] = '\0';
    }

    FILE* pipe = popen(command, "r");
    if (pipe == NULL)
    {
        return -1;
    }

    char buffer[4096];
    size_t used = 0;

    while (fgets(buffer, sizeof buffer, pipe) != NULL)
    {
        if (output == NULL)
        {
            continue;
        }

        size_t chunk = strlen(buffer);

        if (used + chunk < length)
        {
            memcpy(output + used, buffer, chunk + 1);
            used += chunk;
        }
    }

    int status = pclose(pipe);

    return status < 0 ? -1 : WEXITSTATUS(status);
}

static void Show(const char* output)
{
    for (const char* line = output; line != NULL && *line != '\0'; )
    {
        const char* end = strchr(line, '\n');
        int span = end == NULL ? (int) strlen(line) : (int) (end - line);

        printf("     | %.*s\n", span, line);

        line = end == NULL ? NULL : end + 1;
    }
}

/* The second artifact: a carried object whose DT_NEEDED reaches the machine, and a machine library
   that reaches further.

   Three shared objects and a chain between them. The leaf and the middle are built into the scratch
   directory, so the artifact does not carry those names and the placement rule sends them next door.
   The plugin is carried, names the middle, and is found by the payload at its path inside the image.

   Nothing off the machine is required to build any of it. Depending on a real system library would
   make the test's subject -- how deep the walk goes -- a property of whatever that library happened
   to need on the machine running it.

   RUNPATH rather than a search path in the environment: the loader resolves a host name using the
   RUNPATH of the object that asked, which here is the carried plugin, and for the leaf it is the
   middle library that asks. Both get one. */
static void RunHostArtifact(const char* bundler, const char* source, const char* scratch,
    const char* compiler)
{
    char output[65536];

    Run(NULL, 0, "rm -rf %s/carried %s/hostlibs", scratch, scratch);
    Run(NULL, 0, "mkdir -p %s/carried %s/hostlibs", scratch, scratch);

    int leaf = Run(output, sizeof output,
        "%s -O0 -g -shared -fPIC -Wl,-soname,libminst-host-leaf.so"
        " -o %s/hostlibs/libminst-host-leaf.so %s/HostLeaf.c",
        compiler, scratch, source);

    int middle = Run(output, sizeof output,
        "%s -O0 -g -shared -fPIC -Wl,-soname,libminst-host-middle.so"
        " -o %s/hostlibs/libminst-host-middle.so %s/HostMiddle.c"
        " -L%s/hostlibs -lminst-host-leaf -Wl,-rpath,%s/hostlibs",
        compiler, scratch, source, scratch, scratch);

    int plugin = Run(output, sizeof output,
        "%s -O0 -g -shared -fPIC -Wl,-soname,libminst-carried-plugin.so"
        " -o %s/carried/libminst-carried-plugin.so %s/CarriedPlugin.c"
        " -L%s/hostlibs -lminst-host-middle -Wl,-rpath,%s/hostlibs",
        compiler, scratch, source, scratch, scratch);

    int payload = Run(output, sizeof output,
        "%s -O0 -g -o %s/host-payload %s/HostPayload.c", compiler, scratch, source);

    Check(leaf == 0 && middle == 0 && plugin == 0 && payload == 0,
        "the host dependency chain and its payload compile");

    if (leaf != 0 || middle != 0 || plugin != 0 || payload != 0)
    {
        Show(output);
        return;
    }

    int artifact = Run(output, sizeof output,
        "%s %s/host.xml -DBUILD=%s --output %s/host-artifact",
        bundler, source, scratch, scratch);

    Check(artifact == 0, "an unsealed artifact builds, carrying the plugin and not its dependencies");

    if (artifact != 0)
    {
        Show(output);
        return;
    }

    Run(NULL, 0, "chmod +x %s/host-artifact", scratch);

    /* The plugin must be reachable only from inside, and its dependencies only from outside. Either
       one failing the other way would let the artifact pass while proving nothing about the fence. */
    int carried_outside = Run(NULL, 0, "test -e /minst/lib/libminst-carried-plugin.so");
    Check(carried_outside != 0, "the carried plugin does not exist on this machine");

    int host_present = Run(NULL, 0, "test -e %s/hostlibs/libminst-host-leaf.so", scratch);
    Check(host_present == 0, "and the host libraries it reaches are on the machine, not in the image");

    int ran = Run(output, sizeof output, "%s/host-artifact", scratch);

    Check(ran == 0, "the unsealed artifact runs and every check inside it passes");

    if (ran != 0 || strstr(output, "FAIL") != NULL)
    {
        Show(output);
    }
}

/* The member set an artifact carries: every shared object the staged tree installed, the converters
   beside them. Assembled rather than pointed at, because the bundler wants one directory and an
   install has them spread across lib64 and usr/lib64. */
static bool AssembleMembers(const char* staged, const char* scratch)
{
    char output[65536];

    Run(NULL, 0, "rm -rf %s/members", scratch);

    if (Run(output, sizeof output, "mkdir -p %s/members/gconv", scratch) != 0)
    {
        Check(false, "make a place for the member set");
        Show(output);
        return false;
    }

    int copied = Run(output, sizeof output, "cp %s/lib64/*.so* %s/members/", staged, scratch);
    if (copied != 0)
    {
        Check(false, "collect the shared objects from the staged install");
        Show(output);
        return false;
    }

    /* The converters are opened by path at run time, so they travel whole. */
    Run(NULL, 0, "cp %s/usr/lib64/gconv/gconv-modules %s/members/gconv/ 2>/dev/null", staged, scratch);
    Run(NULL, 0, "cp %s/usr/lib64/gconv/*.so %s/members/gconv/ 2>/dev/null", staged, scratch);

    return true;
}

int main(int argc, char** argv)
{
    const char* bundler = NULL;
    const char* staged = NULL;
    const char* source = NULL;
    const char* scratch = NULL;
    const char* compiler = NULL;

    for (int i = 1; i + 1 < argc; i += 2)
    {
        if (strcmp(argv[i], "--bundler") == 0)
        {
            bundler = argv[i + 1];
        }
        else if (strcmp(argv[i], "--staged") == 0)
        {
            staged = argv[i + 1];
        }
        else if (strcmp(argv[i], "--source") == 0)
        {
            source = argv[i + 1];
        }
        else if (strcmp(argv[i], "--scratch") == 0)
        {
            scratch = argv[i + 1];
        }
        else if (strcmp(argv[i], "--compiler") == 0)
        {
            compiler = argv[i + 1];
        }
    }

    if (bundler == NULL || staged == NULL || source == NULL || scratch == NULL || compiler == NULL)
    {
        fprintf(stderr,
            "usage: bundlefs-artifact-test --bundler P --staged P --source P --scratch P"
            " --compiler P\n");
        return 2;
    }

    char output[65536];

    Run(NULL, 0, "mkdir -p %s", scratch);

    /* The payload is built here rather than carried, so it is always against the headers of the tree
       it is testing. */
    int built = Run(output, sizeof output, "%s -O0 -g -o %s/payload %s/Payload.c",
        compiler, scratch, source);
    Check(built == 0, "the payload compiles");

    if (built != 0)
    {
        Show(output);
        return 1;
    }

    if (!AssembleMembers(staged, scratch))
    {
        return 1;
    }

    int runtime = Run(output, sizeof output,
        "%s %s/runtime.xml -DMEMBERS=%s/members --output %s/runtime.bundle",
        bundler, source, scratch, scratch);
    Check(runtime == 0, "the glibc in this tree bundles as a runtime");

    if (runtime != 0)
    {
        Show(output);
        return 1;
    }

    int artifact = Run(output, sizeof output,
        "%s %s/artifact.xml -DBUILD=%s -DSOURCE=%s --output %s/artifact",
        bundler, source, scratch, source, scratch);
    Check(artifact == 0, "an artifact builds from it");

    if (artifact != 0)
    {
        Show(output);
        return 1;
    }

    Run(NULL, 0, "chmod +x %s/artifact", scratch);

    /* Before running it: the same paths must be absent from the machine, or nothing below proves
       anything at all. */
    int present = Run(NULL, 0, "test -e /minst/carried.txt");
    Check(present != 0, "the carried paths do not exist on this machine");

    /* What the machine says before the artifact says anything, so the payload can tell what the
       manifest did from what was already there: one variable for overwrite="false" to leave alone,
       one for a plain set to replace, one for unset to remove, and four lists for the list forms to
       act on. Passed on the command that starts it rather than set here, since Run goes through a
       shell and this keeps the two statements in one place. */
    int ran = Run(output, sizeof output,
        "MINST_TEST_EXISTING=machine MINST_TEST_REPLACED=machine MINST_TEST_UNSET=machine"
        " MINST_TEST_LIST=/middle:/kept:/last MINST_TEST_EMPTIED=/only"
        " MINST_TEST_KEEP=/a:/b:/c MINST_TEST_ADD=/a:/b"
        " %s/artifact %s", scratch, scratch);

    Check(ran == 0, "the artifact runs and every check inside it passes");

    if (ran != 0 || strstr(output, "FAIL") != NULL)
    {
        Show(output);
    }
    else
    {
        /* The tail is the count, which is worth seeing even when everything passed. */
        const char* last = strrchr(output, '\n');

        if (last != NULL && last != output)
        {
            const char* before = last - 1;

            while (before > output && *before != '\n')
            {
                --before;
            }

            printf("     %.*s\n", (int) (last - before), before + (*before == '\n' ? 1 : 0));
        }
    }

    RunHostArtifact(bundler, source, scratch, compiler);

    if (failures != 0)
    {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }

    printf("\nall checks passed\n");

    return 0;
}
