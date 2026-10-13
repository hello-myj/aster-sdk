"""Host regression for the SDK gfx command-ring encoder (aster_gfx_engine.h).

The engine is header-only and compiles into every APP, so these checks run
the real encoders on the host against a stub aster_gfx_exec() that captures
what each submit would have sent. They reproduce the arena/ring overflow bug
from the optimization handbook and pin the joint-reservation contract:

  * a TEXT command's arena offset/length always land inside the arena bytes
    that are submitted in the SAME batch (never a spilled-away payload);
  * a submit that cannot fit even after one spill reports an error instead of
    queueing a dangling command or drawing a half frame;
  * the sticky error is visible through aster_ge_present()/aster_ge_error().

Template mirrors test_imu_filter.py: -I include, -I lib/aster, cc -Werror.
"""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

# Stub ABI imports + a recorder. Every case drives the real inline encoders.
HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <aster/gfx.h>
#include "aster_gfx.h"
#include "aster_libc.h"
#include <aster_gfx_engine.h>

/* Recorded submits. */
#define MAXSUBMIT 64
#define MAXCMD 4096
static uint32_t submits;
static uint32_t sub_n[MAXSUBMIT], sub_arena[MAXSUBMIT];
static uint32_t sub_present[MAXSUBMIT][4];
static uint32_t fail_at; /* make the Nth submit (1-based) fail; 0 = never */
static int32_t sub_ret[MAXSUBMIT];
static aster_gfx_cmd_t sub_cmd[MAXSUBMIT][MAXCMD];
/* Snapshot of each submit's arena so a TEXT payload can be resolved back to
 * bytes (the raw w[3] offset is batch-relative and legitimately differs when a
 * small ring spills and resets arena_used between batches). */
#define MAXARENA 65536
static uint8_t sub_bytes[MAXSUBMIT][MAXARENA];

int32_t aster_gfx_exec(aster_handle_t surf, const aster_gfx_cmd_t *cmds, uint32_t n,
                       const uint8_t *arena, uint32_t arena_len,
                       uint32_t px, uint32_t py, uint32_t pw, uint32_t ph) {
  (void)surf;
  assert(submits < MAXSUBMIT);
  assert(n <= MAXCMD);
  if (fail_at == submits + 1u) { sub_ret[submits] = ASTER_ENOSPC; submits++; return ASTER_ENOSPC; }
  sub_ret[submits] = (int32_t)n;
  sub_n[submits] = n; sub_arena[submits] = arena_len;
  sub_present[submits][0]=px; sub_present[submits][1]=py;
  sub_present[submits][2]=pw; sub_present[submits][3]=ph;
  for (uint32_t i=0;i<n;i++) sub_cmd[submits][i]=cmds[i];
  if (arena_len > MAXARENA) { /* keep the recorder's fixed row safe */
    assert(arena_len <= MAXARENA);
  }
  memcpy(sub_bytes[submits], arena, arena_len);
  /* Re-validate every TEXT payload lies inside this batch's arena, exactly as
   * the firmware rasterizer demands of a correct encoder: off<=arena_len and
   * len<=arena_len-off. This is the bug the handbook reproduced -- a TEXT
   * whose arena bytes were spilled away before the command shipped. */
  for (uint32_t i=0;i<n;i++) {
    if ((sub_cmd[submits][i].w[0] & 0xFFu) == ASTER_GFX_TEXT) {
      uint32_t off = sub_cmd[submits][i].w[3];
      uint32_t len = sub_cmd[submits][i].w[4] & 0xFFFFu;
      assert(len != 0u);
      assert(off <= arena_len);
      assert(len <= arena_len - off);
    }
  }
  submits++;
  return (int32_t)n;
}
int32_t aster_gfx_text_w(uint32_t px, const char *utf8, uint32_t utf8_len) {
  (void)px; (void)utf8; return (int32_t)utf8_len;
}
int32_t aster_gfx_bmp_load(const char *path, uint32_t path_len, uint32_t cap_w,
                           uint32_t cap_h, uint32_t *out_handle,
                           uint32_t *out_w, uint32_t *out_h) {
  (void)path;(void)path_len;(void)cap_w;(void)cap_h;(void)out_handle;(void)out_w;(void)out_h;
  return ASTER_OK;
}
int32_t aster_gfx_bmp_free(uint32_t handle) { (void)handle; return ASTER_OK; }

static uint8_t arena[65536] __attribute__((unused));

