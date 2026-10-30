# avatars

A plugin of [mux](https://github.com/j4niwzis/mux).

A chat's picture beside its row in the list, a person's beside a search result, an account's in the drawer, a reader's in a message's read-by list, and a room's in a server's directory -- each by a rule on the row's text piece. The core's row draws no picture: this plugin shapes the row's text piece (`contract::shape<views::chat_row>`) into the picture and the text side by side. Built where MUX_PLUGIN_AVATARS is on; without it, rows are text alone.
