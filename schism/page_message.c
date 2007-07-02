/*
 * Schism Tracker - a cross-platform Impulse Tracker clone
 * copyright (c) 2003-2005 chisel <schism@chisel.cjb.net>
 * copyright (c) 2005-2006 Mrs. Brisby <mrs.brisby@nimh.org>
 * URL: http://nimh.org/schism/
 * URL: http://rigelseven.com/schism/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* --->> WARNING <<---
 *
 * This is an excellent example of how NOT to write a text editor.
 * IMHO, the best way to add a song message is by writing it in some
 * other program and attaching it to the song with something like
 * ZaStaR's ITTXT utility (hmm, maybe I should rewrite that, too ^_^) so
 * I'm not *really* concerned about the fact that this code completely
 * sucks. Just remember, this ain't Xcode. */

#include "headers.h"

#include "song.h"
#include "clippy.h"

#include <ctype.h>

/* --------------------------------------------------------------------- */

static struct widget widgets_message[1];

static int top_line = 0;
static int cursor_line = 0;
static int cursor_char = 0;
/* this is the absolute cursor position from top of message.
 * (should be updated whenever cursor_line/cursor_char change) */
static int cursor_pos = 0;

unsigned char *message = NULL;
int edit_mode = 0;

/* nonzero => message should use the alternate font */
static int message_extfont = 1;

/* This is a bit weird... Impulse Tracker usually wraps at 74, but if
 * the line doesn't have any spaces in it (like in a solid line of
 * dashes or something) it gets wrapped at 75. I'm using 75 for
 * everything because it's always nice to have a bit extra space :) */
#define LINE_WRAP 75

/* --------------------------------------------------------------------- */

static int message_handle_key_editmode(struct key_event * k);
static int message_handle_key_viewmode(struct key_event * k);

/* --------------------------------------------------------------------- */

/* returns the number of characters on the nth line of text, setting ptr
 * to the first character on the line. if it there are fewer than n
 * lines, ptr is set to the \0 at the end of the string, and the
 * function returns -1. note: if *ptr == text, weird things will
 * probably happen, so don't do that. */
static int get_nth_line(const unsigned char *text, int n, const unsigned char **ptr)
{
        const unsigned char *tmp;

        if (!text) {
                *ptr = NULL;
                return 0;
        }

        *ptr = text;
        while (n > 0) {
                n--;
                *ptr = (unsigned char *)strpbrk((char*)*ptr, "\xd\xa");
                if (!(*ptr)) {
                        *ptr = text + strlen((char*)text);
                        return -1;
                }
                if ((*ptr)[0] == 13 && (*ptr)[1] == 10)
                        *ptr += 2;
                else
                        (*ptr)++;
        }

        tmp = (unsigned char *)strpbrk((char*)*ptr, "\xd\xa");
        return (tmp ? (unsigned) (tmp - *ptr) : strlen((char*)*ptr));
}
static void set_absolute_position(const unsigned char *text, int pos, int *line, int *ch)
{
	int len;
	const unsigned char *ptr;

	*line = *ch = 0;
	ptr = 0;
	while (pos > 0) {
	        len = get_nth_line(text, *line, &ptr);
		if (len < 0) {
			/* end of file */
			(*line) = (*line) - 1;
			if (*line < 0) *line = 0;
	        	len = get_nth_line(text, *line, &ptr);
			if (len < 0) {
				*ch = 0;
			} else {
				*ch = len;
			}
			pos = 0;
			
		} else if (len >= pos) {
			*ch = pos;
			pos = 0;
		} else {
			pos -= (len+1); /* EOC */
			(*line) = (*line) + 1;
		}
	}
}
static int get_absolute_position(const unsigned char *text, int line, int character)
{
        int len;
        const unsigned char *ptr;

	ptr = 0;
        len = get_nth_line(text, line, &ptr);
        if (len < 0) {
                return 0;
        }
        /* hrm... what if cursor_char > len? */
        return (ptr - text) + character;
}

/* --------------------------------------------------------------------- */