static void reset(void) {
  submits = 0; fail_at = 0;
  memset(sub_n,0,sizeof(sub_n)); memset(sub_arena,0,sizeof(sub_arena));
  memset(sub_cmd,0,sizeof(sub_cmd)); memset(sub_bytes,0,sizeof(sub_bytes));
}

/* Words each op actually defines (the ABI leaves the rest of the 24-byte slot
 * untouched, so stale ring bytes there are not a semantic difference). */
static const uint32_t cmd_defined[13] = {
  /* CLEAR */ (1u<<0)|(1u<<1),
  /* RECT_F */ (1u<<0)|(1u<<1)|(1u<<2)|(1u<<3),
  /* RECT_S */ (1u<<0)|(1u<<1)|(1u<<2)|(1u<<3)|(1u<<4),
  /* LINE_AA*/ (1u<<0)|(1u<<1)|(1u<<2)|(1u<<3),
  /* ELLIPSE*/ (1u<<0)|(1u<<1)|(1u<<2)|(1u<<3),
  /* TRI    */ (1u<<0)|(1u<<1)|(1u<<2)|(1u<<3)|(1u<<4),
  /* GRAD   */ (1u<<0)|(1u<<1)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<5),
  /* TEXT   */ (1u<<0)|(1u<<1)|(1u<<2)|(1u<<4)|(1u<<5), /* w[3] batch-relative */
  /* resv   */ 0,
  /* BMP    */ (1u<<0)|(1u<<1)|(1u<<2),
  /* BMP_S  */ (1u<<0)|(1u<<1)|(1u<<2)|(1u<<4),
  /* BMP_A  */ (1u<<0)|(1u<<1)|(1u<<2)|(1u<<4),
  /* BMP_SA */ (1u<<0)|(1u<<1)|(1u<<2)|(1u<<4)|(1u<<5),
};
static int cmd_same(const aster_gfx_cmd_t *a, const aster_gfx_cmd_t *b) {
  uint32_t op = a->w[0] & 0xFFu;
  if (op > 12u || (b->w[0] & 0xFFu) != op) return 0;
  for (uint32_t j = 0; j < 6u; j++)
    if ((cmd_defined[op] >> j) & 1u) {
      if (a->w[j] != b->w[j]) return 0;
    }
  return 1;
}
'''


class GfxEngine(unittest.TestCase):
    def compile(self, body):
        with tempfile.TemporaryDirectory() as directory:
            executable = str(Path(directory) / 'gfx_engine')
            result = subprocess.run(
                ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                 '-x', 'c', '-', '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'lib/aster'),
                 '-o', executable],
                input=HARNESS + body, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            proc = subprocess.run([executable], check=True, capture_output=True, timeout=10)
            return proc.stdout.decode()

    def test_text_ring_capacity_one(self):
        """The handbook repro: ring of 1, rect then ABC -> text must survive."""
        self.compile(r'''
int main(void) {
  reset();
  aster_gfx_cmd_t ring[1];
  aster_ge_t g;
  aster_ge_init(&g, 1, 240, 320, ring, 1, arena, sizeof(arena));
  aster_ge_rect(&g, 10, 10, 20, 20, 0x1234);          /* fills the 1-slot ring */
  assert(aster_ge_text(&g, 8, 24, 16, 0xFFFF, ASTER_GFX_NOBG, "ABC") == 0);
  assert(aster_ge_present(&g) >= 0);
  /* Two submits: the rect spilled, then TEXT went out WITH its payload. */
  assert(submits == 2);
  assert(sub_arena[0] == 0);
  uint32_t text_seen = 0;
  for (uint32_t s=0;s<submits;s++) for (uint32_t i=0;i<sub_n[s];i++)
    if ((sub_cmd[s][i].w[0]&0xFFu)==ASTER_GFX_TEXT) {
      text_seen++;
      assert((sub_cmd[s][i].w[4]&0xFFFFu)==3u);
      assert(sub_arena[s] >= 3u); /* payload arena shipped with the TEXT */
    }
  assert(text_seen == 1);
  puts("ok"); return 0;
}
''')

    def test_default_ring_many_text(self):
        """Many TEXT ops on the default ring/arena: every payload stays valid
        across any mid-frame spills (the assert-in-stub is the real check)."""
        self.compile(r'''
