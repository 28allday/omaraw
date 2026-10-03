#pragma once
#include <QtGlobal>

// One target shared by the working pixelpipe and retained CPU image caches.
// Active images, drivers, AI workers and non-tileable modules can exceed it.
struct ResourceBudget {
    int working, sources, contexts, detail, overviews, snapshots; // MiB
    static ResourceBudget forMemory(int mib) {
        mib = qBound(1024, mib, 65536);
        const int work = qMax(512, mib * 40 / 100), remaining = mib - work;
        const int source = remaining / 2, contexts = remaining / 3;
        const int detail = remaining * 2 / 15, overview = remaining / 60;
        return {work, source, contexts, detail, overview,
                mib - work - source - contexts - detail - overview};
    }
};