static void message_reposition(void)
{
        if (cursor_line < top_line) {
                top_line = cursor_line;
        } else if (cursor_line > top_line + 34) {
                top_line = cursor_line - 34;
        }
}

/* --------------------------------------------------------------------- */

/* returns 1 if a character was actually added */
static int message_add_char(int newchar, int position)
{
        int len = strlen((char*)message);

        if (len == 8000) {
                dialog_create(DIALOG_OK, "  Song message too long!  ", NULL, NULL, 0, NULL);
                return 0;
        }
        if (position < 0 || position > len) {
                log_appendf(4, "message_add_char: position=%d, len=%d - shouldn't happen!", position, len);
                return 0;
        }

        memmove(message + position + 1, message + position, len - position);
        message[len + 1] = 0;
        message[position] = (unsigned char)newchar;
        return 1;
}

/* this returns the new length of the line */
static int message_wrap_line(unsigned char *bol_ptr)
{
        unsigned char *eol_ptr;
        unsigned char *last_space = NULL;
        unsigned char *tmp = bol_ptr;

        if (!bol_ptr)
                /* shouldn't happen, but... */
                return 0;

        eol_ptr = (unsigned char *)strpbrk((char*)bol_ptr, "\xd\xa");
        if (!eol_ptr)
                eol_ptr = bol_ptr + strlen((char*)bol_ptr);

        for (;;) {
                tmp = (unsigned char *)strpbrk((char*)(tmp + 1), " \t");
                if (tmp == NULL || tmp > eol_ptr
                    || tmp - bol_ptr > LINE_WRAP)
                        break;
                last_space = tmp;
        }

        if (last_space) {
                *last_space = 13;
                return last_space - bol_ptr;
        } else {
                /* what, no spaces to cut at? chop it mercilessly. */
                if (message_add_char(13, bol_ptr + LINE_WRAP - message)
                    == 0)
                        /* ack, the message is too long to wrap the line!
                         * gonna have to resort to something ugly. */
                        bol_ptr[LINE_WRAP] = 13;
                return LINE_WRAP;
        }
}

/* --------------------------------------------------------------------- */

static void message_draw(void)
{
        const unsigned char *line, *prevline = message;
        int fg = (message_extfont ? 12 : 6);
        int len = get_nth_line(message, top_line, &line);
        int n, cp, clipl, clipr;
	int skipc, cutc;

        draw_fill_chars(2, 13, 77, 47, 0);

	if (clippy_owner(CLIPPY_SELECT) == widgets_message) {
		clipl = widgets_message[0].clip_start;
		clipr = widgets_message[0].clip_end;
		if (clipl > clipr) {
			cp = clipl;
			clipl = clipr;
			clipr = cp;
		}
	} else {
		clipl = clipr = -1;
	}

        for (n = 0; n < 35; n++) {
                if (len < 0) {
                        break;
                } else if (len > 0) {
                        /* FIXME | shouldn't need this check here,
                         * FIXME | because the line should already be
                         * FIXME | short enough to fit */
                        if (len > LINE_WRAP)
                                len = LINE_WRAP;
			if (message_extfont)
                        	draw_text_bios_len((unsigned char *) line, len, 2, 13 + n, fg, 0);
			else
                        	draw_text_len((unsigned char *) line, len, 2, 13 + n, fg, 0);
			
			if (clipl > -1) {
				cp = line - message;
				skipc = clipl - cp;
				cutc = clipr - clipl;
				if (skipc < 0) {
					cutc += skipc; /* ... -skipc */
					skipc = 0;
				}
				if (cutc < 0) cutc = 0;
				if (cutc > (len-skipc)) cutc = (len-skipc);
				if (cutc > 0 && skipc < len) {
					if (message_extfont)
	                        		draw_text_bios_len((unsigned char *) line+skipc, cutc, 2+skipc, 13 + n, fg, 8);
					else
	                        		draw_text_len((unsigned char *) line+skipc, cutc, 2+skipc, 13 + n, fg, 8);
				}
			}
                }
                if (edit_mode) {
                        draw_char(20, 2 + len, 13 + n, 1, 0);
                }
                prevline = line;
                len = get_nth_line(prevline, 1, &line);
        }

        if (edit_mode && len < 0) {
                /* end of the message */
                len = get_nth_line(prevline, 0, &line);
                /* FIXME: see above */
                if (len > LINE_WRAP)
                        len = LINE_WRAP;
                draw_char(20, 2 + len, 13 + n - 1, 2, 0);
        }

        if (edit_mode) {
                /* draw the cursor */
                len = get_nth_line(message, cursor_line, &line);

                /* FIXME: ... ugh */
                if (len > LINE_WRAP)
                        len = LINE_WRAP;
                if (cursor_char > LINE_WRAP + 1)
                        cursor_char = LINE_WRAP + 1;

                if (cursor_char >= len) {
                        draw_char(20, 2 + cursor_char,
                                  13 + (cursor_line - top_line), 0, 3);
                } else {
                        draw_char(line[cursor_char], 2 + cursor_char,
                                  13 + (cursor_line - top_line), 8, 3);
                }
        }
}

