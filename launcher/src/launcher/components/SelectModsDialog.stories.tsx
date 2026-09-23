import type { Meta, StoryObj } from "@storybook/react-webpack5";
import { SelectModsDialog } from "./SelectModsDialog";

const meta: Meta<typeof SelectModsDialog> = {
  title: "SelectModsDialog",
  component: SelectModsDialog,
};

export default meta;

type Story = StoryObj<typeof SelectModsDialog>;

export const Default: Story = {
  args: {
    title: "Select Mods",
    items: ["ta", "tacc", "ta31", "taesc"],
    initiallyActiveItems: ["ta", "tacc"],
    onSubmit: () => {},
    onCancel: () => {},
  },
};
