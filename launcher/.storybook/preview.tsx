import * as React from "react";
import type { Preview } from "@storybook/react-webpack5";
import CssBaseline from "@mui/material/CssBaseline";
import "../src/launcher/style.css";

const preview: Preview = {
  decorators: [
    (Story) => (
      <React.Fragment>
        <CssBaseline />
        <Story />
      </React.Fragment>
    ),
  ],
};

export default preview;