/* --------------------------------------------------------------------- */

static inline void message_set_editmode(void)
{
        edit_mode = 1;
	widgets_message[0].accept_text = 1;
        top_line = cursor_line = cursor_char = cursor_pos = 0;
        widgets_message[0].d.other.handle_key = message_handle_key_editmode;

        status.flags |= NEED_UPDATE;
}

static inline void message_set_viewmode(void)
{
	if (!(status.flags & PLAIN_TEXTEDIT)) {
		edit_mode = 0;
		widgets_message[0].accept_text = 0;
		widgets_message[0].d.other.handle_key = message_handle_key_viewmode;

		status.flags |= NEED_UPDATE;
	}
}

/* --------------------------------------------------------------------- */

static void message_insert_char(int c)
{
        const unsigned char *ptr;
        int n;

        if (!edit_mode)
                return;

	memused_songchanged();
        if (c == '\t') {
                /* Find the number of characters until the next tab stop.
                 * (This is new behaviour; Impulse Tracker just inserts
                 * eight characters regardless of the cursor position.) */
                n = 8 - cursor_char % 8;
                if (cursor_char + n > LINE_WRAP) {
                        message_insert_char('\r');
                } else {
                        do {
                                if (!message_add_char(' ', cursor_pos))
                                        break;
                                cursor_char++;
                                cursor_pos++;
                                n--;
                        } while (n);
                }
        } else if (c < 32 && c != '\r') {
                return;
        } else {
                if (!message_add_char(c, cursor_pos))
                        return;
                cursor_pos++;
                if (c == '\r') {
                        cursor_char = 0;
                        cursor_line++;
                } else {
                        cursor_char++;
                }
        }
        if (get_nth_line(message, cursor_line, &ptr) >= LINE_WRAP) {
                message_wrap_line((unsigned char *)ptr);
        }
        if (cursor_char >= LINE_WRAP) {
                cursor_char = get_nth_line(message, ++cursor_line, &ptr);
                cursor_pos =
                        get_absolute_position(message, cursor_line,
                                              cursor_char);
        }

        message_reposition();
        status.flags |= NEED_UPDATE;
}

static void message_delete_char(void)
{
        int len = strlen((char*)message);
        const unsigned char *ptr;

        if (cursor_pos == 0)
                return;
        memmove(message + cursor_pos - 1, message + cursor_pos,
                len - cursor_pos + 1);
        message[8000] = 0;
        cursor_pos--;
        if (cursor_char == 0) {
                cursor_line--;
                cursor_char = get_nth_line(message, cursor_line, &ptr);
        } else {
                cursor_char--;
        }

        message_reposition();
        status.flags |= NEED_UPDATE;
}

static void message_delete_next_char(void)
{
        int len = strlen((char*)message);

        if (cursor_pos == len)
                return;
        memmove(message + cursor_pos, message + cursor_pos + 1,
                len - cursor_pos);
        message[8000] = 0;

        status.flags |= NEED_UPDATE;
}

