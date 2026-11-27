import pathlib,re,tempfile,subprocess,shlex
src=(pathlib.Path(__file__).resolve().parents[2] / 'target/i386/latx/latx-config.c').read_text()
def fn(n, typ='void'):
 m=re.search(r'(?:static )?'+typ+r' '+n+r'\([^;]*?\)\s*\{',src); assert m,n
 j=m.end(); d=1
 while d:
  d+=(src[j]=='{')-(src[j]=='}');j+=1
 return src[m.start():j]
support=r'''
#include <glib.h>
#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#define qatomic_fetch_add(p,n) __atomic_fetch_add(p,n,__ATOMIC_SEQ_CST)
#define LATX_TB_EFLAG_STAT_DEFER_TB 0
#define LATX_TB_EFLAG_STAT_DEFER_EDGE 1
#define LATX_TB_EFLAG_STAT_NR 2
typedef struct { void (*notify)(void *,void *); } Notifier;
static void report(void*a,void*b) { (void)a; (void)b; }
static Notifier latx_tb_eflag_stat_notifier={.notify=report};
static unsigned registrations;
static void qemu_add_exit_notifier(Notifier*n) { (void)n; __atomic_fetch_add(&registrations,1,__ATOMIC_SEQ_CST); }
uint64_t latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_NR];
static bool latx_tb_eflag_stat_materialise_on;
static gsize latx_tb_eflag_stat_inited;
'''.replace('#include <stdint.h>','#include <stdint.h>\n#include <stdbool.h>')
test=r'''
static void *worker(void *p) { (void)p; for (int i=0;i<100000;i++) { latx_tb_eflag_stat_init(); latx_tb_eflag_stat_defer(i & 1); } return NULL; }
int main(void) { pthread_t threads[8]; for(int i=0;i<8;i++) assert(!pthread_create(&threads[i],NULL,worker,NULL)); for(int i=0;i<8;i++) assert(!pthread_join(threads[i],NULL)); assert(registrations==1); assert(latx_tb_eflag_stats[0]==400000); assert(latx_tb_eflag_stats[1]==800000); puts("P2 notifier/atomic 8-thread stress: PASS"); }
'''
with tempfile.TemporaryDirectory() as d:
 c=pathlib.Path(d)/'t.c'; o=pathlib.Path(d)/'t';c.write_text(support+fn('latx_tb_eflag_stat_init')+fn('latx_tb_eflag_stat_defer')+test)
 flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','glib-2.0'],text=True))
 subprocess.run(['cc','-O2','-std=c11','-Wall','-Wextra','-Werror',str(c),'-pthread','-o',str(o)]+flags,check=True);subprocess.run([str(o)],check=True)
