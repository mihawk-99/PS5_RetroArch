"""Native startup regression/performance checks, confined to temporary fixtures."""
from pathlib import Path
import json, os, shutil, subprocess, tempfile, time, unittest
ROOT=Path(__file__).resolve().parents[1]
PORT=ROOT
PUGIXML=ROOT/'build/pugixml-ps5/src/src'
DRIVER=r'''
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
static size_t comparisons;
static int count_compare(const char*a,const char*b){++comparisons;return strcmp(a,b);}
#define strcmp count_compare
#include "LIBRARY_SOURCE"
#undef strcmp
int main(int argc,char**argv){
 if(argc!=4)return 2; struct ps5_library lib={0};struct timespec a,b;
 clock_gettime(CLOCK_MONOTONIC,&a);
 if(ps5_library_load(&lib,argv[1],argv[2],argv[3]))return 3;
 clock_gettime(CLOCK_MONOTONIC,&b);
 uint64_t h=1469598103934665603ULL;
 for(size_t i=0;i<lib.game_count*sizeof(*lib.games);++i)h=(h^((unsigned char*)lib.games)[i])*1099511628211ULL;
 unsigned fav=0,played=0;for(size_t i=0;i<lib.game_count;++i){fav+=lib.games[i].favorite!=0;played+=lib.games[i].history!=0;}
 printf("{\"games\":%zu,\"systems\":%zu,\"favorites\":%u,\"history\":%u,\"digest\":\"%llx\",\"comparisons\":%zu,\"ms\":%.3f}\n",lib.game_count,lib.system_count,fav,played,(unsigned long long)h,comparisons,(b.tv_sec-a.tv_sec)*1000.0+(b.tv_nsec-a.tv_nsec)/1000000.0);
 ps5_library_free(&lib);return 0;
}
'''
ADAPTER=r'''
#include "library_ps5.h"
#include <cstdio>
int main(int argc,char**argv){if(argc!=6)return 2; ps5_esde_library_paths p{argv[1],argv[2],argv[3],argv[4],argv[5],nullptr,nullptr};char s[1024];int n=ps5_esde_write_library_to(&p,s,sizeof(s));puts(s);return n<0?1:0;}
'''