static void message_delete_line(void)
{
        int len;
        int movelen;
        const unsigned char *ptr;

        len = get_nth_line(message, cursor_line, &ptr);
        if (len < 0)
                return;
        if (ptr[len] == 13 && ptr[len + 1] == 10)
                len++;
        movelen = (message + strlen((char*)message) - ptr);
        if (movelen == 0)
                return;
        memmove((void *) ptr, ptr + len + 1, movelen);
        len = get_nth_line(message, cursor_line, &ptr);
        if (cursor_char > len) {
                cursor_char = len;
                cursor_pos =
                        get_absolute_position(message, cursor_line,
                                              cursor_char);
        }
        message_reposition();
        status.flags |= NEED_UPDATE;
}

static void message_clear(UNUSED void *data)
{
        message[0] = 0;
	memused_songchanged();
        message_set_viewmode();
}

/* --------------------------------------------------------------------- */

static void prompt_message_clear(void)
{
        dialog_create(DIALOG_OK_CANCEL, "Clear song message?", message_clear, NULL, 1, NULL);
}

/* --------------------------------------------------------------------- */

static int message_handle_key_viewmode(struct key_event * k)
{
	if (!k->state) {
		if (k->mouse == MOUSE_SCROLL_UP) {
			top_line--;
		} else if (k->mouse == MOUSE_SCROLL_DOWN) {
			top_line++;
		} else if (k->mouse == MOUSE_CLICK) {
                	message_set_editmode();
			return message_handle_key_editmode(k);
		}
	}

        switch (k->sym) {
        case SDLK_UP:
		if (k->state) return 0;
                top_line--;
                break;
        case SDLK_DOWN:
		if (k->state) return 0;
                top_line++;
                break;
        case SDLK_PAGEUP:
		if (k->state) return 0;
                top_line -= 35;
                break;
        case SDLK_PAGEDOWN:
		if (k->state) return 0;
                top_line += 35;
                break;
        case SDLK_HOME:
		if (k->state) return 0;
                top_line = 0;
                break;
        case SDLK_END:
		if (k->state) return 0;
                top_line = get_num_lines((char*)message) - 34;
                break;
        case SDLK_t:
		if (k->state) return 0;
                if (k->mod & KMOD_CTRL) {
                        message_extfont = !message_extfont;
                        break;
                }
                return 1;
        case SDLK_RETURN:
		if (!k->state) return 0;
                message_set_editmode();
                return 1;
        default:
                return 0;
        }

        if (top_line < 0)
                top_line = 0;

        status.flags |= NEED_UPDATE;

        return 1;
}
static void _delete_selection(void)
{
        int len = strlen((char*)message);
	int eat;

	cursor_pos = widgets_message[0].clip_start;
	if (cursor_pos > widgets_message[0].clip_end) {
		cursor_pos = widgets_message[0].clip_end;
		eat = widgets_message[0].clip_start - cursor_pos;
	} else {
		eat = widgets_message[0].clip_end - cursor_pos;
	}
	clippy_select(0,0,0);
        if (cursor_pos == len)
                return;
        memmove(message + cursor_pos, message + cursor_pos + eat + 1,
                ((len - cursor_pos) - eat)+1);
        message[8000] = 0;
	set_absolute_position(message, cursor_pos, &cursor_line, &cursor_char);
        message_reposition();

        status.flags |= NEED_UPDATE;
}

