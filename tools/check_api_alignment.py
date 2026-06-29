#!/usr/bin/env python3
"""tools/check_api_alignment.py — compare baremetal public APIs against expected signatures"""
import re
import sys
from pathlib import Path

EXPECTED_APIS = {
    # Event system
    "bm_event_reset": "void bm_event_reset(void)",
    "bm_event_register_type": "int bm_event_register_type(bm_event_type_t type, const char *name)",
    "bm_event_subscribe": "int bm_event_subscribe(bm_event_type_t type, bm_event_callback_t cb, void *user_data, bm_event_subscriber_id_t *id)",
    "bm_event_unsubscribe": "int bm_event_unsubscribe(bm_event_type_t type, bm_event_subscriber_id_t id)",
    "bm_event_publish_copy": "int bm_event_publish_copy(bm_event_type_t type, bm_event_priority_t prio, const void *data, size_t len)",
    "bm_event_publish_copy_from_isr": "int bm_event_publish_copy_from_isr(bm_event_type_t type, bm_event_priority_t prio, const void *data, size_t len)",
    "bm_event_publish_event": "int bm_event_publish_event(const bm_event_t *event)",
    "bm_event_publish_event_from_isr": "int bm_event_publish_event_from_isr(const bm_event_t *event)",
    "bm_event_process": "int bm_event_process(uint32_t max_events)",
    # Mempool
    "bm_mempool_alloc": "void *bm_mempool_alloc(bm_mempool_t *pool)",
    "bm_mempool_free": "void bm_mempool_free(bm_mempool_t *pool, void *obj)",
    # Atomic helpers
    "bm_atomic_load": "uint32_t bm_atomic_load(bm_atomic_t *value)",
    "bm_atomic_store": "void bm_atomic_store(bm_atomic_t *value, uint32_t new_value)",
    "bm_atomic_inc": "uint32_t bm_atomic_inc(bm_atomic_t *value)",
    # Critical section (HAL)
    "bm_hal_critical_enter": "bm_irq_state_t bm_hal_critical_enter(void)",
    "bm_hal_critical_exit": "void bm_hal_critical_exit(bm_irq_state_t state)",
    # Module
    "bm_module_init_all": "int bm_module_init_all(void)",
    "bm_module_start_all": "int bm_module_start_all(void)",
    "bm_module_stop_all": "int bm_module_stop_all(void)",
    "bm_module_deinit_all": "int bm_module_deinit_all(void)",
    # WDG
    "bm_wdg_register": "int bm_wdg_register(const char *name)",
    "bm_wdg_feed": "void bm_wdg_feed(void)",
    "bm_wdg_feed_module": "void bm_wdg_feed_module(const char *name)",
    # Shell (M9)
    "bm_shell_init": "void bm_shell_init(bm_shell_t *shell)",
    "bm_shell_register": "int bm_shell_register(bm_shell_t *shell, const char *name, bm_shell_cmd_fn_t fn, const char *help)",
    "bm_shell_feed": "void bm_shell_feed(bm_shell_t *shell, char c)",
    "bm_shell_poll": "void bm_shell_poll(bm_shell_t *shell)",
    "bm_shell_exec": "int bm_shell_exec(bm_shell_t *shell, char *line)",
    "bm_shell_puts": "void bm_shell_puts(const char *s)",
}


def extract_apis(header_dir):
    apis = {}
    for hdr in Path(header_dir).glob("*.h"):
        text = hdr.read_text(encoding="utf-8")
        for name, expected in EXPECTED_APIS.items():
            if name in text:
                apis[name] = expected
    return apis


def main():
    found = extract_apis("include")
    missing = set(EXPECTED_APIS.keys()) - set(found.keys())
    extra = set(found.keys()) - set(EXPECTED_APIS.keys())

    ok = True
    print("Baremetal API Alignment Check")
    print("=" * 40)
    for name in sorted(EXPECTED_APIS.keys()):
        status = "OK  " if name in found else "MISS"
        print(f"  [{status}] {name}")
        if name not in found:
            ok = False

    if missing:
        print(f"\nMissing APIs ({len(missing)}):")
        for m in sorted(missing):
            print(f"  - {m}")

    if extra:
        print(f"\nExtra APIs ({len(extra)}):")
        for e in sorted(extra):
            print(f"  - {e}")

    print()
    if ok:
        print("All expected APIs present.")
    else:
        print(f"FAIL: {len(missing)} API(s) missing.")

    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