class Startup(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory();cls.build=Path(cls.temp.name);cls.exes={}
  if not (PUGIXML/'pugixml.cpp').exists(): raise unittest.SkipTest('Run tools/build-pugixml.sh first')
  baseline=cls.build/'baseline'
  for rel in ('src/ps5_library.c','frontends/es-de/ps5/library_ps5.cpp'):
   dest=baseline/rel;dest.parent.mkdir(parents=True,exist_ok=True)
   dest.write_bytes(subprocess.check_output(['git','show','7268510b12fe7461342e0cc4497c87a0910c1055:'+rel],cwd=ROOT))
  for name,source,adapter in [('before',baseline/'src/ps5_library.c',baseline/'frontends/es-de/ps5/library_ps5.cpp'),('after',PORT/'src/ps5_library.c',PORT/'frontends/es-de/ps5/library_ps5.cpp')]:
   c=cls.build/(name+'.c');c.write_text(DRIVER.replace('LIBRARY_SOURCE',str(source)))
   exe=cls.build/name;subprocess.run(['cc','-std=c11','-O2','-I'+str(PORT/'src'),str(c),'-o',str(exe)],check=True)
   obj=cls.build/(name+'.o');subprocess.run(['cc','-std=c11','-O2','-I'+str(PORT/'src'),'-c',str(source),'-o',str(obj)],check=True)
   cpp=cls.build/'adapter.cpp';cpp.write_text(ADAPTER);adapt=cls.build/(name+'-adapter')
   subprocess.run(['c++','-std=c++17','-O2','-I'+str(PORT/'src'),'-I'+str(PORT/'frontends/es-de/ps5'),'-I'+str(PORT/'frontends/es-de/ps5/include'),'-I'+str(PUGIXML),str(cpp),str(adapter),str(obj),str(PUGIXML/'pugixml.cpp'),'-o',str(adapt)],check=True)
   cls.exes[name]=(exe,adapt)
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def test_index_collisions_and_allocation_failure(self):
  exe=self.build/'index-test'
  subprocess.run(['cc','-std=c11','-g','-fsanitize=address,undefined','-fno-pie','-no-pie',str(ROOT/'tests/ps5_startup_index_test.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True,timeout=15)
 def test_resource_cache_scope_and_bounds(self):
  exe=self.build/'resource-test'
  subprocess.run(['c++','-std=c++17','-pthread','-g','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(ROOT/'frontends/es-de/ps5/include'),'-I'+str(PUGIXML),str(ROOT/'tests/esde_startup_resources_test.cpp'),str(PUGIXML/'pugixml.cpp'),'-o',str(exe)],check=True)
  with tempfile.TemporaryDirectory() as td:subprocess.run([str(exe),td],check=True,timeout=30)
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.base=Path(self.temp.name)
  for n in ('playlists/builtin','info','cores','data','content'): (self.base/n).mkdir(parents=True)
  self.args=[str(self.base/n) for n in ('playlists','info','cores')]
  self.reference=self.base/'reference.xml';self.reference.write_text('<systemList><system><name>nes</name><fullname>NES</fullname><platform>nes</platform><theme>nes</theme></system></systemList>')
 def run_reader(self,name):return json.loads(subprocess.check_output([str(self.exes[name][0]),*self.args],text=True))
 def fixture(self,n=10000):
  self.items=[{'path':f'smb://host/share/nes/Game {i:05}.zip','label':f'Game {i:05}','core_path':'DETECT','db_name':'Nintendo - Nintendo Entertainment System.lpl'} for i in range(n)]
  self.playlist=self.base/'playlists/Nintendo - Nintendo Entertainment System.lpl';self.playlist.write_text(json.dumps({'items':self.items}))
  (self.base/'playlists/builtin/content_favorites.lpl').write_text(json.dumps({'items':self.items[::3]}))
  (self.base/'playlists/builtin/content_history.lpl').write_text(json.dumps({'items':self.items[::100]}))
 def same(self,a,b):
  for key in ('games','systems','favorites','history','digest'):self.assertEqual(a[key],b[key],key)
 def test_large_library_equal_and_subquadratic(self):
  self.fixture();a=self.run_reader('before');b=self.run_reader('after');self.same(a,b)
  self.assertLess(b['comparisons'],a['comparisons']/20)
  print('Synthetic 10,000-game comparison counts:', a['comparisons'], '->', b['comparisons'])
 def test_duplicates_first_wins_and_history_missing_paths(self):
  self.fixture(1400)
  duplicates=[dict(g,label='Must not replace first') for g in self.items[::2]]
  duplicates.append(dict(self.items[0],path='smb://host/share/nes/Unique # %+é.zip'))
  (self.base/'playlists/Z duplicate.lpl').write_text(json.dumps({'items':duplicates}))
  self.same(self.run_reader('before'),self.run_reader('after'));self.assertEqual(self.run_reader('after')['games'],1401)
 def adapter(self,name,data):
  subprocess.run([str(self.exes[name][1]),*self.args,str(self.reference),str(data)],check=True,stdout=subprocess.DEVNULL)
 def test_noop_writes_and_live_favorite_history_changes(self):
  self.fixture(150);data=self.base/'data';old=self.base/'old';old.mkdir()
  self.adapter('before',old);self.adapter('after',data)
  for p in old.rglob('*'):
   if p.is_file():self.assertEqual(p.read_bytes(),(data/p.relative_to(old)).read_bytes())
  before={str(p.relative_to(data)):(p.read_bytes(),p.stat().st_mtime_ns) for p in data.rglob('*') if p.is_file()}
  time.sleep(.02);self.adapter('after',data)
  self.assertEqual(before,{str(p.relative_to(data)):(p.read_bytes(),p.stat().st_mtime_ns) for p in data.rglob('*') if p.is_file()})
  # ES-DE manual favorite survives; RA add/remove and history remain synchronized.
  g=data/'gamelists/nes/gamelist.xml';text=g.read_text();text=text.replace('<name>Game 00001</name>','<name>Custom name</name><favorite>true</favorite><desc>Keep me</desc>')
  g.write_text(text);(old/'gamelists/nes/gamelist.xml').write_text(text)
  (self.base/'playlists/builtin/content_favorites.lpl').write_text(json.dumps({'items':self.items[1:6]}))
  (self.base/'playlists/builtin/content_history.lpl').write_text(json.dumps({'items':[self.items[9],self.items[1]]}))
  self.adapter('before',old);self.adapter('after',data)
  for p in old.rglob('*'):
   if p.is_file():self.assertEqual(p.read_bytes(),(data/p.relative_to(old)).read_bytes())
  self.assertIn('Keep me',g.read_text());self.assertFalse(list(data.rglob('*.tmp')))
 def test_add_remove_and_empty_library(self):
  self.fixture(9);data=self.base/'data';self.adapter('after',data)
  for items in (self.items[:2],self.items+[dict(self.items[0],path='smb://host/share/nes/New.zip')],[]):
   self.playlist.write_text(json.dumps({'items':items}));self.same(self.run_reader('before'),self.run_reader('after'))
   self.adapter('after',data)

if __name__=='__main__':unittest.main()
