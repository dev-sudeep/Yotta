#ifndef YOTTA_CHAT_H
#define YOTTA_CHAT_H

#include "types.h"

/* Initialize the chat pane */
void chat_init(void);

/* Add a message to the chat history */
void chat_add_message(const char *text, bool is_user);

/* Handle input event while chat pane is active */
void chat_handle_event(InputEvent *ev);

/* Submit the current input as a user message */
void chat_submit(void);

#endif /* YOTTA_CHAT_H */