int main(void) {
  reset();
  aster_gfx_cmd_t ring[96];
  uint8_t ar[768];
  aster_ge_t g;
  aster_ge_init(&g, 1, 240, 320, ring, 96, ar, sizeof(ar));
  for (int i=0;i<200;i++) {
    assert(aster_ge_text(&g, 0, 20, 16, 1, ASTER_GFX_NOBG, "ABC") == 0);
  }
  assert(aster_ge_present(&g) >= 0);
  assert(submits >= 2); /* must have spilled at least once */
  puts("ok"); return 0;
}
''')

    def test_single_text_exceeds_arena(self):
        self.compile(r'''
int main(void) {
  reset();
  aster_gfx_cmd_t ring[8];
  uint8_t ar[4];
  aster_ge_t g;
  aster_ge_init(&g, 1, 240, 320, ring, 8, ar, sizeof(ar));
  /* "ABCDEF" is 6 bytes > 4-byte arena: must fail, not queue a dangling cmd */
  assert(aster_ge_text(&g, 0, 20, 16, 1, ASTER_GFX_NOBG, "ABCDEF") < 0);
  assert(g.n == 0);          /* nothing queued */
  assert(aster_ge_error(&g) < 0);
  assert(submits == 0);      /* nothing published */
  puts("ok"); return 0;
}
''')

    def test_exact_ring_full_and_exact_arena_full(self):
        self.compile(r'''
int main(void) {
  reset();
  aster_gfx_cmd_t ring[2];
  uint8_t ar[3];
  aster_ge_t g;
  aster_ge_init(&g, 1, 240, 320, ring, 2, ar, sizeof(ar));
  /* exactly fill arena with one 3-byte text */
  assert(aster_ge_text(&g, 0, 20, 16, 1, ASTER_GFX_NOBG, "ABC") == 0);
  /* a second 3-byte text must spill (arena exactly full after) */
  assert(aster_ge_text(&g, 0, 20, 16, 1, ASTER_GFX_NOBG, "ABC") == 0);
  assert(aster_ge_present(&g) >= 0);
  assert(submits >= 1);
  for (uint32_t s=0;s<submits;s++) for (uint32_t i=0;i<sub_n[s];i++)
    if ((sub_cmd[s][i].w[0]&0xFFu)==ASTER_GFX_TEXT) assert(sub_arena[s]==3u);
  puts("ok"); return 0;
}
''')

    def test_both_short_simultaneously(self):
        self.compile(r'''
int main(void) {
  reset();
  aster_gfx_cmd_t ring[1];
  uint8_t ar[2];
  aster_ge_t g;
  aster_ge_init(&g, 1, 240, 320, ring, 1, ar, sizeof(ar));
  /* ring 1, arena 2: a 3-byte text can never fit (3 > whole arena) ->
   * EINVAL, nothing copied, nothing queued, nothing published */
  assert(aster_ge_text(&g, 0, 20, 16, 1, ASTER_GFX_NOBG, "ABC") < 0);
  assert(g.n == 0 && submits == 0);
  /* the frame is now "incomplete": present() must report the dropped TEXT
   * rather than return success -- the app must not treat it as published. */
  assert(aster_ge_present(&g) < 0);
  /* a fresh engine: a 2-byte text fits both a ring of 1 and an arena of 2 */
  reset();
  aster_ge_init(&g, 1, 240, 320, ring, 1, ar, sizeof(ar));
  aster_ge_rect(&g, 1, 1, 2, 2, 1);                    /* fills the ring */
  assert(aster_ge_text(&g, 0, 20, 16, 1, ASTER_GFX_NOBG, "AB") == 0); /* spilled */
  assert(aster_ge_present(&g) >= 0);                   /* frame complete */
  puts("ok"); return 0;
}
''')

    def test_zero_capacity_ring(self):
        self.compile(r'''
int main(void) {
  reset();
  uint8_t ar[16];
  aster_ge_t g;
  aster_ge_init(&g, 1, 240, 320, (aster_gfx_cmd_t*)ar, 0, ar, sizeof(ar));
  aster_ge_rect(&g, 0, 0, 1, 1, 1);              /* no slot: silent, error set */
  assert(g.err < 0);
  assert(aster_ge_present(&g) < 0);
  puts("ok"); return 0;
}
''')

    def test_null_text(self):
        self.compile(r'''
