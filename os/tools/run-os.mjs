import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { startImageServer } from './live-images.mjs';

const folder = fileURLToPath(new URL('../../os_image_data/', import.meta.url));
try {
  const server = await startImageServer(folder);
  console.log(`Karnix live images: ${folder} (loopback port 18765)`);
  const [executable, ...args] = process.argv.slice(2);
  if (!executable) throw new Error('Missing QEMU executable');
  const child = spawn(executable, args, { stdio: 'inherit' });
  for (const signal of ['SIGINT', 'SIGTERM']) {
    process.on(signal, () => child.kill(signal));
  }
  child.once('error', error => {
    console.error(`QEMU launch failed: ${error.message}`);
    server.close();
    process.exitCode = 1;
  });
  child.once('exit', (code, signal) => {
    server.close();
    process.exitCode = signal ? 1 : code;
  });
} catch (error) {
  console.error(`Karnix launch failed: ${error.message}`);
  process.exitCode = 1;
}
