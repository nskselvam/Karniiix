import assert from 'node:assert/strict';
import { mkdtemp, writeFile, utimes, symlink, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createHash } from 'node:crypto';
import test from 'node:test';
import { startImageServer } from './live-images.mjs';

test('live service discovers later files, serves exact bytes, rejects writes and excludes symlinks', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'karnix-live-'));
  const server = await startImageServer(directory, 0);
  const url = `http://127.0.0.1:${server.address().port}`;
  try {
    assert.equal(await (await fetch(`${url}/index`)).text(), '');
    const bytes = Buffer.from('test-image-bytes');
    await writeFile(join(directory, 'new.png'), bytes);
    await utimes(join(directory, 'new.png'), new Date(0), new Date(0));
    await writeFile(join(directory, 'private.txt'), 'excluded');
    await symlink(join(directory, 'new.png'), join(directory, 'link.png'));
    const key = `${createHash('sha256').update(bytes).digest('hex')}.png`;
    assert.equal(await (await fetch(`${url}/index`)).text(), `${key}\n`);
    assert.deepEqual(Buffer.from(await (await fetch(`${url}/images/${key}`)).arrayBuffer()), bytes);
    assert.equal((await fetch(`${url}/index`, { method: 'POST' })).status, 405);
    assert.equal((await fetch(`${url}/private.txt`)).status, 404);
    assert.equal((await fetch(`${url}/images/not-an-image`)).status, 404);
    await rm(join(directory, 'new.png'));
    assert.equal(await (await fetch(`${url}/index`)).text(), '');
    assert.equal((await fetch(`${url}/images/${key}`)).status, 404);
  } finally {
    await new Promise(resolve => server.close(resolve));
    await rm(directory, { recursive: true });
  }
});

test('PGM, JPG and PNG inbox images are all published', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'karnix-formats-'));
  const server = await startImageServer(directory, 0);
  const url = `http://127.0.0.1:${server.address().port}`;
  try {
    for (const extension of ['pgm', 'jpg', 'png']) {
      const path = join(directory, `image.${extension}`);
      await writeFile(path, `format-${extension}`);
      await utimes(path, new Date(0), new Date(0));
    }
    const keys = (await (await fetch(`${url}/index`)).text()).trim().split('\n');
    assert.deepEqual(keys.map(key => key.split('.')[1]).sort(), ['jpg', 'pgm', 'png']);
    for (const key of keys) assert.equal((await fetch(`${url}/images/${key}`)).status, 200);
  } finally {
    await new Promise(resolve => server.close(resolve));
    await rm(directory, { recursive: true });
  }
});
