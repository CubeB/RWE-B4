import { spawn, SpawnOptions } from "child_process";
import { ChildProcess } from "child_process";
import { StringDecoder } from "string_decoder";

const maxRegistryOutputBytes = 16 * 1024 * 1024;
const maxDiagnosticBytes = 8000;

export function listPackageRegistry(
  setupExecutable: string,
  workingDirectory: string,
  libraryRoot: string,
  spawnProcess: typeof spawn = spawn
): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const child: ChildProcess = spawnProcess(
      setupExecutable,
      ["--list-packages", "--library-root", libraryRoot],
      { cwd: workingDirectory, windowsHide: true } as SpawnOptions
    );
    const decoder = new StringDecoder("utf8");
    let stdout = "";
    let stdoutBytes = 0;
    let stderr = "";
    let outputTooLarge = false;
    child.stdout?.on("data", (chunk: Buffer | string) => {
      const buffer = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
      stdoutBytes += buffer.byteLength;
      if (stdoutBytes > maxRegistryOutputBytes) {
        outputTooLarge = true;
      } else if (!outputTooLarge) {
        stdout += decoder.write(buffer);
      }
    });
    child.stderr?.on("data", (chunk: Buffer | string) => {
      stderr = (stderr + chunk.toString()).slice(-maxDiagnosticBytes);
    });
    child.on("error", reject);
    child.on("close", code => {
      stdout += decoder.end();
      if (outputTooLarge) {
        reject(
          new Error(
            `Package registry output exceeds ${maxRegistryOutputBytes} bytes`
          )
        );
        return;
      }
      try {
        const response = JSON.parse(stdout);
        if (code === 0) resolve(response);
        else
          reject(
            new Error(
              response?.error?.message ||
                stderr ||
                `Package listing failed (${code})`
            )
          );
      } catch {
        reject(
          new Error(stderr || `Invalid package registry response (${code})`)
        );
      }
    });
  });
}
