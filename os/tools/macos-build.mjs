import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { cp, mkdir, readFile, writeFile, open, stat } from 'node:fs/promises';
import { join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { inflateRawSync } from 'node:zlib';

const root = fileURLToPath(new URL('../', import.meta.url));
const build = join(root, 'build/macos');
const downloads = join(build, 'downloads');
const guest = join(build, 'root');
const alpine = 'https://dl-cdn.alpinelinux.org/alpine/v3.21';
const packages = [
  'bash', 'coreutils', 'grep', 'sed', 'findutils', 'curl', 'tzdata',
  'xorg-server', 'xinit', 'xf86-input-evdev', 'xf86-video-fbdev',
  'twm', 'xsetroot', 'st', 'nedit', 'thunar', 'feh', 'mesa-dri-gallium',
  'mesa-utils', 'font-dejavu', 'font-misc-misc', 'font-cursor-misc',
  'xkeyboard-config', 'xauth', 'dbus', 'htop',
];

function run(command, args, options = {}) {
  const result = spawnSync(command, args, { cwd: root, stdio: 'inherit', ...options });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${command} failed (${result.status})`);
  return result.stdout;
}

async function download(url, path) {
  try {
    if ((await stat(path)).size > 0) return;
  } catch (error) {
    if (error.code !== 'ENOENT') throw error;
  }
  run('curl', ['--fail', '--location', '--retry', '3', '--retry-all-errors', '--silent', '--show-error',
    url, '-o', `${path}.partial`]);
  const { rename } = await import('node:fs/promises');
  await rename(`${path}.partial`, path);
}

// APK v2 comprises separately compressed signature, control, and data streams.
export function gzipMembers(buffer) {
  const members = [];
  let offset = 0;
  while (offset < buffer.length) {
    const start = offset;
    if (buffer[offset] !== 0x1f || buffer[offset + 1] !== 0x8b ||
        buffer[offset + 2] !== 8) throw new Error('Invalid APK gzip stream');
    const flags = buffer[offset + 3];
    offset += 10;
    if (flags & 4) offset += 2 + buffer.readUInt16LE(offset);
    for (const flag of [8, 16]) {
      if (flags & flag) {
        const end = buffer.indexOf(0, offset);
        if (end < 0) throw new Error('Invalid gzip header');
        offset = end + 1;
      }
    }
    if (flags & 2) offset += 2;
    const decoded = inflateRawSync(buffer.subarray(offset), { info: true });
    offset += decoded.engine.bytesWritten + 8;
    if (offset > buffer.length) throw new Error('Truncated APK stream');
    members.push({ compressed: buffer.subarray(start, offset), data: decoded.buffer });
  }
  return members;
}

export function tarEntry(buffer, name) {
  for (let offset = 0; offset + 512 <= buffer.length;) {
    const header = buffer.subarray(offset, offset + 512);
    const entryName = header.subarray(0, 100).toString().replace(/\0.*$/, '');
    const size = parseInt(header.subarray(124, 136).toString().replace(/\0.*$/, '').trim() || '0', 8);
    if (!Number.isFinite(size)) throw new Error('Invalid tar size');
    if (entryName === name) return buffer.subarray(offset + 512, offset + 512 + size);
    offset += 512 + Math.ceil(size / 512) * 512;
  }
  throw new Error(`Missing archive entry: ${name}`);
}

async function prepareKernel() {
  const archive = join(downloads, 'uacpi-3.1.0.tar.gz');
  await download('https://github.com/uACPI/uACPI/archive/refs/tags/3.1.0.tar.gz', archive);
  if (createHash('sha256').update(await readFile(archive)).digest('hex') !==
      '1e8279ca5cba4b98cc017bf83ac960e08504aee5c990395501f129addd8fe1c7') {
    throw new Error('uACPI archive checksum mismatch');
  }
  run('tar', ['-xzf', archive, '-C', downloads]);
  await cp(join(downloads, 'uACPI-3.1.0/source'), join(root, 'src/kernel/acpi/uacpi'), { recursive: true });
  await cp(join(downloads, 'uACPI-3.1.0/include/uacpi'), join(root, 'src/kernel/include/uacpi'), { recursive: true });
  run('make', ['-C', join(root, 'src/kernel'), '-j6',
    'COMPILER=x86_64-elf-gcc', 'LINKER=x86_64-elf-ld',
    'CFLAGS=-std=gnu11 -m64 -g -c -ffreestanding -Wall -Werror -fcommon -Iinclude/ -Inetworking/lwip/include/ -isystem ../../target/usr/include -fPIE -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -fno-stack-protector -fno-stack-check -fno-lto']);
}

export function satisfies(provide, requirement) {
  const parse = value => /^([^<>=~]+)([<>=~]+)?(.*)$/.exec(value);
  const [, name, operator, version] = parse(requirement);
  const [, offeredName, , offeredVersion] = parse(provide);
  if (name !== offeredName) return false;
  if (!operator) return true;
  if (!offeredVersion) return false;
  if (operator === '=') return offeredVersion === version;
  if (operator === '~') return offeredVersion === version ||
    offeredVersion.startsWith(`${version}.`) || offeredVersion.startsWith(`${version}-`);
  if (/_?(alpha|beta|pre|rc)/.test(offeredVersion + version)) {
    throw new Error(`Unsupported prerelease constraint: ${provide} / ${requirement}`);
  }
  const result = offeredVersion.localeCompare(version, 'en', { numeric: true });
  if (operator === '>=') return result >= 0;
  if (operator === '>') return result > 0;
  if (operator === '<=') return result <= 0;
  if (operator === '<') return result < 0;
  throw new Error(`Unsupported package constraint: ${requirement}`);
}

async function prepareUserspace() {
  const entries = [];
  for (const repo of ['main', 'community', 'edge/community', 'edge/testing']) {
    const index = join(downloads, `${repo.replace('/', '-')}-APKINDEX.tar.gz`);
    const base = repo.startsWith('edge/') ?
      `https://dl-cdn.alpinelinux.org/alpine/${repo}` : `${alpine}/${repo}`;
    await download(`${base}/x86_64/APKINDEX.tar.gz`, index);
    const content = run('tar', ['-xOf', index, 'APKINDEX'], { stdio: ['ignore', 'pipe', 'inherit'], encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
    for (const block of content.trim().split('\n\n')) {
      const fields = Object.fromEntries(block.split('\n').map(line => [line[0], line.slice(2)]));
      if (fields.P) entries.push({ ...fields, repo, base });
    }
  }
  const providers = new Map();
  for (const entry of entries) {
    for (const provide of [entry.P, ...(entry.p || '').split(' ').filter(Boolean)]) {
      const name = provide.split(/[=<>~]/)[0];
      const list = providers.get(name) || [];
      list.push(entry);
      providers.set(name, list);
    }
  }
  const selected = new Map();
  const requirements = [];
  const provides = entry => [`${entry.P}=${entry.V}`, ...(entry.p || '').split(' ').filter(Boolean)];
  function resolve(dependency) {
    requirements.push(dependency);
    if (dependency.startsWith('!')) return;
    const name = dependency.split(/[=<>~]/)[0];
    const candidates = providers.get(name)?.filter(entry =>
      provides(entry).some(provide => satisfies(provide, dependency)));
    if (!candidates?.length) throw new Error(`Unresolved Alpine dependency: ${dependency}`);
    // A single stable repository snapshot supplies the versioned dependency set.
    const entry = candidates.find(candidate => selected.has(candidate.P)) ||
      candidates.find(candidate => candidate.P === name) ||
      [...candidates].sort((a, b) => Number(b.k || 0) - Number(a.k || 0))[0];
    if (selected.has(entry.P)) {
      if (selected.get(entry.P).V !== entry.V) {
        throw new Error(`Conflicting versions for ${entry.P}: ${selected.get(entry.P).V} / ${entry.V}`);
      }
      return;
    }
    selected.set(entry.P, entry);
    for (const child of (entry.D || '').split(' ').filter(Boolean)) resolve(child);
  }
  for (const name of packages) resolve(name);
  for (const requirement of requirements) {
    const conflict = requirement.startsWith('!');
    const matched = [...selected.values()].some(entry =>
      provides(entry).some(provide => satisfies(provide, conflict ? requirement.slice(1) : requirement)));
    if (conflict ? matched : !matched) throw new Error(`Unsatisfied package constraint: ${requirement}`);
  }
  console.log(`Preparing ${selected.size} Alpine x86_64 packages without chroot or sudo.`);
  await mkdir(guest, { recursive: true });
  for (const entry of selected.values()) {
    const filename = `${entry.P}-${entry.V}.apk`;
    const path = join(downloads, filename);
    await download(`${entry.base}/x86_64/${filename}`, path);
    const buffer = await readFile(path);
    if (buffer.length !== Number(entry.S)) throw new Error(`Package size mismatch: ${filename}`);
    const members = gzipMembers(buffer);
    if (members.length !== 3) throw new Error(`Unsupported APK format: ${filename}`);
    const checksum = `Q1${createHash('sha1').update(members[1].compressed).digest('base64')}`;
    if (checksum !== entry.C) throw new Error(`Package control checksum mismatch: ${filename}`);
    const info = tarEntry(members[1].data, '.PKGINFO').toString();
    const datahash = /^datahash = ([a-f0-9]{64})$/m.exec(info)?.[1];
    if (!datahash || createHash('sha256').update(members[2].compressed).digest('hex') !== datahash) {
      throw new Error(`Package data checksum mismatch: ${filename}`);
    }
    // Only extract data: package installation scripts are Linux programs, not host scripts.
    const dataPath = join(build, 'package-data.tar');
    await writeFile(dataPath, members[2].data);
    run('tar', ['-xf', dataPath, '-C', guest, '--no-same-owner']);
  }
  await cp(join(root, 'target'), guest, { recursive: true });
  for (const directory of ['dev', 'proc', 'sys', 'tmp', 'run', 'var/log', 'var/tmp', 'boot/limine', 'boot/EFI/BOOT']) {
    await mkdir(join(guest, directory), { recursive: true });
  }
  await writeFile(join(guest, 'etc/hostname'), 'karnix\n');
  await writeFile(join(guest, 'etc/resolv.conf'), 'nameserver 10.0.2.3\n');
  await writeFile(join(build, 'packages.json'), JSON.stringify([...selected.values()].map(({ P, V, C }) => ({ name: P, version: V, checksum: C })), null, 2));
}

async function makeDisk() {
  const prefix = run('brew', ['--prefix', 'e2fsprogs'], { stdio: ['ignore', 'pipe', 'inherit'], encoding: 'utf8' }).trim();
  const limineArchive = join(downloads, 'limine-v10.tar.gz');
  await download('https://github.com/limine-bootloader/limine/archive/refs/heads/v10.x-binary.tar.gz', limineArchive);
  if (createHash('sha256').update(await readFile(limineArchive)).digest('hex') !==
      '8365711d7b60848401ec5a76d1794f8912f91b477375716d2ba5dea39605234a') {
    throw new Error('Limine archive changed; review and update the pinned checksum before building');
  }
  run('tar', ['-xzf', limineArchive, '-C', downloads]);
  const bootloader = join(downloads, 'Limine-10.x-binary');
  run('make', ['-C', bootloader]);
  await cp(join(bootloader, 'limine-bios.sys'), join(guest, 'boot/limine/limine-bios.sys'));
  await cp(join(bootloader, 'BOOTX64.EFI'), join(guest, 'boot/EFI/BOOT/BOOTX64.EFI'));
  const boot = join(build, 'boot.img');
  const filesystem = join(build, 'root.ext2');
  const bootHandle = await open(boot, 'w');
  await bootHandle.truncate(64 * 1024 * 1024);
  await bootHandle.close();
  run('mformat', ['-i', boot, '-F', '-v', 'LIMINE', '::']);
  run('mcopy', ['-s', '-i', boot, join(guest, 'boot/kernel.bin'),
    join(guest, 'boot/limine'), join(guest, 'boot/EFI'), '::/']);
  run(join(prefix, 'sbin/mke2fs'), ['-q', '-t', 'ext2', '-F', '-b', '4096',
    '-I', '128', '-O', 'none,filetype,sparse_super,large_file',
    '-d', guest, filesystem, '524288']);
  const bootStart = 2048;
  const bootSectors = 131072;
  const rootStart = bootStart + bootSectors;
  const rootSectors = (await stat(filesystem)).size / 512;
  const disk = join(root, 'disk.img');
  const handle = await open(`${disk}.partial`, 'w');
  await handle.truncate((rootStart + rootSectors) * 512);
  const mbr = Buffer.alloc(512);
  for (const [index, start, count, type] of [[0, bootStart, bootSectors, 0x0c], [1, rootStart, rootSectors, 0x83]]) {
    const offset = 446 + index * 16;
    mbr[offset] = index === 0 ? 0x80 : 0;
    mbr[offset + 4] = type;
    mbr.writeUInt32LE(start, offset + 8);
    mbr.writeUInt32LE(count, offset + 12);
  }
  mbr.writeUInt16LE(0xaa55, 510);
  await handle.write(mbr, 0, mbr.length, 0);
  for (const [path, start] of [[boot, bootStart], [filesystem, rootStart]]) {
    const input = await open(path, 'r');
    const buffer = Buffer.alloc(4 * 1024 * 1024);
    let offset = 0;
    for (;;) {
      const { bytesRead } = await input.read(buffer, 0, buffer.length, offset);
      if (!bytesRead) break;
      await handle.write(buffer, 0, bytesRead, start * 512 + offset);
      offset += bytesRead;
    }
    await input.close();
  }
  await handle.close();
  run(join(bootloader, 'limine'), ['bios-install', `${disk}.partial`]);
  const { rename } = await import('node:fs/promises');
  await rename(`${disk}.partial`, disk);
  console.log(`Built cavOS graphical disk on macOS: ${disk}`);
}

async function main() {
  try {
    if (process.platform !== 'darwin') throw new Error('This builder is for macOS.');
    const mode = process.argv[2];
    if (mode && !['kernel', 'userspace', 'disk-only'].includes(mode)) {
      throw new Error(`Unknown build mode: ${mode}`);
    }
    await mkdir(downloads, { recursive: true });
    if (mode === 'userspace') {
      await prepareUserspace();
    } else {
      await prepareKernel();
    }
    if (!['kernel', 'userspace'].includes(mode)) {
      if (mode === 'disk-only') {
        await cp(join(root, 'target'), guest, { recursive: true });
      } else {
        await prepareUserspace();
      }
      await makeDisk();
    }
  } catch (error) {
    console.error(`macOS cavOS build failed: ${error.message}`);
    process.exitCode = 1;
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  await main();
}
