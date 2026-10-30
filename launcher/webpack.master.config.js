const path = require("path");
const CopyWebpackPlugin = require("copy-webpack-plugin");
const webpack = require("webpack");

module.exports = {
  mode: process.env.RWE_LAUNCHER_IS_DEV ? "development" : "production",
  entry: "./src/master-server/master-server.ts",
  target: "node",
  devtool: process.env.RWE_LAUNCHER_IS_DEV ? "source-map" : false,
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
                    node: "22",
                  },
                },
              ],
              "@babel/typescript",
            ],
          },
        },
        exclude: /node_modules/,
      },
    ],
  },
  resolve: {
    extensions: [".ts", ".tsx", ".js", ".json"],
  },
  output: {
    filename: "master-server.js",
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
          from: "package.json",
          to: "package.json",
        },
      ],
    }),
    new webpack.IgnorePlugin({ resourceRegExp: /^uws$/ }),
  ],
  optimization: { minimize: !process.env.RWE_LAUNCHER_IS_DEV },
};
