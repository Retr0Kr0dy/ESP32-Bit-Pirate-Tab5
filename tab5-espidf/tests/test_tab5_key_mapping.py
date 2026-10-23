"""Test the native event->IInput adapter without IDF or board hardware."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class Tab5KeyMappingTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'host g++ missing')
    def test_key_events(self):
        source = r"""#include "Tab5KeyMapping.h"
#include "Tab5ArrowRepeat.h"
#include <cassert>
static sic_key_event_t ev(sic_keycode_t code, bool press=true, char ascii=0) {
    sic_key_event_t e{}; e.code=code; e.pressed=press; e.ascii=ascii; return e;
}
int main() {
    assert(tab5MapKeyEvent(ev(SIC_KEY_ENTER)) == KEY_OK);
    assert(tab5MapKeyEvent(ev(SIC_KEY_TAB)) == KEY_TAB_CUSTOM);
    assert(tab5MapKeyEvent(ev(SIC_KEY_ESC)) == KEY_ESC_CUSTOM);
    assert(tab5MapKeyEvent(ev(SIC_KEY_BACKSPACE)) == KEY_DEL);
    assert(tab5MapKeyEvent(ev(SIC_KEY_DEL)) == KEY_DEL);
    assert(tab5MapKeyEvent(ev(SIC_KEY_UP)) == KEY_ARROW_UP);
    assert(tab5MapKeyEvent(ev(SIC_KEY_RIGHT)) == KEY_ARROW_RIGHT);
    assert(tab5MapKeyEvent(ev(SIC_KEY_CTRL)) == KEY_NONE);
    assert(tab5MapKeyEvent(ev(SIC_KEY_SHIFT, false, 'A')) == KEY_NONE);
    assert(tab5MapKeyEvent(ev(SIC_KEY_SPACE, true, ' ')) == ' ');
    assert(tab5MapKeyEvent(ev(SIC_KEY_NONE, true, 'q')) == 'q');
    Tab5ArrowRepeat repeat;
    repeat.onEvent(ev(SIC_KEY_DOWN), 1000);
    assert(repeat.poll(1200) == KEY_NONE);
    assert(repeat.poll(1350) == KEY_ARROW_DOWN);
    assert(repeat.poll(1351) == KEY_NONE);
    repeat.onEvent(ev(SIC_KEY_DOWN, false), 1352);
    assert(repeat.poll(1400) == KEY_NONE);
    repeat.onEvent(ev(SIC_KEY_UP), 2000);
    assert(repeat.poll(2360) == KEY_ARROW_UP);
    repeat.onEvent(ev(SIC_KEY_ENTER), 2370);
    assert(repeat.poll(2500) == KEY_NONE);
    repeat.onEvent(ev(SIC_KEY_UP), 3000);
    assert(repeat.poll(34000) == KEY_NONE); // missed release cannot scroll forever
}
"""
        with tempfile.TemporaryDirectory() as d:
            d=Path(d); (d/'test.cpp').write_text(source)
            cmd=['g++','-std=c++17','-Wall','-Wextra','-Werror',
                 '-I'+str(ROOT/'main/boards/tab5_bitpirate'),
                 '-I'+str(ROOT/'lib/BitPirateCore'),
                 '-I'+str(ROOT/'lib/SIC/include'),
                 str(d/'test.cpp'),'-o',str(d/'test')]
            subprocess.run(cmd,check=True,capture_output=True,text=True)
            subprocess.run([str(d/'test')],check=True,capture_output=True,text=True)
