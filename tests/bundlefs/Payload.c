/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* The program an artifact carries, exercising every path that libc routes through the bundle
 * filesystem, and what the manifest says it should be told.
 *
 * Nothing here knows bundlefs exists. It calls open, read, mmap, dup and the rest, against paths no
 * machine has -- so an answer at all proves the artifact served it rather than the machine happening
 * to hold something of the same name.
 *
 * Each check prints one line beginning "ok" or "FAIL", and the last line is a count. The driver reads
 * that rather than matching prose, so a check can be reworded without touching it.  */

#define _GNU_SOURCE

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

static const char* CarriedFile = "/minst/carried.txt";
static const char* CarriedTree = "/minst/tree";

/* The first bytes of CarriedFile, which several checks slice differently. */
static const char* Contents = "this text exists only inside the artifact";

static int failures;
static int checks;

static void Check(int passed, const char* what)
{
    printf("%s %s\n", passed ? "ok  " : "FAIL", what);

    ++checks;

    if (!passed)
    {
        ++failures;
    }
}

/* Whether an address is mapped from the artifact itself rather than from a copy. Read from
   /proc/self/maps and compared against /proc/self/exe, so the answer does not depend on where the
   artifact was built or what it is called. */
static int MappedFromArtifact(const void* address)
{
    char self[4096] = { 0 };
    if (readlink("/proc/self/exe", self, sizeof self - 1) <= 0)
    {
        return 0;
    }

    FILE* maps = fopen("/proc/self/maps", "r");
    if (maps == NULL)
    {
        return 0;
    }

    char line[8192];
    int matched = 0;

    while (fgets(line, sizeof line, maps) != NULL)
    {
        unsigned long long from = 0;
        unsigned long long to = 0;

        if (sscanf(line, "%llx-%llx", &from, &to) != 2)
        {
            continue;
        }

        if ((unsigned long long) (uintptr_t) address < from
            || (unsigned long long) (uintptr_t) address >= to)
        {
            continue;
        }

        matched = strstr(line, self) != NULL;
        break;
    }

    fclose(maps);

    return matched;
}

static void CheckReading(void)
{
    FILE* carried = fopen(CarriedFile, "rb");
    if (carried == NULL)
    {
        Check(0, "fopen a carried path");
        return;
    }

    char line[128] = { 0 };
    int got = fgets(line, sizeof line, carried) != NULL;

    fclose(carried);

    Check(got && strcmp(line, Contents) == 0, "fopen and fgets a carried path");
}

static void CheckMetadata(void)
{
    struct stat described;
    int ok = stat(CarriedFile, &described) == 0;

    Check(ok && S_ISREG(described.st_mode) && described.st_size == (off_t) strlen(Contents),
        "stat reports the size and kind");

    Check(access(CarriedFile, R_OK) == 0, "access says readable");
    Check(access(CarriedFile, W_OK) != 0, "access says not writable");

    struct statx extended;
    int wide = statx(AT_FDCWD, CarriedFile, 0, STATX_BASIC_STATS, &extended) == 0;

    Check(wide && (extended.stx_mode & S_IFMT) == S_IFREG
        && extended.stx_size == strlen(Contents), "statx reports the size and kind");
}

