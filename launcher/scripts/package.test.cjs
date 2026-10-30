'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const test = require('node:test');
const { options, setWindowsGuiSubsystem, assertFrontendAssets } = require('./package.cjs');

test('packaging rejects requested host mismatches', () => {
  assert.throws(() => options(['--arch=arm64']), /Unsupported architecture/);
  const otherHostPlatform = process.platform === 'win32' ? 'linux' : 'win32';
  assert.throws(() => options([`--platform=${otherHostPlatform}`]), /Cannot package/);
  assert.throws(() => options(['--platform', 'darwin']), /Unsupported platform/);
  assert.equal(options([`--platform=${process.platform}`, '--arch=x64']).arch, 'x64');
});

test('packaging requires complete frontend assets and at least one font', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'rwe-frontend-test-'));
  try {
    for (const name of ['index.html', 'renderer.js', 'b4-icon.png']) fs.writeFileSync(path.join(directory, name), 'asset');
    assert.throws(() => assertFrontendAssets(directory), /font assets are missing/);
    fs.writeFileSync(path.join(directory, 'font.ttf'), 'font');
    assert.doesNotThrow(() => assertFrontendAssets(directory));
    fs.rmSync(path.join(directory, 'renderer.js'));
    assert.throws(() => assertFrontendAssets(directory), /renderer.js/);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});

test('mismatched CLI request leaves existing package files untouched', () => {
  const launcherRoot = path.resolve(__dirname, '..');
  const outputName = `rwe-launcher-${process.platform}-x64`;
  const output = path.join(launcherRoot, outputName);
  fs.mkdirSync(output, { recursive: true });
  const marker = path.join(output, 'preserve-me.txt');
  fs.writeFileSync(marker, 'existing package');
  try {
    const result = spawnSync(process.execPath, [path.join(__dirname, 'package.cjs'), '--arch=arm64'], { cwd: launcherRoot });
    assert.notEqual(result.status, 0);
    assert.equal(fs.readFileSync(marker, 'utf8'), 'existing package');
  } finally {
    fs.rmSync(marker, { force: true });
  }
});

test('Windows subsystem patch validates PE bounds and changes console to GUI', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'rwe-pe-test-'));
  const file = path.join(directory, 'sample.exe');
  try {
    const valid = Buffer.alloc(512);
    valid.write('MZ', 0, 'ascii');
    valid.writeUInt32LE(0x80, 0x3c);
    valid.write('PE\0\0', 0x80, 'ascii');
    valid.writeUInt16LE(0x20b, 0x98);
    valid.writeUInt16LE(3, 0x98 + 68);
    fs.writeFileSync(file, valid);
    setWindowsGuiSubsystem(file);
    assert.equal(fs.readFileSync(file).readUInt16LE(0x98 + 68), 2);

    const truncated = Buffer.alloc(120);
    truncated.write('MZ', 0, 'ascii');
    truncated.writeUInt32LE(0x70, 0x3c);
    fs.writeFileSync(file, truncated);
    assert.throws(() => setWindowsGuiSubsystem(file), /valid PE header/);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});
