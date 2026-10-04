// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "working_cache.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define MIB (UINT64_C(1024) * 1024)
#define PREFIX ".omaraw-work-"
typedef struct buffer {
    void *pixels;
    size_t bytes;
    int fd, directory, mapped;
    char name[80];
    struct buffer *next;
} buffer;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static char cache_directory[PATH_MAX];
static uint64_t memory_limit, working_limit, disk_limit, serial;
static buffer *buffers;
static oma_working_stats stats;
static void (*reclaim_previews)(const char *, uint64_t);

void oma_working_set_reclaimer(void (*reclaim)(const char *, uint64_t))
{
    pthread_mutex_lock(&mutex);
    reclaim_previews = reclaim;
    pthread_mutex_unlock(&mutex);
}

void oma_working_configure(const char *directory, uint64_t memory_bytes,
                           uint64_t working_bytes, uint64_t disk_bytes)
{
    pthread_mutex_lock(&mutex);
    snprintf(cache_directory, sizeof(cache_directory), "%s", directory ? directory : "");
    memory_limit = memory_bytes;
    working_limit = working_bytes;
    disk_limit = disk_bytes;
    pthread_mutex_unlock(&mutex);
}

static uint64_t resident_bytes(void)
{
    unsigned long pages = 0, ignored;
    FILE *file = fopen("/proc/self/statm", "re");
    if(file) { if(fscanf(file, "%lu %lu", &ignored, &pages) != 2) pages = 0; fclose(file); }
    return (uint64_t)pages * (uint64_t)sysconf(_SC_PAGESIZE);
}

static uint64_t available_bytes(void)
{
    FILE *file = fopen("/proc/meminfo", "re");
    if(!file) return UINT64_MAX;
    char line[256];
    unsigned long long kib;
    uint64_t available = UINT64_MAX;
    while(fgets(line, sizeof(line), file))
        if(sscanf(line, "MemAvailable: %llu kB", &kib) == 1) { available = kib * 1024; break; }
    fclose(file);
    return available;
}