static void CheckDirectories(void)
{
    struct stat described;
    Check(stat(CarriedTree, &described) == 0 && S_ISDIR(described.st_mode),
        "stat reports a carried directory");

    DIR* directory = opendir(CarriedTree);
    if (directory == NULL)
    {
        Check(0, "opendir a carried directory");
        return;
    }

    int count = 0;
    int sawSubdirectory = 0;
    struct dirent* entry;

    while ((entry = readdir(directory)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        if (entry->d_type == DT_DIR)
        {
            sawSubdirectory = 1;
        }

        ++count;
    }

    closedir(directory);

    Check(count == 4, "readdir returns every entry");
    Check(sawSubdirectory, "readdir reports a subdirectory as one");

    DIR* nested = opendir("/minst/tree/sub");
    Check(nested != NULL, "opendir below the mount point");

    if (nested != NULL)
    {
        closedir(nested);
    }

    /* A directory as a plain descriptor, which is how openat and fdopendir reach one. */
    int descriptor = open(CarriedTree, O_RDONLY | O_DIRECTORY);
    Check(descriptor >= 0, "open a carried directory with O_DIRECTORY");

    if (descriptor < 0)
    {
        return;
    }

    DIR* fromDescriptor = fdopendir(descriptor);
    if (fromDescriptor == NULL)
    {
        Check(0, "fdopendir a carried directory descriptor");
        close(descriptor);
        return;
    }

    int again = 0;

    while ((entry = readdir(fromDescriptor)) != NULL)
    {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {
            ++again;
        }
    }

    closedir(fromDescriptor);

    Check(again == 4, "fdopendir returns every entry");
}

static void CheckRelativePaths(void)
{
    int tree = open(CarriedTree, O_RDONLY | O_DIRECTORY);
    if (tree < 0)
    {
        Check(0, "open the directory to resolve against");
        return;
    }

    /* The kernel cannot answer any of these: the number it holds is an anonymous file with no name
       in any tree. */
    int relative = openat(tree, "a.txt", O_RDONLY);
    Check(relative >= 0, "openat resolves against a carried directory");

    if (relative >= 0)
    {
        char head[16] = { 0 };
        Check(read(relative, head, sizeof head - 1) > 0 && strncmp(head, "top level", 9) == 0,
            "  and the file it opened is the right one");
        close(relative);
    }

    struct stat described;
    Check(fstatat(tree, "b.md", &described, 0) == 0, "fstatat resolves against a carried directory");
    Check(faccessat(tree, "b.md", R_OK, 0) == 0, "faccessat resolves against a carried directory");

    char target[64] = { 0 };
    Check(readlinkat(tree, "link.txt", target, sizeof target - 1) > 0
        && strcmp(target, "a.txt") == 0, "readlinkat resolves against a carried directory");

    close(tree);

    int direct = openat(AT_FDCWD, CarriedFile, O_RDONLY);
    Check(direct >= 0, "openat with AT_FDCWD");

    if (direct >= 0)
    {
        close(direct);
    }

    char linked[64] = { 0 };
    Check(readlink("/minst/tree/link.txt", linked, sizeof linked - 1) > 0
        && strcmp(linked, "a.txt") == 0, "readlink returns the target unfollowed");
}

static void CheckMapping(void)
{
    int descriptor = open(CarriedFile, O_RDONLY);
    if (descriptor < 0)
    {
        Check(0, "open for mapping");
        return;
    }

    size_t length = strlen(Contents);
    char* view = mmap(NULL, length, PROT_READ, MAP_PRIVATE, descriptor, 0);

    /* Closed before the mapping is read. POSIX says the mapping outlives the descriptor, and this is
       where an arrangement that hung the content off the descriptor would come apart. */
    close(descriptor);

    if (view == MAP_FAILED)
    {
        Check(0, "mmap a carried file");
        return;
    }

    Check(memcmp(view, Contents, length) == 0, "mmap a carried file, read after closing it");
    Check(MappedFromArtifact(view), "  and it is mapped from the artifact rather than a copy");
    Check(view[length] == '\0', "  and the padding past the content reads as zero");

    munmap(view, length);

    /* Past the blocks the content occupies. Mapping the artifact there would hand over the next
       file, so this one has to fall back to a copy. */
    descriptor = open(CarriedFile, O_RDONLY);
    if (descriptor < 0)
    {
        Check(0, "reopen for the oversized mapping");
        return;
    }

    char* over = mmap(NULL, 8192, PROT_READ, MAP_PRIVATE, descriptor, 0);
    close(descriptor);

    if (over == MAP_FAILED)
    {
        Check(0, "mmap past the content's blocks");
        return;
    }

    Check(memcmp(over, Contents, length) == 0, "mmap past the content's blocks still reads it");
    Check(!MappedFromArtifact(over), "  and that one is a copy, not the artifact");

    munmap(over, 8192);
}

static void CheckWritesRefused(void)
{
    int descriptor = open(CarriedFile, O_RDONLY);
    if (descriptor < 0)
    {
        Check(0, "open to check the write guards");
        return;
    }

    errno = 0;
    Check(write(descriptor, "clobber", 7) < 0 && errno == EBADF, "write is refused with EBADF");

    errno = 0;
    Check(ftruncate(descriptor, 0) < 0 && errno == EBADF, "ftruncate is refused with EBADF");

    int mode = fcntl(descriptor, F_GETFL);
    Check(mode >= 0 && (mode & O_ACCMODE) == O_RDONLY, "fcntl reports the mode it was opened with");

    /* Past libc entirely. Only the seal can refuse these, which is why the seal exists as well as
       the guard: an LD_PRELOAD or a second libc reaches the descriptor without coming through us. */
    errno = 0;
    Check(syscall(SYS_write, descriptor, "clobber", (size_t) 7) < 0 && errno == EPERM,
        "a raw write syscall is refused by the seal");

    errno = 0;
    Check(syscall(SYS_ftruncate, descriptor, (long) 4096) < 0 && errno == EPERM,
        "a raw ftruncate syscall is refused by the seal");

    void* shared = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
    Check(shared == MAP_FAILED, "a shared writable mapping is refused");

    if (shared != MAP_FAILED)
    {
        munmap(shared, 4096);
    }

    close(descriptor);
}

static void CheckDuplication(void)
{
    int descriptor = open(CarriedFile, O_RDONLY);
    if (descriptor < 0)
    {
        Check(0, "open to duplicate");
        return;
    }

    int copy = dup(descriptor);
    if (copy < 0)
    {
        Check(0, "dup a carried descriptor");
        close(descriptor);
        return;
    }

    /* Four bytes through each must be the file's first eight, not its first four twice. */
    char joined[9] = { 0 };
    int a = read(descriptor, joined, 4) == 4;
    int b = read(copy, joined + 4, 4) == 4;

    Check(a && b && strncmp(joined, Contents, 8) == 0, "dup shares the read position");

    /* Closing one must not take the file from the other. */
    close(descriptor);

    char more[5] = { 0 };
    Check(read(copy, more, 4) == 4 && strncmp(more, Contents + 8, 4) == 0,
        "  and closing the original leaves the copy working");

    close(copy);

    int one = open(CarriedFile, O_RDONLY);
    int two = open("/minst/tree/a.txt", O_RDONLY);

    if (one < 0 || two < 0)
    {
        Check(0, "open two to check dup2");
        return;
    }

    Check(dup2(one, two) >= 0, "dup2 onto a descriptor that is already carried");

    char over[8] = { 0 };
    Check(read(two, over, sizeof over - 1) > 0 && strncmp(over, Contents, 7) == 0,
        "  and it reads the file it was pointed at");

    Check(dup2(one, one) >= 0, "dup2 onto its own number is allowed");

    char self[8] = { 0 };
    Check(read(one, self, sizeof self - 1) > 0 && strncmp(self, Contents + 7, 7) == 0,
        "  and does not lose the position");

    close(one);
    close(two);
}

static void CheckCloseRange(void)
{
    int held[4];

    for (int i = 0; i < 4; ++i)
    {
        held[i] = open(CarriedFile, O_RDONLY);

        if (held[i] < 0)
        {
            Check(0, "open several to close as a range");
            return;
        }
    }

    if (close_range((unsigned) held[0], (unsigned) held[3], 0) != 0)
    {
        Check(0, "close_range closes a span of carried descriptors");
        return;
    }

    /* The kernel hands these numbers straight back. If the table kept the records, the reopened
       descriptor finds a dead one first and reads nothing. */
    int again = open(CarriedFile, O_RDONLY);
    if (again < 0)
    {
        Check(0, "reopen after close_range");
        return;
    }

    char after[8] = { 0 };
    int read_back = read(again, after, sizeof after - 1) > 0 && strncmp(after, Contents, 7) == 0;

    Check(again >= held[0] && again <= held[3], "close_range frees the numbers for reuse");
    Check(read_back, "  and a reused number reads the file, not an abandoned record");

    close(again);
}

static void CheckVectorAndPositional(void)
{
    int descriptor = open(CarriedFile, O_RDONLY);
    if (descriptor < 0)
    {
        Check(0, "open for pread and readv");
        return;
    }

    char away[8] = { 0 };
    Check(pread(descriptor, away, 4, 5) == 4 && strncmp(away, Contents + 5, 4) == 0,
        "pread reads at an offset");

    char first[5] = { 0 };
    char second[5] = { 0 };
    struct iovec vector[2];

    vector[0].iov_base = first;
    vector[0].iov_len = 4;
    vector[1].iov_base = second;
    vector[1].iov_len = 4;

    /* Proves pread left the position alone: this starts from the beginning. */
    char both[9] = { 0 };
    int got = readv(descriptor, vector, 2) == 8;

    memcpy(both, first, 4);
    memcpy(both + 4, second, 4);

    Check(got && strncmp(both, Contents, 8) == 0,
        "readv fills the buffers in order, and pread did not move the position");

    close(descriptor);
}

static void CheckSendfile(const char* scratch)
{
    char landing[4096];
    snprintf(landing, sizeof landing, "%s/sendfile.out", scratch);

    int source = open(CarriedFile, O_RDONLY);
    int sink = open(landing, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (source < 0 || sink < 0)
    {
        Check(0, "open for sendfile");
        return;
    }

    ssize_t sent = sendfile(sink, source, NULL, 64);

    close(sink);
    close(source);

    Check(sent == (ssize_t) strlen(Contents), "sendfile sends the whole file");

    /* Read back off the machine, so the bytes are proved to have crossed rather than reported to. */
    FILE* landed = fopen(landing, "rb");
    if (landed == NULL)
    {
        Check(0, "  and the bytes arrive in a real file");
        return;
    }

    char back[128] = { 0 };
    int read_back = fgets(back, sizeof back, landed) != NULL;

    fclose(landed);
    unlink(landing);

    Check(read_back && strcmp(back, Contents) == 0, "  and the bytes arrive in a real file");
}

/* The artifact is sealed unless its manifest asked for the machine, and this one did not. Sealing is
   about resolving shared objects and nothing else -- every check above did ordinary file input and
   output against the machine's paths and against carried ones, and none of it is affected. */
static void CheckSealing(void)
{
    void* host = dlopen("libz.so.1", RTLD_NOW);

    Check(host == NULL, "a sealed artifact refuses to load an object it does not carry");

    if (host != NULL)
    {
        dlclose(host);
    }

    /* Writing somewhere on the machine still works, which is the half sealing must not touch. */
    FILE* scratch = fopen("/dev/null", "w");
    Check(scratch != NULL, "  and ordinary host file writing still works");

    if (scratch != NULL)
    {
        fclose(scratch);
    }
}

/* The reader itself, reachable by name from inside the artifact. §6.1 of the format document: an
   application does its bundled input and output through this rather than through open, and gets the
   path space libc already mounted instead of building a second one over the same bytes.

   Found with dlsym rather than linked, because this program is compiled against the machine's libc
   and only meets the artifact's at run time. */
static void CheckReaderIsExported(void)
{
    void* (*processFileSystem)(void) = dlsym(RTLD_DEFAULT, "BfsProcessFileSystem");

    Check(processFileSystem != NULL, "the reader is exported from libc under its own name");

    if (processFileSystem == NULL)
    {
        return;
    }

    Check(processFileSystem() != NULL, "  and hands back the path space libc already mounted");

    Check(dlsym(RTLD_DEFAULT, "BfsFileOpen") != NULL
        && dlsym(RTLD_DEFAULT, "BfsFileExtent") != NULL
        && dlsym(RTLD_DEFAULT, "BfsDirectoryRead") != NULL, "  along with the rest of the surface");
}

/* What the manifest said the program should be told.
 *
 * The environment is the one thing an artifact cannot be given from outside without stopping being a
 * unit, so every action the manifest surface offers is exercised here rather than only the one Qt
 * needed. The driver sets MINST_TEST_EXISTING, MINST_TEST_LIST and MINST_TEST_EMPTIED before running
 * this, which is what the overwrite and list-shape cases are answering about. */
static void CheckEnvironment(void)
{
    const char* set = getenv("MINST_TEST_SET");
    Check(set != NULL && strcmp(set, "carried") == 0, "the manifest sets a variable");

    const char* existing = getenv("MINST_TEST_EXISTING");
    Check(existing != NULL && strcmp(existing, "machine") == 0,
        "  and overwrite=\"false\" leaves what the machine said");

    const char* replaced = getenv("MINST_TEST_REPLACED");
    Check(replaced != NULL && strcmp(replaced, "carried") == 0,
        "  while a plain set replaces it");

    Check(getenv("MINST_TEST_UNSET") == NULL, "unset removes a variable the machine supplied");

    /* Prepended, appended, one entry removed, and an entry already present moved to the front rather
       than repeated. */
    const char* list = getenv("MINST_TEST_LIST");
    Check(list != NULL && strcmp(list, "/last:/front:/kept:/back") == 0,
        "the list forms prepend, append, remove and move without repeating");

    /* overwrite="false" is about the entry, not the list: an entry already there keeps its place
       instead of moving to the end the action names, and one that is not there is still added. */
    const char* keep = getenv("MINST_TEST_KEEP");
    Check(keep != NULL && strcmp(keep, "/a:/b:/c") == 0,
        "  overwrite=\"false\" leaves an entry already in the list where it is");

    const char* added = getenv("MINST_TEST_ADD");
    Check(added != NULL && strcmp(added, "/new:/a:/b") == 0,
        "  and still adds one that is not there");

    const char* fresh = getenv("MINST_TEST_FRESH");
    Check(fresh != NULL && strcmp(fresh, "/only") == 0,
        "  appending to a variable that is not there leaves no stray separator");

    Check(getenv("MINST_TEST_EMPTIED") == NULL,
        "  and removing the last entry removes the variable");
}

int main(int argc, char** argv)
{
    /* Somewhere on the machine to send a file to, which is the one case that has to leave the
       image. */
    const char* scratch = argc > 1 ? argv[1] : "/tmp";

    CheckReading();
    CheckMetadata();
    CheckDirectories();
    CheckRelativePaths();
    CheckMapping();
    CheckWritesRefused();
    CheckDuplication();
    CheckCloseRange();
    CheckVectorAndPositional();
    CheckSendfile(scratch);
    CheckSealing();
    CheckReaderIsExported();
    CheckEnvironment();

    printf("%d check(s), %d failure(s)\n", checks, failures);

    return failures == 0 ? 0 : 1;
}
