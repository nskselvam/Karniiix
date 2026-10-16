import assert from 'node:assert/strict';
import { readdir, readFile, mkdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { gzipSync } from 'node:zlib';
import { gzipMembers, satisfies } from './macos-build.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const source = fileURLToPath(new URL('../../cavOS/', import.meta.url));
let hasOriginal = true;
try {
  await readdir(source);
} catch (error) {
  if (error.code !== 'ENOENT') throw error;
  hasOriginal = false;
}

async function files(directory, prefix = '') {
  const entries = await readdir(join(directory, prefix), { withFileTypes: true });
  const result = [];
  for (const entry of entries) {
    const path = join(prefix, entry.name);
    if (entry.isDirectory()) result.push(...await files(directory, path));
    else result.push(path);
  }
  return result.sort();
}

test('every cavOS source file is imported; only documented integration differs', {
  skip: !hasOriginal && 'Original checkout absent; standalone OS does not require it',
}, async () => {
  const originals = await files(join(source, 'src'));
  assert.ok(originals.length > 0);
  for (const path of originals) {
    if (path === join('kernel', 'Makefile')) {
      const makefile = await readFile(join(root, 'src', path), 'utf8');
      assert.match(makefile, /exit \$\$status/);
      continue;
    }
    if (path === join('kernel', 'entry', 'kernel.c')) {
      assert.match(await readFile(join(root, 'src', path), 'utf8'), /KARNIX - Image and research workspace/);
      continue;
    }
    if (path === join('kernel', 'filesystems', 'ext2', 'ext2_controller.c')) {
      const controller = await readFile(join(root, 'src', path), 'utf8');
      assert.match(controller, /Ext2 block zero is a sparse hole/);
      continue;
    }
    assert.deepEqual(await readFile(join(root, 'src', path)),
      await readFile(join(source, 'src', path)), path);
  }
});

test('all supporting cavOS files are present, including hidden desktop config', {
  skip: !hasOriginal && 'Original checkout absent; standalone OS does not require it',
}, async () => {
  for (const directory of ['target', 'tools', 'patches', 'docs', '.vscode', '.github']) {
    for (const path of await files(join(source, directory))) {
      await readFile(join(root, directory, path));
    }
  }
  assert.deepEqual(await readFile(join(root, 'LICENSE')),
    await readFile(join(source, 'LICENSE')));
});

test('primary entry point works from either repository root or OS directory', () => {
  for (const args of [['-C', root, 'help'], ['-f', join(root, 'Makefile'), 'help']]) {
    const result = spawnSync('make', args, { cwd: dirname(root), encoding: 'utf8' });
    assert.equal(result.status, 0, result.stderr);
    assert.match(result.stdout, /cavOS-based kernel and X11\/TWM/);
  }
});

test('shell startup and integration scripts have valid Bash syntax', () => {
  const result = spawnSync('bash', ['-n', join(root, 'target/root/.bashrc')], {
    encoding: 'utf8',
  });
  assert.equal(result.status, 0, result.stderr);
  for (const path of ['tools/check-host.sh', 'tools/userland/ports.sh']) {
    const check = spawnSync('bash', ['-n', join(root, path)], { encoding: 'utf8' });
    assert.equal(check.status, 0, check.stderr);
  }
});

test('macOS selects the native builder instead of Linux chroot tools', {
  skip: process.platform !== 'darwin',
}, () => {
  const result = spawnSync('make', ['-n', '-C', root, 'disk'], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /macos-build\.mjs/);
  assert.doesNotMatch(result.stdout, /ports\.sh|make_disk\.sh/);
});

test('APK parser preserves all three independent gzip streams and rejects invalid input', () => {
  const contents = ['signature', 'control', 'data'];
  const members = gzipMembers(Buffer.concat(contents.map(value => gzipSync(value))));
  assert.deepEqual(members.map(member => member.data.toString()), contents);
  assert.throws(() => gzipMembers(Buffer.from('invalid')), /Invalid APK gzip stream/);
});

test('package resolution enforces versions and virtual-library requirements', () => {
  assert.equal(satisfies('musl=1.2.5-r11', 'musl>=1.2.5-r10'), true);
  assert.equal(satisfies('musl=1.2.5-r9', 'musl>=1.2.5-r10'), false);
  assert.equal(satisfies('so:libssl.so.3=3', 'so:libssl.so.3'), true);
  assert.equal(satisfies('example=2.0-r0', 'example=1.0-r0'), false);
  assert.equal(satisfies('example=1.2.3-r0', 'example~1.2'), true);
});

test('actual Ext2 reader preserves sparse holes, later blocks, and unaligned reads', async () => {
  const controller = await readFile(join(root, 'src/kernel/filesystems/ext2/ext2_controller.c'), 'utf8');
  const start = controller.indexOf('size_t ext2ReadInner(');
  const end = controller.indexOf('\nsize_t ext2Write(', start);
  assert.ok(start >= 0 && end > start);
  const harness = `
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define BLOCK_SIZE 4096
#define SECTOR_SIZE 512
#define MIN(a,b) ((a)<(b)?(a):(b))
#define EXT2_PTR(p) ((Ext2 *)(p))
#define EXT2_DIR_PTR(p) ((Ext2OpenFd *)(p))
#define BLOCK_TO_LBA(e,a,b) ((b)*8)
typedef struct {size_t blockSize;} Ext2;
typedef struct {size_t ptr; void *globalObject;} Ext2OpenFd;
typedef struct {void *fsInfo;} Mount;
typedef struct {Mount *mountPoint; void *dir;} OpenFile;
size_t DivRoundUp(size_t a,size_t b){return (a+b-1)/b;}
void *VirtualAllocate(size_t n){return malloc(n*BLOCK_SIZE);}
uint32_t *ext2BlockChain(Ext2 *e,Ext2OpenFd *f,size_t start,size_t count){
  (void)e;(void)f; uint32_t pattern[]={3,0,4,5,0,7};
  uint32_t *out=calloc(count+1,sizeof(uint32_t));
  for(size_t i=0;i<count && start+i<6;i++)out[i]=pattern[start+i];
  return out;
}
void getDiskBytes(void *out,size_t lba,size_t sectors){
  for(size_t i=0;i<sectors*SECTOR_SIZE;i++)((uint8_t *)out)[i]=(lba*512+i)/4096;
}
void ext2CacheAddSecurely(Mount *m,void *o,void *p,size_t start,size_t count){
  (void)m;(void)o;(void)start;(void)count;free(p);
}
${controller.slice(start, end)}
int main(void){
  Ext2 e={4096};Mount mount={&e};Ext2OpenFd dir={0};OpenFile file={&mount,&dir};
  uint8_t output[6*4096];uint8_t pattern[]={3,0,4,5,0,7};
  for(size_t offset=0;offset<4096;offset+=997){
    dir.ptr=offset;size_t length=sizeof(output)-offset;
    assert(ext2ReadInner(&file,output,length)==length);
    for(size_t i=0;i<length;i++)assert(output[i]==pattern[(i+offset)/4096]);
    assert(dir.ptr==sizeof(output));
  }
  return 0;
}`;
  await mkdir(join(root, 'build'), { recursive: true });
  const binary = join(root, 'build/ext2-sparse-test');
  const compile = spawnSync('cc', ['-std=c11', '-Wall', '-Wextra', '-Werror', '-x', 'c', '-', '-o', binary], {
    input: harness, encoding: 'utf8',
  });
  assert.equal(compile.status, 0, compile.stderr);
  const result = spawnSync(binary, [], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
});
