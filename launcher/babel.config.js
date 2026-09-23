// Babel's job here is Jest's: webpack's configs carry their own presets,
// because each bundle has a different target and the renderer needs JSX.
module.exports = {
  presets: [
    [
      "@babel/env",
      {
        targets: {
          node: "current",
        },
      },
    ],
    "@babel/typescript",
  ],
};
