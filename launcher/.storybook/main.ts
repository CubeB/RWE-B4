import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import type { StorybookConfig } from "@storybook/react-webpack5";

// This file is loaded as a module, so there is no `require` to resolve with.
const here = dirname(fileURLToPath(import.meta.url));

// Storybook 10. The old config.tsx / webpack.config.js pair was Storybook 6's
// shape, and its webpack4 builders were where most of the launcher's critical
// advisories lived.
//
// The TypeScript rule is explicit for the same reason the old
// webpack.config.js had one: the framework's own pipeline hands a .tsx story
// to its export-order loader and to nothing else, so `import type` reaches
// webpack as a parse error. These are the renderer bundle's presets, so a
// story compiles the way the component it shows does.
const config: StorybookConfig = {
  stories: ["../src/**/*.stories.@(ts|tsx)"],
  framework: {
    name: "@storybook/react-webpack5",
    options: {},
  },
  webpackFinal: async config => {
    config.module = config.module ?? {};
    config.module.rules = config.module.rules ?? [];
    config.module.rules.push({
      test: /\.tsx?$/,
      exclude: /node_modules/,
      use: {
        loader: "babel-loader",
        options: {
          babelrc: false,
          configFile: false,
          presets: [
            ["@babel/env", { targets: { chrome: "120" } }],
            "@babel/react",
            "@babel/typescript",
          ],
        },
      },
    });
    config.resolve = config.resolve ?? {};
    config.resolve.alias = {
      ...config.resolve.alias,
      electron: resolve(here, "electron-stub.ts"),
    };
    config.resolve.extensions = [
      ...(config.resolve.extensions ?? []),
      ".ts",
      ".tsx",
    ];
    return config;
  },
};

export default config;
