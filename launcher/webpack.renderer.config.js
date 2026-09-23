const path = require("path");
const CopyWebpackPlugin = require("copy-webpack-plugin");
const ReactRefreshWebpackPlugin = require("@pmmmwh/react-refresh-webpack-plugin");
const webpack = require("webpack");

// react-hot-loader is gone. It worked by patching react-dom through an alias,
// which React 18's createRoot does not tolerate; React Refresh is the
// supported successor and needs no alias, only the babel plugin and the
// webpack plugin, and only while the dev server is running.
const isDev = !!process.env.RWE_LAUNCHER_IS_DEV;

module.exports = {
  mode: "development",
  entry: "./src/launcher/renderer.tsx",
  target: "electron-renderer",
  devtool: "source-map",
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
                    electron: "44.4.5",
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
    ...(isDev ? [new ReactRefreshWebpackPlugin()] : []),
  ],
  optimization: {
    moduleIds: "named",
  },

  devServer: {
    static: {
      directory: "./dist",
    },
    port: 8080,
    hot: true,
  },
};