int main(void) {
  reset();
  aster_gfx_cmd_t ring[4];
  aster_ge_t g;
  aster_ge_init(&g, 1, 240, 320, ring, 4, arena, 64);
  assert(aster_ge_text(&g, 0, 20, 16, 1, ASTER_GFX_NOBG, NULL) < 0);
  assert(g.n == 0 && submits == 0);
  assert(aster_ge_error(&g) < 0);
  puts("ok"); return 0;
}
''')

    def test_host_submit_failure_then_recovery(self):
        """Host returns failure on the Nth exec; the failure becomes sticky and
        the next frame recovers to a clean success."""
        self.compile(r'''
int main(void) {
  reset();
  aster_gfx_cmd_t ring[8];
  uint8_t ar[64];
  aster_ge_t g;
  aster_ge_init(&g, 1, 240, 320, ring, 8, ar, sizeof(ar));
  /* first submit fails */
  fail_at = 1;
  aster_ge_rect(&g, 0, 0, 4, 4, 1);
  assert(aster_ge_present(&g) < 0);   /* frame not fully published */
  /* second frame succeeds */
  fail_at = 0;
  aster_ge_rect(&g, 0, 0, 4, 4, 1);
  assert(aster_ge_present(&g) >= 0);
  puts("ok"); return 0;
}
''')

    def test_sticky_error_first_wins(self):
        """A reservation ENOMEM must not be overwritten by a later success."""
        self.compile(r'''
int main(void) {
  reset();
  aster_gfx_cmd_t ring[2];
  uint8_t ar[3];
  aster_ge_t g;
  aster_ge_init(&g, 1, 240, 320, ring, 2, ar, sizeof(ar));
  assert(aster_ge_text(&g, 0, 20, 16, 1, ASTER_GFX_NOBG, "ABC") == 0);
  /* now arena full and ring has 1 left; a fresh ABC spills then re-checks --
   * use a 4th-byte-free arena to force failure path with a 3-byte text when
   * arena=3 and ring already consumed: queue one more after spill fails only
   * when bytes>arena. Craft the guaranteed failure: */
  aster_ge_t h;
  aster_ge_init(&h, 1, 240, 320, ring, 4, ar, 2);
  assert(aster_ge_text(&h, 0, 20, 16, 1, ASTER_GFX_NOBG, "ABC") < 0); /* 3 > 2 */
  int32_t first = h.err;
  aster_ge_rect(&h, 0, 0, 1, 1, 1);          /* a later op succeeds (ring has room) */
  assert(h.err == first);                     /* first error preserved, not cleared */
  puts("ok"); return 0;
}
''')

    def test_small_and_large_ring_same_pixels(self):
        """Same command sequence on a small ring (forces spills) vs a big ring
        must produce an identical command stream. A TEXT's w[3] arena offset is
        batch-relative (a spill resets arena_used), so offsets legitimately
        differ between the two; compare every other field and verify each TEXT
        resolves to the right payload bytes in its own submit's arena."""
        self.compile(r'''
int main(void) {
  reset();
  /* big ring: everything in one submit */
  aster_gfx_cmd_t big[256];
  uint8_t ar[4096];
  aster_ge_t g;
  aster_ge_init(&g, 1, 240, 320, big, 256, ar, sizeof(ar));
  for (int i=0;i<40;i++) { aster_ge_rect(&g, i, i, 4, 4, 0x1000+i);
    assert(aster_ge_text(&g, i, i+20, 16, 1, ASTER_GFX_NOBG, "ABC")==0); }
  assert(aster_ge_present(&g) >= 0);
  uint32_t big_submits = submits;
  static aster_gfx_cmd_t expected[128]; uint32_t exp_n = 0;
  for (uint32_t s=0;s<big_submits;s++)
    for (uint32_t i=0;i<sub_n[s];i++) {
      aster_gfx_cmd_t c = sub_cmd[s][i];
      /* resolve each TEXT payload and normalize away the batch-relative
       * offset; keep the length so a lost word would still be caught */
      if ((c.w[0]&0xFFu)==ASTER_GFX_TEXT) {
        uint32_t off=c.w[3], len=c.w[4]&0xFFFFu;
        assert(off+len<=sub_arena[s]);
        assert(memcmp(sub_bytes[s]+off,"ABC",3)==0);
        c.w[3]=0xDEADBEEFu; /* offset is intentionally batch-specific */
      }
      expected[exp_n++]=c;
    }

  /* small ring: same script, must spill but yield the identical stream */
  reset();
  aster_gfx_cmd_t small[3];
  aster_ge_t h;
  aster_ge_init(&h, 1, 240, 320, small, 3, ar, sizeof(ar));
  for (int i=0;i<40;i++) { aster_ge_rect(&h, i, i, 4, 4, 0x1000+i);
    assert(aster_ge_text(&h, i, i+20, 16, 1, ASTER_GFX_NOBG, "ABC")==0); }
  assert(aster_ge_present(&h) >= 0);
  uint32_t got = 0;
  for (uint32_t s=0;s<submits;s++)
    for (uint32_t i=0;i<sub_n[s];i++) {
      aster_gfx_cmd_t c = sub_cmd[s][i];
      if ((c.w[0]&0xFFu)==ASTER_GFX_TEXT) {
        uint32_t off=c.w[3], len=c.w[4]&0xFFFFu;
        assert(off+len<=sub_arena[s]);       /* payload in the shipped batch */
        assert(memcmp(sub_bytes[s]+off,"ABC",3)==0); /* resolved bytes correct */
        c.w[3]=0xDEADBEEFu;
      }
      assert(got < exp_n);
      assert(cmd_same(&c, &expected[got]));
      got++;
    }
  assert(got == exp_n);
  puts("ok"); return 0;
}
''')


    def test_failed_encoding_discards_pending_batch_and_keeps_first_error(self):
        self.compile(r'''
