"""Exercise actual portable directory callback and preview cleanup."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
s = (root/'src/skins.c').read_text()
callback = s[s.index('struct skin_scan_context {'):s.index('\nvoid skins_scan(void)')]
stub = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SKIN_NAME_MAX 48
enum skin_category_type {SKIN_RIFLE};
struct kv6_t {void* voxels; bool flip_yaw;};
struct skin_entry {char name[48],suffix[48];struct kv6_t* previews;};
struct skin_model_def {const char* basename;};
struct skin_category {struct skin_model_def models[1];int count,model_count;struct skin_entry* entries;};
#define log_warn(...) ((void)0)
bool skins_weapon_category(enum skin_category_type c) {(void)c;return true;}
int flip_find(int c,const char* s) {(void)c;return strcmp(s,"RPG7") ? -1 : 0;}
int skin_reload_model(enum skin_category_type c,struct skin_category*cat,int m,struct kv6_t*k,const char*p) {
 (void)c;(void)cat;(void)m;if(strstr(p,"Broken"))return -1;k->voxels=malloc(1);return 0;
}
'''
checks = r'''
int main(void) {
 struct skin_category c={.models={{"semi"}},.count=0,.model_count=1};
 struct skin_scan_context ctx={&c,SKIN_RIFLE,1};c.entries=malloc(sizeof(struct skin_entry));
 skin_scan_file("semi(RPG7).kv6",&ctx);assert(c.count==1 && !strcmp(c.entries[0].suffix,"RPG7") && c.entries[0].previews[0].flip_yaw);
 skin_scan_file("semi(Example Gun).kv6",&ctx);assert(c.count==2 && !strcmp(c.entries[1].name,"Example Gun"));
 skin_scan_file("semi(Default).kv6",&ctx);assert(c.count==3 && !strcmp(c.entries[2].name,"Default (custom)") && !strcmp(c.entries[2].suffix,"Default"));
 skin_scan_file("semi(Broken).kv6",&ctx);skin_scan_file("semi().kv6",&ctx);skin_scan_file("semi(a)b).kv6",&ctx);
 skin_scan_file("semitracer(RPG7).kv6",&ctx);skin_scan_file("semi(X).kv6.bak",&ctx);assert(c.count==3);
 for(int i=0;i<c.count;i++){free(c.entries[i].previews[0].voxels);free(c.entries[i].previews);}free(c.entries);
 puts("portable skin discovery checks passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(stub+callback+checks);exe=Path(d)/'test'
 subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
