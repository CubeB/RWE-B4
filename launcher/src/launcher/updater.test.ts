import {
  chooseUpdateAsset,
  evaluateUpdates,
  isNewerVersion,
  parseVersion,
  ReleaseInfo,
} from "./updater";

function release(
  tag: string,
  assets: Array<[string, string]> = []
): ReleaseInfo {
  return {
    tag,
    name: tag,
    url: `https://example.test/${tag}`,
    prerelease: tag.includes("-"),
    publishedAt: "2026-10-26T00:00:00Z",
    assets: assets.map(([name, url]) => ({ name, url })),
  };
}

describe("update versions", () => {
  it("orders prereleases and releases", () => {
    expect(parseVersion("v1.1.0-pre6")).toEqual({
      numbers: [1, 1, 0],
      prerelease: ["pre6"],
    });
    expect(isNewerVersion("v1.1.0-pre6", "v1.1.0-pre5")).toBe(true);
    expect(isNewerVersion("v1.1.0", "v1.1.0-pre6")).toBe(true);
    expect(isNewerVersion("v1.1.0-pre5", "v1.1.0-pre6")).toBe(false);
    expect(isNewerVersion("v1.1.0", "v1.1.0")).toBe(false);
    expect(isNewerVersion("v1.2.0", "v1.1.9")).toBe(true);
  });

  it("reads the commit distance git describe adds after a tag", () => {
    expect(parseVersion("v1.1.0-pre6-3-gabc1234-dirty")).toEqual({
      numbers: [1, 1, 0],
      prerelease: ["pre6"],
    });
  });

  it("refuses to compare a plain development hash", () => {
    expect(parseVersion("abc1234")).toBeUndefined();
    expect(isNewerVersion("v1.1.0", "abc1234")).toBe(false);
  });
});

describe("update evaluation", () => {
  const releases = [
    release("v1.1.0-pre5"),
    release("v1.1.0-pre6", [
      [
        "RobotWarEngine-1.1.0-pre6-Windows-Release.zip",
        "https://example.test/zip",
      ],
      [
        "RobotWarEngine-1.1.0-pre6-Windows-Debug.exe",
        "https://example.test/debug",
      ],
      [
        "RobotWarEngine-1.1.0-pre6-Windows-Release.exe",
        "https://example.test/setup.exe",
      ],
    ]),
    release("v1.0.0"),
  ];

  it("picks the newest release and offers the platform's installer", () => {
    const result = evaluateUpdates(releases, "v1.1.0-pre5", "win32");
    expect(result.latest?.tag).toBe("v1.1.0-pre6");
    expect(result.updateAvailable).toBe(true);
    expect(result.asset?.url).toBe("https://example.test/setup.exe");
  });

  it("reports up to date when the build is the newest release", () => {
    const result = evaluateUpdates(releases, "v1.1.0-pre6", "win32");
    expect(result.updateAvailable).toBe(false);
    expect(result.latest?.tag).toBe("v1.1.0-pre6");
  });

  it("offers no in-place install from a development build", () => {
    const result = evaluateUpdates(releases, "abc1234", "linux");
    expect(result.currentIsComparable).toBe(false);
    expect(result.updateAvailable).toBe(false);
    expect(result.latest?.tag).toBe("v1.1.0-pre6");
  });

  it("chooses the AppImage on Linux and nothing on an unknown platform", () => {
    const assets = [
      { name: "RobotWarEngine-1.1.0-pre6-Windows-Release.exe", url: "setup" },
      { name: "Robot_War_Engine-1.1.0-pre6.AppImage", url: "appimage" },
    ];
    expect(chooseUpdateAsset("linux", assets)?.url).toBe("appimage");
    expect(chooseUpdateAsset("win32", assets)?.url).toBe("setup");
    expect(chooseUpdateAsset("darwin", assets)).toBeUndefined();
  });
});
