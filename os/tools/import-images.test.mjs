import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readdir, readFile } from 'node:fs/promises';
import { extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const root = fileURLToPath(new URL('../', import.meta.url));
const source = join(root, '../karnix_spine_edge/data');
const destination = join(root, 'target/root/medical-images');
const extensions = new Set(['.jpg', '.jpeg', '.png', '.pgm', '.ppm', '.bmp', '.gif', '.tif', '.tiff', '.webp']);

async function imagePaths(relative = '') {
  const result = [];
  for (const entry of await readdir(join(source, relative), { withFileTypes: true })) {
    const path = join(relative, entry.name);
    if (entry.isDirectory()) result.push(...await imagePaths(path));
    else if (entry.isFile() && extensions.has(extname(path).toLowerCase())) result.push(path);
  }
  return result;
}

test('every source image is copied unchanged and covered by the manifest', async () => {
  const manifest = JSON.parse(await readFile(join(destination, 'manifest.json'), 'utf8'));
  const paths = await imagePaths();
  assert.ok(paths.length > 0);
  assert.equal(manifest.count, paths.length);
  assert.deepEqual(manifest.images.map(image => image.path).sort(), paths.sort());
  const filelist = (await readFile(join(root, 'target/root/medical-images.list'), 'utf8')).trim().split('\n');
  assert.deepEqual(filelist, manifest.images.map(image => `/root/medical-images/${image.path}`));
  for (const image of manifest.images) {
    const original = await readFile(join(source, image.path));
    const copied = await readFile(join(destination, image.path));
    assert.deepEqual(copied, original, image.path);
    assert.equal(copied.length, image.bytes, image.path);
    assert.equal(createHash('sha256').update(copied).digest('hex'), image.sha256, image.path);
  }
});
