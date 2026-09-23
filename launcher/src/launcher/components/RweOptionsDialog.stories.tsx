import type { Meta, StoryObj } from "@storybook/react-webpack5";
import { RweOptionsDialog } from "./RweOptionsDialog";

const meta: Meta<typeof RweOptionsDialog> = {
  title: "RweOptionsDialog",
  component: RweOptionsDialog,
};

export default meta;

type Story = StoryObj<typeof RweOptionsDialog>;

export const Default: Story = {
  args: {
    videoModes: [
      { width: 640, height: 480 },
      { width: 800, height: 600 },
      { width: 1024, height: 768 },
    ],
    onSubmit: () => {},
    onCancel: () => {},
  },
};
