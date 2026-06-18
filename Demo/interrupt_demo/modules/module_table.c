#include "bm_module.h"

BM_MODULE_DECLARE(events);
BM_MODULE_DECLARE(tick);
BM_MODULE_DECLARE(sample);
BM_MODULE_DECLARE(stats);
BM_MODULE_DECLARE(hw);

BM_MODULE_TABLE(
    BM_MODULE_ENTRY(events),
    BM_MODULE_ENTRY(tick),
    BM_MODULE_ENTRY(sample),
    BM_MODULE_ENTRY(stats),
    BM_MODULE_ENTRY(hw));
