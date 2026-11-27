#!/usr/bin/env python3
"""Exercise the real experimental page-table fallback dispatch in isolation."""
from pathlib import Path
import subprocess
import tempfile

src = (Path(__file__).resolve().parents[2] / 'target/i386/tcg/sysemu/excp_helper.c').read_text()
start = src.index('static inline bool ptw_hack_translate(')
end = src.index('\n#endif', start)
helper = src[start:end]
# All fast and complete walker levels must use the dispatch; raw reads cannot
# handle an MMIO page-table entry for which ptw_translate supplies no host VA.
assert src.count('ptw_hack_translate(&pte_trans, pte_addr)') == 12
assert 'pte = *((uint64_t *)pte_trans.haddr)' not in src
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t hwaddr;
typedef struct { void *haddr; hwaddr gaddr; } PTETranslate;
#define HACK_RAMBASE UINT64_C(0x10000000000)
#define unlikely(x) (x)
static bool experimental, succeeds;
static unsigned calls;
static bool hamt_allow_long_no_pse(void) { return experimental; }
static bool ptw_translate(PTETranslate *p, hwaddr a) {
    calls++; p->gaddr = a; p->haddr = 0; return succeeds;
}
'''
suffix = r'''
int main(void) {
    PTETranslate p = {0};
    experimental = false; succeeds = true;
    assert(ptw_hack_translate(&p, 0x1234));
    assert((uintptr_t)p.haddr == HACK_RAMBASE + 0x1234 && calls == 0);
    experimental = true;
    assert(ptw_hack_translate(&p, UINT64_C(0x840004000)));
    assert(calls == 1 && !p.haddr && p.gaddr == UINT64_C(0x840004000));
    succeeds = false;
    assert(!ptw_hack_translate(&p, 0x2000));
    assert(calls == 2 && p.gaddr == 0x2000);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c, exe = Path(d) / 'test.c', Path(d) / 'test'
    c.write_text(prefix + helper + suffix)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('HAMT experimental PTE fallback dispatch: PASS')