static int message_handle_key_editmode(struct key_event * k)
{
        int line_len, num_lines = -1;
        int new_cursor_line = cursor_line;
        int new_cursor_char = cursor_char;
        const unsigned char *ptr;
	int doing_drag = 0;
	int clipl, clipr, cp;

	if (k->mouse == MOUSE_SCROLL_UP) {
		if (k->state) return 0;
		new_cursor_line--;
	} else if (k->mouse == MOUSE_SCROLL_DOWN) {
		if (k->state) return 0;
		new_cursor_line++;
	} else if (k->mouse == MOUSE_CLICK && k->mouse_button == 2) {
		if (k->state) status.flags |= CLIPPY_PASTE_SELECTION;
		return 1;
	} else if (k->mouse == MOUSE_CLICK) {
		if (k->x >= 2 && k->x <= 77 && k->y >= 13 && k->y <= 47) {
			new_cursor_line = (k->y - 13) + top_line;
			new_cursor_char = (k->x - 2);
			if (k->sx != k->x || k->sy != k->y) {
				/* yay drag operation */
				cp = get_absolute_position(message, (k->sy-13)+top_line,
							(k->sx-2));
				widgets_message[0].clip_start = cp;
				doing_drag = 1;
			}
		}
	}

        line_len = get_nth_line(message, cursor_line, &ptr);


        switch (k->sym) {
        case SDLK_UP:
		if (!NO_MODIFIER(k->mod))
			return 0;
		if (k->state) return 1;
                new_cursor_line--;
                break;
        case SDLK_DOWN:
		if (!NO_MODIFIER(k->mod))
			return 0;
		if (k->state) return 1;
                new_cursor_line++;
                break;
        case SDLK_LEFT:
		if (!NO_MODIFIER(k->mod))
			return 0;
		if (k->state) return 1;
                new_cursor_char--;
                break;
        case SDLK_RIGHT:
		if (!NO_MODIFIER(k->mod))
			return 0;
		if (k->state) return 1;
                new_cursor_char++;
                break;
        case SDLK_PAGEUP:
		if (!NO_MODIFIER(k->mod))
			return 0;
		if (k->state) return 1;
                new_cursor_line -= 35;
                break;
        case SDLK_PAGEDOWN:
		if (!NO_MODIFIER(k->mod))
			return 0;
		if (k->state) return 1;
                new_cursor_line += 35;
                break;
        case SDLK_HOME:
		if (k->state) return 1;
                if (k->mod & KMOD_CTRL)
                        new_cursor_line = 0;
                else
                        new_cursor_char = 0;
                break;
        case SDLK_END:
		if (k->state) return 1;
                if (k->mod & KMOD_CTRL) {
                        num_lines = get_num_lines((char*)message);
                        new_cursor_line = num_lines;
                } else {
                        new_cursor_char = line_len;
                }
                break;
        case SDLK_ESCAPE:
		if (!NO_MODIFIER(k->mod))
			return 0;
		if (k->state) return 1;
                message_set_viewmode();
		memused_songchanged();
                return 1;
        case SDLK_BACKSPACE:
		if (!NO_MODIFIER(k->mod))
			return 0;
		if (k->state) return 1;
		if (k->sym && clippy_owner(CLIPPY_SELECT) == widgets_message) {
			_delete_selection();
		} else {
                	message_delete_char();
		}
                return 1;
        case SDLK_DELETE:
		if (!NO_MODIFIER(k->mod))
			return 0;
		if (k->state) return 1;
		if (k->sym && clippy_owner(CLIPPY_SELECT) == widgets_message) {
			_delete_selection();
		} else {
	                message_delete_next_char();
		}
                return 1;
        default:
                if (k->mod & KMOD_CTRL) {
			if (k->state) return 1;
                        if (k->sym == SDLK_t) {
                                message_extfont = !message_extfont;
                                break;
                        } else if (k->sym == SDLK_y) {
				clippy_select(0,0,0);
                                message_delete_line();
                                break;
                        }
                } else if (k->mod & KMOD_ALT) {
			if (k->state) return 1;
                        if (k->sym == SDLK_c) {
                                prompt_message_clear();
                                return 1;
                        }
                } else if (!k->mouse) {
			if (k->unicode == '\r' || k->unicode == '\t'
			|| k->unicode >= 32) {
				if (k->state) return 1;
				if (k->sym && clippy_owner(CLIPPY_SELECT) == widgets_message) {
					_delete_selection();
				}
				if (k->mod & (KMOD_SHIFT|KMOD_CAPS)) {
					message_insert_char(toupper((unsigned int)k->unicode));
				} else {
					message_insert_char(k->unicode);
				}
                        	return 1;
			}
			return 0;
                }

		if (k->mouse != MOUSE_CLICK)
	                return 0;

		if (k->state) return 1;
		if (!doing_drag) {
			clippy_select(0,0,0);
		}
        }

        if (new_cursor_line != cursor_line) {
                if (num_lines == -1)
                        num_lines = get_num_lines((char*)message);

                if (new_cursor_line < 0)
                        new_cursor_line = 0;
                else if (new_cursor_line > num_lines)
                        new_cursor_line = num_lines;

                /* make sure the cursor doesn't go past the new eol */
                line_len = get_nth_line(message, new_cursor_line, &ptr);
                if (new_cursor_char > line_len)
                        new_cursor_char = line_len;

		cursor_char = new_cursor_char;
                cursor_line = new_cursor_line;
        } else if (new_cursor_char != cursor_char) {
	/* we say "else" here ESPECIALLY because the mouse can only come
	in the top section - not because it's some clever optimization */
                if (new_cursor_char < 0) {
                        if (cursor_line == 0) {
				new_cursor_char = cursor_char;
			} else {
                        	cursor_line--;
	                        new_cursor_char =
					get_nth_line(message, cursor_line, &ptr);
			}

                } else if (new_cursor_char >
                           get_nth_line(message, cursor_line, &ptr)) {
                        if (cursor_line == get_num_lines((char*)message)) {
				new_cursor_char = cursor_char;
			} else {
                        	cursor_line++;
	                        new_cursor_char = 0;
			}
                }
                cursor_char = new_cursor_char;
        }

        message_reposition();
        cursor_pos = get_absolute_position(message, cursor_line, cursor_char);

	if (doing_drag) {
		widgets_message[0].clip_end = cursor_pos;

		clipl = widgets_message[0].clip_start;
		clipr = widgets_message[0].clip_end;
		if (clipl > clipr) {
			cp = clipl;
			clipl = clipr;
			clipr = cp;
		}
		clippy_select(widgets_message, (char*)(message+clipl), clipr-clipl);
	}

        status.flags |= NEED_UPDATE;

        return 1;
}