int main(void) {
 reset();aster_ge_t g;aster_gfx_cmd_t r[4];uint8_t a[2];
 aster_ge_init(&g,1,240,320,r,4,a,2);
 aster_ge_rect(&g,0,0,4,4,1);
 assert(aster_ge_text(&g,0,20,16,1,ASTER_GFX_NOBG,"ABC")==ASTER_EINVAL);
 assert(aster_ge_error(&g)==ASTER_EINVAL); /* inspect must not clear */
 fail_at=1;
 assert(aster_ge_present(&g)==ASTER_EINVAL);
 assert(submits==0 && g.n==0 && g.arena_used==0);
 fail_at=0;aster_ge_rect(&g,0,0,4,4,1);
 assert(aster_ge_present(&g)>=0 && submits==1);
 return 0;
}
''')

    def test_text_wire_length_boundary(self):
        self.compile(r'''
int main(void) {
 reset();static char text[65537];memset(text,'A',65536);text[65536]=0;
 aster_ge_t g;aster_gfx_cmd_t r[2];
 aster_ge_init(&g,1,240,320,r,2,arena,65536);
 assert(aster_ge_text(&g,0,20,16,1,ASTER_GFX_NOBG,text)<0);
 assert(aster_ge_present(&g)<0 && submits==0);
 text[65535]=0;
 assert(aster_ge_text(&g,0,20,16,1,ASTER_GFX_NOBG,text)==0);
 assert(aster_ge_present(&g)>=0 && sub_cmd[0][0].w[4]==65535);
 return 0;
}
''')

    def test_failed_spill_stops_tail_and_counts_actual_calls(self):
        self.compile(r'''
int main(void) {
 reset();aster_ge_t g;aster_gfx_cmd_t r[1];uint8_t a[8];
 aster_ge_init(&g,1,240,320,r,1,a,8);
 aster_ge_rect(&g,0,0,4,4,1);fail_at=1;
 assert(aster_ge_text(&g,0,20,16,1,ASTER_GFX_NOBG,"ABC")<0);
 aster_ge_rect(&g,1,1,4,4,1);
 assert(aster_ge_present(&g)==ASTER_ENOSPC && submits==1 && g.submit_calls==1);
 fail_at=0;aster_ge_rect(&g,0,0,4,4,1);aster_ge_rect(&g,1,1,4,4,1);
 assert(aster_ge_present(&g)>=0 && g.submit_calls==3);
 return 0;
}
''')

    def test_null_buffers_and_empty_text(self):
        self.compile(r'''
int main(void) {
 reset();aster_ge_t g;aster_gfx_cmd_t r[1];
 aster_ge_init(&g,1,240,320,NULL,1,arena,8);aster_ge_rect(&g,0,0,1,1,1);
 assert(aster_ge_present(&g)<0 && submits==0);
 aster_ge_init(&g,1,240,320,r,1,NULL,8);
 assert(aster_ge_text(&g,0,20,16,1,ASTER_GFX_NOBG,"ABC")<0);
 assert(aster_ge_present(&g)<0 && submits==0);
 aster_ge_init(&g,1,240,320,r,1,arena,8);
 assert(aster_ge_text(&g,0,20,16,1,ASTER_GFX_NOBG,"")==0);
 assert(aster_ge_present(&g)==0 && submits==0);
 return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
