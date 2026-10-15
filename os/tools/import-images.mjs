import { createHash } from 'node:crypto';
import { copyFile, mkdir, readdir, readFile, stat, writeFile } from 'node:fs/promises';
import { dirname, extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const source = join(root, '../karnix_spine_edge/data');
const destination = join(root, 'target/root/medical-images');
const extensions = new Set(['.jpg', '.jpeg', '.png', '.pgm', '.ppm', '.bmp', '.gif', '.tif', '.tiff', '.webp']);
const images = [];

async function visit(relative = '') {
  const entries = await readdir(join(source, relative), { withFileTypes: true });
  for (const entry of entries.sort((a, b) => a.name.localeCompare(b.name))) {
    const path = join(relative, entry.name);
    if (entry.isDirectory()) {
      await visit(path);
    } else if (entry.isFile() && extensions.has(extname(entry.name).toLowerCase())) {
      const from = join(source, path);
      const to = join(destination, path);
      const data = await readFile(from);
      const sha256 = createHash('sha256').update(data).digest('hex');
      await mkdir(dirname(to), { recursive: true });
      let matches = false;
      try {
        matches = (await stat(to)).size === data.length &&
          createHash('sha256').update(await readFile(to)).digest('hex') === sha256;
      } catch (error) {
        if (error.code !== 'ENOENT') throw error;
      }
      if (!matches) await copyFile(from, to);
      images.push({ path: path.split('\\').join('/'), bytes: data.length, sha256 });
    }
  }
}

try {
  await mkdir(destination, { recursive: true });
  await visit();
  if (!images.length) throw new Error(`No supported image files found in ${source}`);
  await writeFile(join(destination, 'manifest.json'), `${JSON.stringify({
    source: 'karnix_spine_edge/data',
    count: images.length,
    images,
  }, null, 2)}\n`);
  await writeFile(join(root, 'target/root/medical-images.list'),
    `${images.map(image => `/root/medical-images/${image.path}`).join('\n')}\n`);
  console.log(`Imported ${images.length} images (${(images.reduce((sum, image) => sum + image.bytes, 0) / 1024 / 1024).toFixed(1)} MiB). Originals preserved.`);
} catch (error) {
  console.error(`Image import failed: ${error.message}`);
  process.exitCode = 1;
}