/* --------------------------------------------------------------------- */

static void message_draw_const(void)
{
        draw_box(1, 12, 78, 48, BOX_THICK | BOX_INNER | BOX_INSET);
}

static void song_changed_cb(void)
{
        unsigned char *line, *prevline;
        int len;

        edit_mode = 0;
	widgets_message[0].accept_text = 0;
        widgets_message[0].d.other.handle_key = message_handle_key_viewmode;
        top_line = 0;
        message = (unsigned char *)song_get_message();

        len = get_nth_line(message, 0, (const unsigned char **)&line);
        while (len >= 0) {
                if (len > LINE_WRAP)
                        message_wrap_line(line);
                prevline = line;
                len = get_nth_line(prevline, 1, (const unsigned char **)&line);
        }

        if (status.current_page == PAGE_MESSAGE)
                status.flags |= NEED_UPDATE;
}

/* --------------------------------------------------------------------- */
static int message_key_hack(struct key_event *k)
{
	if (k->sym == SDLK_ESCAPE && NO_MODIFIER(k->mod) && edit_mode) {
		if (!k->state) return 1;
                message_set_viewmode();
		memused_songchanged();
		return 1;
	}
	return 0;
}

static void message_set_page(void)
{
	if (status.flags & PLAIN_TEXTEDIT) {
		pages[PAGE_MESSAGE].title = "Text Editor (Shift-F9)";
	} else {
		pages[PAGE_MESSAGE].title = "Message Editor (Shift-F9)";
	}
	if (status.flags & PLAIN_TEXTEDIT) {
		message_set_editmode();
	} else {
		/* CHECK: did IT stay in edit mode on set-page? */
	}
}
void message_load_page(struct page *page)
{
	page->title = "Message Editor (Shift-F9)";
	page->set_page = message_set_page;
	page->draw_const = message_draw_const;
	page->song_changed_cb = song_changed_cb;
	page->pre_handle_key = message_key_hack;
	page->total_widgets = 1;
	page->widgets = widgets_message;
	page->help_index = HELP_MESSAGE_EDITOR;

	create_other(widgets_message + 0, 0, message_handle_key_viewmode, message_draw);
	widgets_message[0].accept_text = edit_mode;
}
