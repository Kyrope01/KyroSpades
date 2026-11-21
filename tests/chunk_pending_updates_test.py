"""Exercise the real pending-chunk drain with more than one frame's budget."""
from pathlib import Path
from subprocess import run
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[1]
source = (root / 'src/chunk.c').read_text()
a = source.index('#define CHUNK_QUEUE_MAX_PENDING')
b = source.index('\n}', source.index('void chunk_queue_blocks()', a)) + 2
for name in ('naive', 'textured'):
    mesher = source.split(f'void chunk_generate_{name}(', 1)[1].split('\n}', 1)[0]
    assert 'map_read_lock();' not in mesher and 'map_read_unlock();' not in mesher

harness = r'''
#include <assert.h>
#include "hashtable.h"
#include "utils.h"
struct chunk_work_packet { int id, gen; bool bulk; };
static HashTable chunk_block_queue;
static int chunk_block_queue_lock, chunk_work_queue;
static int chunk_gen = 7, sent, seen[65];
static bool locked;
static void chunk_lock_lock(int* lock) { (void)lock; assert(!locked); locked = true; }
static void chunk_lock_unlock(int* lock) { (void)lock; assert(locked); locked = false; }
static void channel_put(int* queue, struct chunk_work_packet* work) {
    (void)queue;
    assert(!locked && work->gen == chunk_gen && !work->bulk);
    assert(work->id >= 0 && work->id < 65 && !seen[work->id]++);
    ++sent;
}
'''
checks = r'''
int main(void) {
    ht_setup(&chunk_block_queue, sizeof(int), sizeof(struct chunk_work_packet), 64);
    for(int i = 0; i < 65; ++i) {
        struct chunk_work_packet work = {.id = i, .bulk = true};
        ht_insert(&chunk_block_queue, &i, &work);
    }
    chunk_queue_blocks();
    assert(sent == 64 && chunk_block_queue.size == 1);
    ++chunk_gen;
    chunk_queue_blocks();
    assert(sent == 65 && ht_is_empty(&chunk_block_queue));
    ht_destroy(&chunk_block_queue);
}
'''
with TemporaryDirectory() as tmp:
    src = Path(tmp) / 'queue.c'
    exe = src.with_suffix('')
    src.write_text(harness + source[a:b] + checks)
    run(['cc', '-std=gnu11', '-I', str(root / 'src'), str(src),
         str(root / 'src/hashtable.c'), str(root / 'src/utils.c'), '-lm', '-o', str(exe)], check=True)
    run([str(exe)], check=True)
