"""Bounded catalogue parsing across every network split, escapes and URL bytes."""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class Catalogue(unittest.TestCase):
 def test_streaming_json(self):
  code=r'''
#include <assert.h>
#include "catalog.h"
int main(void){
 catalog_reader r={0};char title[128],file[192],url[512];unsigned objects=0;
 const char *json="[{\"description\":\"brace } and \\\" quote\",\"title\":\"A \\u4e2d\\u6587\",\"filename\":\"a b&c.mp3\"},{\"title\":\"B\",\"filename\":\"b.mp3\"}]";
 for(const char *p=json;*p;++p){int n=catalog_byte(&r,*p);assert(n>=0);if(!n)continue;++objects;assert(catalog_field(r.object,"title",title,sizeof(title)));assert(catalog_field(r.object,"filename",file,sizeof(file)));if(objects==1){assert(!strcmp(title,"A 中文"));assert(!strcmp(file,"a b&c.mp3"));}}
 assert(objects==2&&!r.depth);uint32_t id;assert(catalog_uint("{\"nested\":{\"id\":99},\"id\":17}","id",&id)&&id==17);
 assert(!catalog_field("{\"title\":\"abcdef\"}","title",title,3));
 assert(!catalog_field("{\"title\":\"\\uD800\"}","title",title,sizeof(title)));
 assert(!catalog_field("{\"filename\":\"\\u0000.mp3\"}","filename",file,sizeof(file)));
 memset(&r,0,sizeof(r));assert(!catalog_byte(&r,'['));assert(!catalog_byte(&r,'{'));
 int result=0;for(unsigned i=0;i<sizeof(r.object);++i){result=catalog_byte(&r,'a');if(result<0)break;}assert(result<0);return 0;
}'''
  with tempfile.TemporaryDirectory() as d:
   src=pathlib.Path(d)/'catalog.c';src.write_text(code);exe=str(pathlib.Path(d)/'catalog')
   subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-g','-I'+str(ROOT/'apps/online-music/src'),str(src),'-o',exe],check=True)
   subprocess.run([exe],check=True,timeout=10)
