import type { Meta, StoryObj } from "@storybook/react-webpack5";
import MapSelectDialog from "./MapSelectDialog";

const meta: Meta<typeof MapSelectDialog> = {
  title: "MapSelectDialog",
  component: MapSelectDialog,
};

export default meta;

type Story = StoryObj<typeof MapSelectDialog>;

export const Default: Story = {
  args: {
    open: true,
    maps: ["Gods of War", "Comet Catcher", "Painted Desert", "John's Pass"],
    selectedMap: "John's Pass",
    selectedMapDetails: {
      description: "10 X 15 Follow the river up the middle, if you dare!",
      memory: "32 mb",
      numberOfPlayers: "2, 3, 4",
    },
    onConfirm: () => {},
    onSelect: () => {},
    onClose: () => {},
  },
};
