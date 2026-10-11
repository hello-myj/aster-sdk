import subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class NativeFonts(unittest.TestCase):
    def test_hardware_selection_uses_published_native_metrics(self):
        source=(ROOT/'lib/aster/aster_text.h').read_text()
        code=source[source.index('typedef struct {'):source.index('/* Baseline step.')]
        code=code.replace('#include <aster/font_metrics.h>','#include "'+str(ROOT/'include/aster/font_metrics.h')+'"')
        with tempfile.TemporaryDirectory() as d:
            exe=str(Path(d)/'fonts')
            c='#include <stdint.h>\n#include <assert.h>\n'+code+'''
int main(void) {
 assert(aster_text_snap(0)==20);
 assert(aster_text_for_display(16,240,320)==12);
 assert(aster_text_for_display(20,240,320)==14);
 assert(aster_text_for_display(16,320,480)==16);
 assert(aster_text_for_display(20,320,480)==20);
 assert(aster_text_for_display(16,480,800)==24);
 assert(aster_text_for_display(20,480,800)==28);
 assert(aster_text_for_display(20,720,1280)==48);
 assert(aster_text_for_display(20,0,480)==20);
 for(int px=1;px<256;px++) {const aster_text_face_t *f=aster_text_face(px);assert(f->px==aster_text_snap(px) && f->line_h && f->asc);}
 return 0;
}
'''
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-x','c','-','-o',exe],input=c,text=True,check=True,capture_output=True)
            subprocess.run([exe],check=True)
