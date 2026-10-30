const assert = require("node:assert/strict");
const { TextEncoder, TextDecoder } = require("node:util");
const { JSDOM, VirtualConsole } = require("jsdom");

const url = process.argv[2];
if (!url)
  throw new Error("Usage: node scripts/browser-smoke.cjs <launcher-url>");

class EventSourceStub {
  constructor() {
    this.onmessage = null;
    this.onerror = null;
  }
  close() {}
}

async function main() {
  const errors = [];
  const virtualConsole = new VirtualConsole();
  virtualConsole.on("jsdomError", error => errors.push(error.message));
  const dom = await JSDOM.fromURL(url, {
    resources: "usable",
    runScripts: "dangerously",
    pretendToBeVisual: true,
    virtualConsole,
    beforeParse(window) {
      window.TextEncoder = TextEncoder;
      window.TextDecoder = TextDecoder;
      window.EventSource = EventSourceStub;
      window.fetch = (input, init) =>
        fetch(new URL(input, window.location.href), init);
    },
  });
  try {
    const deadline = Date.now() + 10_000;
    while (Date.now() < deadline) {
      const text = dom.window.document.body.textContent || "";
      if (text.includes("Play RWE") && text.includes("Community Patch")) break;
      await new Promise(resolve => setTimeout(resolve, 50));
    }
    const text = dom.window.document.body.textContent || "";
    assert.match(text, /Play RWE/, "React launcher did not mount");
    assert.match(text, /Community Patch/);
    assert.match(text, /Import game data/);
    assert.match(text, /Player name/);
    assert.deepEqual(
      errors,
      [],
      `Browser runtime errors: ${errors.join("; ")}`
    );
    process.stdout.write(
      "Browser launcher smoke passed: React mounted and core controls rendered.\n"
    );
  } finally {
    dom.window.close();
  }
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
