import type { Meta, StoryObj } from "@storybook/react-webpack5";
import { Wizard } from "./Wizard";

// Component Story Format 3. `storiesOf` went in Storybook 7, and the whole
// Storybook 6 tree was where most of the launcher's critical advisories lived.
const meta: Meta<typeof Wizard> = {
  title: "Wizard",
  component: Wizard,
  args: {
    onNext: () => {},
    onClose: () => {},
  },
};

export default meta;

type Story = StoryObj<typeof Wizard>;

export const Welcome: Story = { args: { state: "welcome" } };
export const Working: Story = { args: { state: "working" } };
export const Success: Story = { args: { state: "success" } };
export const Fail: Story = { args: { state: "fail" } };
