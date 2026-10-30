const path = require("path");
const CopyWebpackPlugin = require("copy-webpack-plugin");

module.exports = {
  mode: process.env.RWE_LAUNCHER_IS_DEV ? "development" : "production",
  entry: "./src/launcher/main.ts",
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
    filename: "main.js",
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
          from: "../assets/b4-icon.png",
          to: "b4-icon.png",
        },
      ],
    }),
  ],
  optimization: { minimize: !process.env.RWE_LAUNCHER_IS_DEV },
};
