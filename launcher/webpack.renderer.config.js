const path = require("path");
const CopyWebpackPlugin = require("copy-webpack-plugin");
const ReactRefreshWebpackPlugin = require("@pmmmwh/react-refresh-webpack-plugin");
const webpack = require("webpack");
const { execFileSync } = require("child_process");

// What this launcher was built from: the release tag when the build is made
// from one, otherwise the commit. The updater compares it with GitHub's
// releases. A build without git (a tarball) falls back to package.json.
function launcherVersion() {
  try {
    return execFileSync("git", ["describe", "--tags", "--always", "--dirty"], {
      cwd: __dirname,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "ignore"],
    }).trim();
  } catch {
    return require("./package.json").version;
  }
}

// react-hot-loader is gone. It worked by patching react-dom through an alias,
// which React 18's createRoot does not tolerate; React Refresh is the
// supported successor and needs no alias, only the babel plugin and the
// webpack plugin, and only while the dev server is running.
const isDev = !!process.env.RWE_LAUNCHER_IS_DEV;

module.exports = {
  mode: isDev ? "development" : "production",
  entry: "./src/launcher/renderer.tsx",
  target: "web",
  devtool: isDev ? "source-map" : false,
  module: {
    rules: [
      {
        test: /\.tsx?$/,
        use: {
          loader: "babel-loader",
          options: {
            presets: [
              [
                "@babel/env",
                {
                  targets: {
                    browsers: [
                      "last 2 Chrome versions",
                      "last 2 Firefox versions",
                      "last 2 Edge versions",
                    ],
                  },
                },
              ],
              "@babel/react",
              "@babel/typescript",
            ],
            plugins: isDev ? ["react-refresh/babel"] : [],
          },
        },
        exclude: /node_modules/,
      },
      {
        test: /\.css$/,
        use: ["style-loader", "css-loader"],
      },
      {
        test: /\.ttf$/,
        type: "asset/resource",
      },
    ],
  },
  resolve: {
    extensions: [".ts", ".tsx", ".js", ".json"],
  },
  output: {
    filename: "renderer.js",
    path: path.resolve(__dirname, "dist"),
  },
  node: {
    __dirname: false,
    __filename: false,
  },
  plugins: [
    new CopyWebpackPlugin({
      patterns: [
        {
          from: "index.html",
          to: "index.html",
        },
      ],
    }),
    new webpack.IgnorePlugin({ resourceRegExp: /^uws$/ }),
    new webpack.DefinePlugin({
      __RWE_LAUNCHER_VERSION__: JSON.stringify(launcherVersion()),
    }),
    ...(isDev ? [new ReactRefreshWebpackPlugin()] : []),
  ],
  optimization: {
    moduleIds: isDev ? "named" : "deterministic",
    minimize: !isDev,
  },

  devServer: {
    static: {
      directory: "./dist",
    },
    port: 8080,
    hot: true,
  },
};