// The directory lock serializes reservations across app/CLI processes. Each
// working file also holds a lifetime lock, so a crash is reclaimed next time
// without risking another process's live pixels. Never follow symlinks.
static uint64_t scan(int directory, int working_only)
{
    const int copy = openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *dir = copy < 0 ? NULL : fdopendir(copy);
    if(!dir) { if(copy >= 0) close(copy); return UINT64_MAX; }
    uint64_t bytes = 0;
    struct dirent *entry;
    while((entry = readdir(dir)))
    {
        if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        struct stat st;
        if(fstatat(directory, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) || !S_ISREG(st.st_mode)) continue;
        const int working = !strncmp(entry->d_name, PREFIX, strlen(PREFIX));
        if(working)
        {
            const int fd = openat(directory, entry->d_name, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
            if(fd >= 0)
            {
                if(flock(fd, LOCK_EX | LOCK_NB) == 0)
                {
                    struct stat opened;
                    if(!fstat(fd, &opened) && opened.st_ino == st.st_ino && opened.st_dev == st.st_dev
                       && !unlinkat(directory, entry->d_name, 0)) st.st_size = 0;
                }
                close(fd);
            }
        }
        if((!working_only || working) && st.st_size > 0)
            bytes = (uint64_t)st.st_size > UINT64_MAX - bytes ? UINT64_MAX : bytes + st.st_size;
    }
    closedir(dir);
    return bytes;
}

static int lock_directory(int directory)
{
    int fd = openat(directory, ".omaraw-working.lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if(fd >= 0 && flock(fd, LOCK_EX)) { close(fd); fd = -1; }
    return fd;
}

uint64_t oma_working_disk_usage(const char *directory)
{
    int dir = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(dir < 0) return 0;
    int lock = lock_directory(dir);
    uint64_t bytes = lock < 0 ? 0 : scan(dir, 1);
    if(lock >= 0) close(lock);
    close(dir);
    return bytes == UINT64_MAX ? 0 : bytes;
}

static int map_buffer(buffer *b)
{
    int dir = open(cache_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(dir < 0) return 0;
    int lock = lock_directory(dir);
    if(lock < 0) { close(dir); return 0; }
    struct statfs fs;
    struct statvfs space;
    int ok = 0;
    // tmpfs/ramfs would consume RAM again. Reserve real extents before mapping:
    // a sparse ftruncate alone can SIGBUS later when the disk is full.
    if(fstatfs(dir, &fs) || fs.f_type == 0x01021994 || fs.f_type == 0x858458f6
       || fstatvfs(dir, &space)) goto done;
    uint64_t free_bytes = (uint64_t)space.f_bavail * space.f_frsize;
    uint64_t used = scan(dir, 0);
    if(reclaim_previews && (used > disk_limit || b->bytes > disk_limit - used
                           || free_bytes < b->bytes + 512 * MIB))
    {
        const uint64_t working = scan(dir, 1);
        if(working > disk_limit || b->bytes > disk_limit - working) goto done;
        uint64_t preview_limit = disk_limit - working - b->bytes;
        if(free_bytes < b->bytes + 512 * MIB)
        {
            const uint64_t need = b->bytes + 512 * MIB - free_bytes;
            const uint64_t previews = used > working ? used - working : 0;
            const uint64_t target = previews > need ? previews - need : 0;
            if(target < preview_limit) preview_limit = target;
        }
        reclaim_previews(cache_directory, preview_limit);
        used = scan(dir, 0);
        if(fstatvfs(dir, &space)) goto done;
        free_bytes = (uint64_t)space.f_bavail * space.f_frsize;
    }
    if(free_bytes < 512 * MIB || b->bytes > free_bytes - 512 * MIB) goto done;
    if(used > disk_limit || b->bytes > disk_limit - used) goto done;
    for(int attempt = 0; attempt < 16; ++attempt)
    {
        snprintf(b->name, sizeof(b->name), PREFIX "%ld-%llu", (long)getpid(), (unsigned long long)++serial);
        b->fd = openat(dir, b->name, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if(b->fd >= 0 || errno != EEXIST) break;
    }
    if(b->fd < 0) goto done;
    if(flock(b->fd, LOCK_EX | LOCK_NB) || posix_fallocate(b->fd, 0, b->bytes)) goto remove;
    b->pixels = mmap(NULL, b->bytes, PROT_READ | PROT_WRITE, MAP_SHARED, b->fd, 0);
    if(b->pixels == MAP_FAILED) { b->pixels = NULL; goto remove; }
    b->directory = dir;
    b->mapped = 1;
    stats.disk_bytes += b->bytes;
    if(stats.disk_bytes > stats.peak_disk_bytes) stats.peak_disk_bytes = stats.disk_bytes;
    ++stats.backed_allocations;
    ok = 1;
    goto done;
remove:
    unlinkat(dir, b->name, 0);
    close(b->fd);
    b->fd = -1;
done:
    close(lock);
    if(!ok) close(dir);
    return ok;
}

void *oma_working_alloc(size_t bytes)
{
    if(!bytes) return NULL;
    buffer *b = calloc(1, sizeof(*b));
    if(!b) return NULL;
    b->bytes = bytes;
    b->fd = b->directory = -1;
    pthread_mutex_lock(&mutex);
    const int large = bytes >= 8 * MIB;
    const int pressure = large && memory_limit
        && (bytes > working_limit || stats.ram_bytes > working_limit - bytes
            || resident_bytes() + bytes > memory_limit
            || available_bytes() < bytes + 512 * MIB);
    if(!pressure)
    {
        if(large)
        {
            b->pixels = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if(b->pixels == MAP_FAILED) b->pixels = NULL;
            else b->mapped = 1;
        }
        else if(posix_memalign(&b->pixels, 64, bytes)) b->pixels = NULL;
        if(b->pixels) stats.ram_bytes += bytes;
    }
    if(!b->pixels && !(large && disk_limit && map_buffer(b)))
    {
        ++stats.failures;
        fprintf(stderr, "[OmaRaw working cache] Cannot reserve %zu bytes within RAM/disk limits\n", bytes);
        free(b);
        b = NULL;
    }
    if(b) { b->next = buffers; buffers = b; }
    pthread_mutex_unlock(&mutex);
    return b ? b->pixels : NULL;
}

static void discard_backing(buffer *b)
{
    munmap(b->pixels, b->bytes);
    unlinkat(b->directory, b->name, 0);
    close(b->fd);
    close(b->directory);
    stats.disk_bytes -= b->bytes;
}

static int back_existing(const void *pixels, size_t preserve)
{
    buffer *b = buffers;
    while(b && b->pixels != pixels) b = b->next;
    if(!b || !b->mapped || b->fd >= 0) return 1;
    buffer disk = { .bytes = b->bytes, .fd = -1, .directory = -1 };
    if(!disk_limit || !map_buffer(&disk)) return 0;
    if(preserve > b->bytes) preserve = b->bytes;
    // Copy into the file's reclaimable page cache in bounded writes. Do not
    // allocate another anonymous full image. Unused output needs no copy.
    size_t at = 0;
    while(at < preserve)
    {
        const size_t chunk = preserve - at > 16 * MIB ? 16 * MIB : preserve - at;
        const ssize_t written = pwrite(disk.fd, (const char *)pixels + at, chunk, at);
        if(written < 0 && errno == EINTR) continue;
        if(written <= 0) { discard_backing(&disk); return 0; }
        at += written;
    }
    // This range is already reserved by our anonymous mmap, never by malloc.
    // The engine has joined its processing workers at this stage boundary.
    // MAP_FIXED therefore replaces only our own inactive buffer. On failure
    // the caller invalidates the pipe and aborts before using these pixels.
    // See mmap(2), "Using MAP_FIXED safely".
    void *mapped = mmap(b->pixels, b->bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, disk.fd, 0);
    if(mapped == MAP_FAILED) { discard_backing(&disk); return 0; }
    munmap(disk.pixels, disk.bytes);
    b->fd = disk.fd;
    b->directory = disk.directory;
    memcpy(b->name, disk.name, sizeof(b->name));
    stats.ram_bytes -= b->bytes;
    ++stats.migrations;
    return 1;
}

int oma_working_prepare(const void *input, size_t input_bytes, void *output, size_t anticipated_bytes)
{
    pthread_mutex_lock(&mutex);
    const int pressure = memory_limit
        && (stats.ram_bytes > working_limit || anticipated_bytes > memory_limit
            || resident_bytes() > memory_limit - anticipated_bytes
            || available_bytes() < anticipated_bytes + 512 * MIB);
    int ok = 1;
    if(pressure)
    {
        ok = back_existing(input, input_bytes) && back_existing(output, input == output ? input_bytes : 0);
        if(!ok)
        {
            ++stats.failures;
            fprintf(stderr, "[OmaRaw working cache] Cannot back processing buffers within the disk limit\n");
        }
    }
    pthread_mutex_unlock(&mutex);
    return ok;
}

void oma_working_free(void *pixels)
{
    if(!pixels) return;
    pthread_mutex_lock(&mutex);
    buffer **link = &buffers;
    while(*link && (*link)->pixels != pixels) link = &(*link)->next;
    buffer *b = *link;
    if(b)
    {
        *link = b->next;
        if(b->fd >= 0)
        {
            discard_backing(b);
        }
        else { if(b->mapped) munmap(pixels, b->bytes); else free(pixels); stats.ram_bytes -= b->bytes; }
        free(b);
    }
    // Buffers made before the bridge installed its callbacks use dt's normal
    // aligned allocator (posix_memalign on this platform).
    else free(pixels);
    pthread_mutex_unlock(&mutex);
}

oma_working_stats oma_working_get_stats(void)
{
    pthread_mutex_lock(&mutex);
    const oma_working_stats result = stats;
    pthread_mutex_unlock(&mutex);
    return result;
}
