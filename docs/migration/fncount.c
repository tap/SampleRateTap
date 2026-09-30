// Per-function instruction attribution for the G3 allowance check
// (docs/migration/allow-g3.txt; gates.py cross --target hexagon).
//
// Every executed instruction (packet, on Hexagon) is attributed by an
// inline counter to the symbol whose extent contains it, so the counts are
// exact, not sampled, and the total equals tools/qemu_insn_plugin's count.
//
//   -plugin libfncount.so,symfile=<path>
//
// symfile: one "hexaddr hexsize name" line per text symbol, sorted by
// address (from llvm-nm -n -S --defined-only). Prints one "FN <count>
// <name>" line per symbol that ran, "FN <count> ??" for unattributed
// instructions, and "FN_TOTAL <count>".
#include <glib.h>
#include <inttypes.h>
#include <qemu-plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    uint64_t addr, size;
    char*    name;
    uint64_t count;
} sym_t;

static sym_t*   syms;
static int      nsyms;
static uint64_t unknown;

static sym_t* lookup(uint64_t pc) {
    int lo = 0, hi = nsyms - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (pc < syms[mid].addr)
            hi = mid - 1;
        else if (pc >= syms[mid].addr + syms[mid].size)
            lo = mid + 1;
        else
            return &syms[mid];
    }
    return NULL;
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb* tb) {
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn* insn = qemu_plugin_tb_get_insn(tb, i);
        sym_t*                   s    = lookup(qemu_plugin_insn_vaddr(insn));
        qemu_plugin_register_vcpu_insn_exec_inline(insn, QEMU_PLUGIN_INLINE_ADD_U64, s ? &s->count : &unknown, 1);
    }
}

static void plugin_exit(qemu_plugin_id_t id, void* p) {
    uint64_t total = unknown;
    for (int i = 0; i < nsyms; i++)
        total += syms[i].count;
    for (int i = 0; i < nsyms; i++)
        if (syms[i].count)
            qemu_plugin_outs(g_strdup_printf("FN %" PRIu64 " %s\n", syms[i].count, syms[i].name));
    qemu_plugin_outs(g_strdup_printf("FN %" PRIu64 " ??\nFN_TOTAL %" PRIu64 "\n", unknown, total));
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t* info, int argc, char** argv) {
    const char* symfile = NULL;
    for (int i = 0; i < argc; i++)
        if (g_str_has_prefix(argv[i], "symfile="))
            symfile = argv[i] + 8;
    if (!symfile) {
        fprintf(stderr, "fncount: symfile=<path> required\n");
        return -1;
    }
    FILE* f = fopen(symfile, "r");
    if (!f) {
        perror(symfile);
        return -1;
    }
    char line[4096];
    int  cap = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long long a, s;
        char               name[4000];
        if (sscanf(line, "%llx %llx %3999[^\n]", &a, &s, name) != 3)
            continue;
        if (nsyms == cap) {
            cap  = cap ? cap * 2 : 1024;
            syms = realloc(syms, cap * sizeof *syms);
        }
        syms[nsyms].addr  = a;
        syms[nsyms].size  = s;
        syms[nsyms].name  = strdup(name);
        syms[nsyms].count = 0;
        nsyms++;
    }
    fclose(f);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
