#ifndef YOTTA_INPUT_H
#define YOTTA_INPUT_H

#include "types.h"

/* Put the terminal into raw mode (no echo, no line buffering) */
void input_raw_mode(void);

/* Restore the original terminal mode */
void input_restore_mode(void);

/* Read one input event (blocking). Returns false if no input available
   within the given timeout (milliseconds). */
bool input_read_event(InputEvent *ev, int timeout_ms);

/* Parse a UTF-8 codepoint from a byte; handles multibyte sequences */
uint32_t input_utf8_codepoint(const unsigned char *buf, int *bytes_consumed);

#endif /* YOTTA_INPUT_H */
