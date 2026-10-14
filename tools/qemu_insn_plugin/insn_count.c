// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
/* Minimal QEMU TCG plugin: counts executed guest instructions and prints
 * one line at exit:
 *
 *   TAP_SR_INSN_COUNT <n>
 *
 * Used by scripts/icount.py for the deterministic performance ratchet
 * (docs/PERFORMANCE.md). Counting uses the inline-add fast path; the single
 * counter is exact for our single-vCPU deterministic workloads.
 *
 * A workload that brackets calls with tools/probe/call_marker.h's
 * tap_sr_call_begin() / tap_sr_call_end() (the per-call maximum workloads,
 * the roadmap's A6) gets a second line:
 *
 *   TAP_SR_CALL_MAX <max> call=<k> calls=<n> total=<sum>
 *
 * the most instructions executed between one begin and the next end, which
 * of the n bracketed calls it was (1-based), and their sum. The markers are
 * found by symbol (qemu_plugin_insn_symbol, the guest image's symbol table),
 * on the first of their instructions in a translation block: a call enters
 * at the function's start, and the markers are straight-line code. Only
 * those two instructions get a callback, so a workload without the markers
 * counts exactly as before.
 *
 * Build (qemu-plugin.h fetched for the matching QEMU 8.2.x; API v2):
 *   gcc -shared -fPIC $(pkg-config --cflags glib-2.0) \
 *       -I<dir with qemu-plugin.h> insn_count.c -o libinsncount.so
 */
#include <glib.h>
#include <inttypes.h>
#include <qemu-plugin.h>
#include <stdint.h>
#include <string.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t insn_count;

/* The per-call maximum (the file comment). */
static uint64_t call_begin_at;
static uint64_t call_max;
static uint64_t call_max_at;
static uint64_t call_n;
static uint64_t call_total;

static void on_call_begin(unsigned int vcpu, void* userdata) {
    (void)vcpu;
    (void)userdata;
    call_begin_at = insn_count;
}

static void on_call_end(unsigned int vcpu, void* userdata) {
    (void)vcpu;
    (void)userdata;
    const uint64_t d = insn_count - call_begin_at;
    ++call_n;
    call_total += d;
    if (d > call_max) {
        call_max    = d;
        call_max_at = call_n;
    }
}

/* ANCHOR: pf_hooks */
static void tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb* tb) {
    (void)id;
    size_t      n    = qemu_plugin_tb_n_insns(tb);
    const char* prev = NULL;
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn* insn = qemu_plugin_tb_get_insn(tb, i);
        qemu_plugin_register_vcpu_insn_exec_inline(insn, QEMU_PLUGIN_INLINE_ADD_U64, &insn_count, 1);
        const char* sym = qemu_plugin_insn_symbol(insn);
        if (sym != NULL && (prev == NULL || strcmp(sym, prev) != 0)) {
            if (strcmp(sym, "tap_sr_call_begin") == 0) {
                qemu_plugin_register_vcpu_insn_exec_cb(insn, on_call_begin, QEMU_PLUGIN_CB_NO_REGS, NULL);
            }
            else if (strcmp(sym, "tap_sr_call_end") == 0) {
                qemu_plugin_register_vcpu_insn_exec_cb(insn, on_call_end, QEMU_PLUGIN_CB_NO_REGS, NULL);
            }
        }
        prev = sym;
    }
}

static void at_exit(qemu_plugin_id_t id, void* userdata) {
    (void)id;
    (void)userdata;
    g_autofree gchar* msg = g_strdup_printf("TAP_SR_INSN_COUNT %" PRIu64 "\n", insn_count);
    qemu_plugin_outs(msg);
    if (call_n != 0) {
        g_autofree gchar* calls =
            g_strdup_printf("TAP_SR_CALL_MAX %" PRIu64 " call=%" PRIu64 " calls=%" PRIu64 " total=%" PRIu64 "\n",
                            call_max, call_max_at, call_n, call_total);
        qemu_plugin_outs(calls);
    }
}
/* ANCHOR_END: pf_hooks */

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t* info, int argc, char** argv) {
    (void)info;
    (void)argc;
    (void)argv;
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
    return 0;
}
