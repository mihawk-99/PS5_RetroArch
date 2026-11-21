#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <assert.h>
#include <string.h>
static int fail_index;
static void *maybe_calloc(size_t n,size_t s){return fail_index&&s==sizeof(size_t)?NULL:calloc(n,s);}
#define calloc maybe_calloc
#include "../src/ps5_library.c"
#undef calloc
int main(void){
 struct ps5_library lib={0};lib.games=calloc(300,sizeof(*lib.games));assert(lib.games);
 struct game_paths index={0};unsigned n=0;
 // Deliberately collide in the initial bucket, then exercise resize/realloc-safe indices.
 for(unsigned candidate=0;n<200;candidate++){
  char key[40];snprintf(key,sizeof(key),"game-%u",candidate);
  if((path_hash(key)&127)!=0)continue;
  strcpy(lib.games[n].path,key);index_game(&index,&lib,n);lib.game_count=++n;
 }
 for(size_t i=0;i<lib.game_count;i++)assert(indexed_game(&lib,&index,lib.games[i].path)==&lib.games[i]);
 assert(!indexed_game(&lib,&index,"not present"));free(index.slots);
 index=(struct game_paths){0};fail_index=1;index_game(&index,&lib,0);assert(index.disabled);
 for(size_t i=0;i<lib.game_count;i++)assert(indexed_game(&lib,&index,lib.games[i].path)==&lib.games[i]);
 assert(!indexed_game(&lib,&index,"not present"));
 fail_index=0;index=(struct game_paths){0};for(size_t i=0;i<64;i++)index_game(&index,&lib,i);
 fail_index=1;index_game(&index,&lib,64);assert(index.disabled&&!index.slots);
 assert(indexed_game(&lib,&index,lib.games[199].path)==&lib.games[199]);
 free(lib.games);puts("PASS: collision probing, rehashing, absent paths and allocation-failure fallback");
}
