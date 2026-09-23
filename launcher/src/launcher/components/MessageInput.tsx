import { Button, TextField } from "@mui/material";
import * as React from "react";

interface MessageInputProps {
  onSend: (message: string) => void;
}

interface MessageInputState {
  value: string;
}

class MessageInput extends React.Component<
  MessageInputProps,
  MessageInputState
> {
  constructor(props: MessageInputProps) {
    super(props);
    this.state = { value: "" };
    this.handleMessageChange = this.handleMessageChange.bind(this);
    this.handleSend = this.handleSend.bind(this);
  }

  render() {
    return (
      <form
        className="game-room-screen-bottom-panel"
        onSubmit={this.handleSend}
      >
        <TextField
          sx={{ flexGrow: 1 }}
          value={this.state.value}
          onChange={this.handleMessageChange}
        />
        <Button type="submit">Send</Button>
      </form>
    );
  }

  private handleMessageChange(event: React.SyntheticEvent<EventTarget>) {
    this.setState({ value: (event.target as HTMLInputElement).value });
  }

  private handleSend(event: React.SyntheticEvent<EventTarget>) {
    event.preventDefault();
    if (this.state.value) {
      this.setState({ value: "" });
      this.props.onSend(this.state.value);
    }
  }
}

export default MessageInput;
