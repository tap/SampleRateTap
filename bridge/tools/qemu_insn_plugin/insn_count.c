/* Minimal QEMU TCG plugin: counts executed guest instructions and prints
 * one line at exit:
 *
 *   RATIO_INSN_COUNT <n>
 *
 * Used by scripts/icount.py for the deterministic performance ratchet
 * (PLAN.md section 7). Counting uses the inline-add fast path; the single
 * counter is exact for our single-vCPU deterministic workloads. Ported from
 * SampleRateTap's tools/qemu_insn_plugin/ with the marker renamed.
 *
 * Build (qemu-plugin.h fetched for the matching QEMU 8.2.x; API v2):
 *   gcc -shared -fPIC $(pkg-config --cflags glib-2.0) \
 *       -I<dir with qemu-plugin.h> insn_count.c -o libinsncount.so
 */
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
#include <glib.h>
#include <inttypes.h>
#include <qemu-plugin.h>
#include <stdint.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t insn_count;

static void tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb* tb) {
    (void)id;
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn* insn = qemu_plugin_tb_get_insn(tb, i);
        qemu_plugin_register_vcpu_insn_exec_inline(insn, QEMU_PLUGIN_INLINE_ADD_U64, &insn_count, 1);
    }
}

static void at_exit(qemu_plugin_id_t id, void* userdata) {
    (void)id;
    (void)userdata;
    g_autofree gchar* msg = g_strdup_printf("RATIO_INSN_COUNT %" PRIu64 "\n", insn_count);
    qemu_plugin_outs(msg);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t* info, int argc, char** argv) {
    (void)info;
    (void)argc;
    (void)argv;
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
    return 0;
}
