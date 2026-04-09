#include "chat.h"
#include "types.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void chat_init(void) {
    g.chat_count = 0;
    g.chat_input_len = 0;
    g.chat_input[0] = '\0';
    g.chat_scroll = 0;

    /* Welcome message */
    chat_add_message(
        "Hello! I'm your Copilot assistant. Ask me anything about your code.",
        false);
}

void chat_add_message(const char *text, bool is_user) {
    if (g.chat_count >= MAX_CHAT_MSGS) {
        /* Drop oldest */
        free(g.chat_msgs[0].text);
        memmove(g.chat_msgs, g.chat_msgs + 1,
                (MAX_CHAT_MSGS - 1) * sizeof(ChatMessage));
        g.chat_count--;
    }
    ChatMessage *m = &g.chat_msgs[g.chat_count++];
    m->text    = strdup(text ? text : "");
    m->is_user = is_user;
    g.needs_redraw = true;
}

void chat_submit(void) {
    if (g.chat_input_len == 0) return;
    g.chat_input[g.chat_input_len] = '\0';
    chat_add_message(g.chat_input, true);

    /* Placeholder AI response */
    char reply[512];
    snprintf(reply, sizeof(reply),
        "I see you're asking about: \"%s\"\n"
        "Connect an AI backend (e.g. OpenAI API) to get real responses.",
        g.chat_input);
    chat_add_message(reply, false);

    g.chat_input_len = 0;
    g.chat_input[0] = '\0';
    g.needs_redraw = true;
}

void chat_handle_event(InputEvent *ev) {
    switch (ev->key) {
        case KEY_ENTER:
            chat_submit();
            break;
        case KEY_BACKSPACE:
            if (g.chat_input_len > 0)
                g.chat_input[--g.chat_input_len] = '\0';
            g.needs_redraw = true;
            break;
        case KEY_ARROW_UP:
            if (g.chat_scroll < g.chat_count - 1) {
                g.chat_scroll++;
                g.needs_redraw = true;
            }
            break;
        case KEY_ARROW_DOWN:
            if (g.chat_scroll > 0) {
                g.chat_scroll--;
                g.needs_redraw = true;
            }
            break;
        default:
            if (ev->codepoint >= 32 &&
                g.chat_input_len < (int)sizeof(g.chat_input) - 4) {
                uint32_t cp = ev->codepoint;
                char tmp[4]; int n;
                if (cp < 0x80) { tmp[0]=(char)cp; n=1; }
                else if (cp < 0x800) { tmp[0]=(char)(0xC0|(cp>>6)); tmp[1]=(char)(0x80|(cp&0x3F)); n=2; }
                else { tmp[0]=(char)(0xE0|(cp>>12)); tmp[1]=(char)(0x80|((cp>>6)&0x3F)); tmp[2]=(char)(0x80|(cp&0x3F)); n=3; }
                memcpy(g.chat_input + g.chat_input_len, tmp, n);
                g.chat_input_len += n;
                g.chat_input[g.chat_input_len] = '\0';
                g.needs_redraw = true;
            }
            break;
    }
}
