import assert from 'node:assert/strict';
import { mkdtemp, writeFile, utimes, symlink, rm, readFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createHash } from 'node:crypto';
import test from 'node:test';
import { startImageServer } from './live-images.mjs';

test('slim desktop excludes browser packages, launchers and menu entries', async () => {
  const menu = await readFile(new URL('../target/root/.twmrc', import.meta.url), 'utf8');
  const welcome = await readFile(new URL('../target/root/karnix-welcome.sh', import.meta.url), 'utf8');
  const builder = await readFile(new URL('macos-build.mjs', import.meta.url), 'utf8');
  const ports = await readFile(new URL('userland/ports.sh', import.meta.url), 'utf8');
  for (const contents of [menu, welcome, builder, ports]) {
    assert.doesNotMatch(contents, /netsurf|karnix-browser|Google Search|\b['"]links['"]/i);
  }
  await assert.rejects(readFile(new URL('../target/root/karnix-browser.sh', import.meta.url)),
    { code: 'ENOENT' });
});

test('incoming images sync without opening a viewer; menu retains manual launch', async () => {
  const guest = new URL('../target/root/', import.meta.url);
  const sync = await readFile(new URL('karnix-live-images.sh', guest), 'utf8');
  const startup = await readFile(new URL('.xinitrc', guest), 'utf8');
  const menu = await readFile(new URL('.twmrc', guest), 'utf8');
  assert.match(sync, /echo "Received image: \$name"/);
  assert.doesNotMatch(sync, /\b(feh|thunar)\b/);
  assert.doesNotMatch(startup, /\b(feh|thunar)\b/);
  assert.match(menu, /"Incoming Images"\s+f\.exec "exec feh .*\/root\/incoming-images/);
});

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
