// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Thread-safe storage for engine pixelpipe buffers. Large buffers start in
// anonymous RAM and acquire disk backing under pressure. Small buffers remain
// in RAM. All allocations must be returned through oma_working_free.
void oma_working_configure(const char *directory, uint64_t memory_bytes,
                           uint64_t working_bytes, uint64_t disk_bytes);
// Reclaim disposable previews before refusing a working reservation. Called
// under the reservation lock; it must not call back into this allocator.
void oma_working_set_reclaimer(void (*reclaim)(const char *, uint64_t));
void *oma_working_alloc(size_t bytes);
void oma_working_free(void *pixels);
// ONLY at a pixelpipe stage boundary: no thread may access input/output until
// this returns. Addresses stay fixed, input pixels are preserved, output is
// disposable. A false result requires invalidating this pipe's cached pixels
// and aborting the render. anticipated_bytes includes upcoming output/scratch.
int oma_working_prepare(const void *input, size_t input_bytes, void *output, size_t anticipated_bytes);
typedef struct oma_working_stats {
    uint64_t ram_bytes, disk_bytes, peak_disk_bytes, backed_allocations, migrations, failures;
} oma_working_stats;
oma_working_stats oma_working_get_stats(void);
// Includes other processes sharing this cache. Reclaims only abandoned working
// files, never an active mapping or a preview/photo. Safe during processing.
uint64_t oma_working_disk_usage(const char *directory);
#ifdef __cplusplus
}
#endif
