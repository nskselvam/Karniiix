import { createServer } from 'node:http';
import { createHash } from 'node:crypto';
import { mkdir, readdir, readFile, stat } from 'node:fs/promises';
import { extname, join } from 'node:path';

const extensions = new Set(['.jpg', '.jpeg', '.png', '.pgm', '.ppm', '.bmp', '.gif', '.tif', '.tiff', '.webp']);

export async function startImageServer(directory, port = 18765) {
  await mkdir(directory, { recursive: true });
  const images = new Map();
  async function scan(path = directory) {
    const found = new Map();
    for (const entry of await readdir(path, { withFileTypes: true })) {
      const full = join(path, entry.name);
      if (entry.isDirectory()) {
        for (const [key, value] of await scan(full)) found.set(key, value);
      } else if (entry.isFile() && extensions.has(extname(entry.name).toLowerCase())) {
        const before = await stat(full);
        if (Date.now() - before.mtimeMs < 1500) continue;
        const bytes = await readFile(full);
        const after = await stat(full);
        if (before.size !== after.size || before.mtimeMs !== after.mtimeMs) continue;
        const key = `${createHash('sha256').update(bytes).digest('hex')}${extname(entry.name).toLowerCase()}`;
        found.set(key, bytes);
      }
    }
    return found;
  }
  const server = createServer(async (request, response) => {
    try {
      if (request.method !== 'GET') {
        response.writeHead(405).end('Read-only service\n');
        return;
      }
      if (request.url === '/index') {
        const found = await scan();
        images.clear();
        for (const [key, bytes] of found) images.set(key, bytes);
        response.writeHead(200, { 'Content-Type': 'text/plain', 'Cache-Control': 'no-store' });
        response.end([...images.keys()].join('\n') + (images.size ? '\n' : ''));
        return;
      }
      const match = /^\/images\/([a-f0-9]{64}\.[a-z]+)$/.exec(request.url);
      const bytes = match && images.get(match[1]);
      if (!bytes) {
        response.writeHead(404).end('Image not found\n');
        return;
      }
      response.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': bytes.length });
      response.end(bytes);
    } catch (error) {
      console.error(`Karnix image service: ${error.message}`);
      response.writeHead(500).end('Image scan failed; check the host terminal\n');
    }
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, '127.0.0.1', resolve);
  });
  return server;
}
