#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const https = require('node:https');
const { execFile, spawnSync } = require('node:child_process');
const { promisify } = require('node:util');
const { inject } = require('postject');

const root = path.resolve(__dirname, '..');
const dist = path.join(root, 'dist');
const fuse = 'NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2';

function options(argv) {
  const values = { platform: process.platform, arch: process.arch };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--platform' || arg === '--arch') {
      if (!argv[i + 1]) throw new Error(`${arg} needs a value`);
      values[arg.slice(2)] = argv[++i];
    } else if (arg.startsWith('--platform=')) values.platform = arg.slice(11);
    else if (arg.startsWith('--arch=')) values.arch = arg.slice(7);
    else throw new Error(`Unknown packaging option: ${arg}`);
  }
  if (!['linux', 'win32'].includes(values.platform)) throw new Error(`Unsupported platform: ${values.platform}`);
  if (values.arch !== 'x64') throw new Error(`Unsupported architecture: ${values.arch}`);
  if (values.platform !== process.platform || values.arch !== process.arch) {
    throw new Error(`Cannot package ${values.platform}-${values.arch} on ${process.platform}-${process.arch}`);
  }
  return values;
}

function childOutput(args) {
  const result = spawnSync(process.execPath, args, { cwd: root, stdio: 'inherit' });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`Node SEA generation failed (${result.status})`);
}

function getNodeLicense() {
  const url = `https://raw.githubusercontent.com/nodejs/node/${process.version}/LICENSE`;
  return new Promise((resolve, reject) => https.get(url, { timeout: 30000 }, response => {
    if (response.statusCode !== 200) {
      response.resume();
      reject(new Error(`Node licence download returned HTTP ${response.statusCode}`));
      return;
    }
    const chunks = [];
    response.on('data', chunk => chunks.push(chunk));
    response.on('end', () => resolve(Buffer.concat(chunks)));
  }).on('timeout', function onTimeout() { this.destroy(new Error('Node licence download timed out')); })
    .on('error', reject));
}

function copyFrontend(output) {
  const allowed = new Set(['index.html', 'renderer.js', 'b4-icon.png']);
  for (const name of fs.readdirSync(dist)) {
    const source = path.join(dist, name);
    if (fs.statSync(source).isFile() && (allowed.has(name) || name.endsWith('.LICENSE.txt') || name.endsWith('.ttf'))) {
      fs.copyFileSync(source, path.join(output, name));
    }
  }
}

function assertFrontendAssets(frontendDist = dist) {
  for (const name of ['index.html', 'renderer.js', 'b4-icon.png']) {
    if (!fs.existsSync(path.join(frontendDist, name)) || !fs.statSync(path.join(frontendDist, name)).isFile()) {
      throw new Error(`Required frontend asset is missing: ${name}`);
    }
  }
  if (!fs.readdirSync(frontendDist).some(name => name.endsWith('.ttf') && fs.statSync(path.join(frontendDist, name)).isFile())) {
    throw new Error('Required frontend font assets are missing from dist');
  }
}

function setWindowsGuiSubsystem(file) {
  const data = fs.readFileSync(file);
  if (data.length < 0x40 || data.toString('ascii', 0, 2) !== 'MZ') throw new Error('SEA executable has no valid DOS header');
  const pe = data.readUInt32LE(0x3c);
  if (pe + 24 + 70 > data.length || data.toString('ascii', pe, pe + 4) !== 'PE\0\0') throw new Error('SEA executable has no valid PE header');
  const magic = data.readUInt16LE(pe + 24);
  if (magic !== 0x10b && magic !== 0x20b) throw new Error('SEA executable has an unsupported PE optional header');
  const subsystemOffset = pe + 24 + 68;
  if (data.readUInt16LE(subsystemOffset) !== 3) throw new Error('Expected a console PE subsystem');
  data.writeUInt16LE(2, subsystemOffset);
  fs.writeFileSync(file, data);
}

async function setWindowsResources(file) {
  const rceditExe = path.resolve(path.dirname(require.resolve('rcedit')), '..', 'bin', 'rcedit-x64.exe');
  const icon = path.join(root, '..', 'assets', 'b4.ico');
  const args = [
    file,
    '--set-version-string', 'ProductName', 'Robot War Engine Launcher',
    '--set-version-string', 'FileDescription', 'Robot War Engine Launcher',
    '--set-icon', icon,
  ];
  await promisify(execFile)(rceditExe, args, {
    windowsHide: true,
    timeout: 60000,
    killSignal: 'SIGTERM',
    maxBuffer: 1024 * 1024,
  });
}

async function main() {
  const { platform } = options(process.argv.slice(2));
  assertFrontendAssets();
  const outputName = `rwe-launcher-${platform}-x64`;
  const output = path.resolve(root, outputName);
  if (path.dirname(output) !== root || path.basename(output) !== outputName) throw new Error('Unsafe package output path');
  fs.rmSync(output, { recursive: true, force: true });
  fs.mkdirSync(output, { recursive: true });

  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'rwe-sea-'));
  try {
    const config = path.join(temp, 'sea-config.json');
    const blob = path.join(temp, 'sea.blob');
    fs.writeFileSync(config, JSON.stringify({
      main: path.join(dist, 'main.js'),
      output: blob,
      disableExperimentalSEAWarning: true,
      useCodeCache: false,
      useSnapshot: false,
    }));
    childOutput(['--experimental-sea-config', config]);
    const executable = path.join(output, platform === 'win32' ? 'rwe-launcher.exe' : 'rwe-launcher');
    fs.copyFileSync(process.execPath, executable);
    if (platform === 'win32') {
      await setWindowsResources(executable);
    }
    await inject(executable, 'NODE_SEA_BLOB', fs.readFileSync(blob), { sentinelFuse: fuse });
    if (platform === 'win32') setWindowsGuiSubsystem(executable);
    else fs.chmodSync(executable, 0o755);
    copyFrontend(output);
    fs.mkdirSync(path.join(output, 'licenses'), { recursive: true });
    fs.copyFileSync(path.join(root, '..', 'LICENSE'), path.join(output, 'licenses', 'RWE-LICENSE'));
    fs.copyFileSync(path.join(root, 'src', 'launcher', 'font', 'LICENSE.txt'), path.join(output, 'licenses', 'Roboto-LICENSE.txt'));
    fs.writeFileSync(path.join(output, 'licenses', 'node-LICENSE.txt'), await getNodeLicense());
    const files = [];
    let bytes = 0;
    for (const name of fs.readdirSync(output, { withFileTypes: true })) {
      if (name.isFile()) {
        const size = fs.statSync(path.join(output, name.name)).size;
        bytes += size;
        files.push(`${name.name} (${size} bytes)`);
      } else if (name.isDirectory()) {
        for (const child of fs.readdirSync(path.join(output, name.name))) {
          const size = fs.statSync(path.join(output, name.name, child)).size;
          bytes += size;
          files.push(`${name.name}/${child} (${size} bytes)`);
        }
      }
    }
    console.log(`Packaged ${outputName}: ${bytes} bytes total\n${files.join('\n')}`);
  } finally {
    fs.rmSync(temp, { recursive: true, force: true });
  }
}

if (require.main === module) main().catch(error => { console.error(error); process.exitCode = 1; });

module.exports = { options, setWindowsGuiSubsystem, setWindowsResources, assertFrontendAssets };
