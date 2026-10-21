"""Compile Neon Duel's production touch branch with its actual combat engine."""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
APP = ROOT / 'apps/neon-duel'


def function(source, name):
    match = re.search(r'^(?:static |ASTER_APP_EXPORT )?(?:void|uint32_t) ' + name + r'\(', source, re.M)
    start = source.index('{', match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


class NeonDuelInput(unittest.TestCase):
    def test_two_touch_controls_and_lifecycle_under_sanitizers(self):
        source = (APP / 'src/main.c').read_text()
        touch = source.split('}else if(ev->type==ASTER_EVENT_TOUCH){', 1)[1].split('}else if(ev->type==ASTER_EVENT_KEY){', 1)[0]
        helpers = ''.join(function(source, name) for name in ('clear_input', 'update_held', 'hit_button', 'aster_app_pause', 'aster_app_resume'))
        code = r'''
#include <assert.h>
#include <string.h>
#include <aster/event.h>
#include "game.h"
#define ASTER_APP_EXPORT
static Game game;
static uint32_t slots[8], keys, width=320, height=240, previous, accumulator;
static int suspended;
static uint32_t aster_time_now_ms(void) { return 100; }
static const int button_x[7]={5,49,93,133,177,223,269};
static const int button_w[7]={40,40,36,40,42,42,46};
''' + helpers + '\nstatic void input(const aster_event_t *ev) {\n' + touch + r'''
}
static void point(unsigned slot, unsigned action, unsigned x, unsigned y) {
    aster_event_t e={.type=ASTER_EVENT_TOUCH};
    e.u.touch.slot=slot; e.u.touch.action=action; e.u.touch.x=x; e.u.touch.y=y; input(&e);
}
static void reset(void) { game_init(&game); game_start(&game,1); game.ready_ms=0; clear_input(); }
int main(void) {
    reset();point(0,ASTER_TOUCH_DOWN,68,210);point(1,ASTER_TOUCH_DOWN,195,210);
    assert(game.held==(BTN_RIGHT|BTN_PUNCH));assert(game.p[0].attack==PUNCH);
    point(1,ASTER_TOUCH_UP,195,210);assert(game.held==BTN_RIGHT);
    float x=game.p[0].x;for(int i=0;i<20;i++)game_step(&game,33);assert(game.p[0].x>x);
    point(0,ASTER_TOUCH_UP,68,210);assert(game.held==0);
    reset();point(1,ASTER_TOUCH_DOWN,24,210);point(0,ASTER_TOUCH_DOWN,242,210);
    assert(game.held==(BTN_LEFT|BTN_KICK));point(1,ASTER_TOUCH_UP,24,210);assert(game.held==BTN_KICK);
    point(0,ASTER_TOUCH_MOVE,160,100);assert(game.held==0);
    reset();point(0,ASTER_TOUCH_DOWN,110,210);point(1,ASTER_TOUCH_DOWN,195,210);
    assert((game.held&(BTN_JUMP|BTN_PUNCH))==(BTN_JUMP|BTN_PUNCH));
    aster_app_pause();assert(suspended&&game.held==0);aster_app_resume();assert(!suspended&&game.held==0&&previous==100);
    point(1,ASTER_TOUCH_UP,195,210);assert(game.held==0);
    point(99,ASTER_TOUCH_DOWN,195,210);assert(game.held==0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            exe = str(pathlib.Path(folder) / 'neon-input')
            build = subprocess.run(['cc', '-DNEON_HOST_TEST', '-std=c99', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-I'+str(ROOT/'include'), '-I'+str(APP/'src'), '-x', 'c', '-', '-x', 'none', str(APP/'src/game.c'), '-o', exe], input=code, text=True, capture_output=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([exe], text=True, capture_output=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
