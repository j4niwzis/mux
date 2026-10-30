# avatars

A plugin of [mux](https://github.com/j4niwzis/mux).

A chat's picture beside its row in the list, a person's beside a search result, and a sender's face beside their messages in a group (through `contract::message_faces`, the core keeping where it is placed). The core's row draws no picture: this plugin shapes the row's text piece (`contract::shape<views::chat_row>`) into the picture and the text side by side. Built where MUX_PLUGIN_AVATARS is on; without it, rows are text alone.
