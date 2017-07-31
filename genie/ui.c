/* Copyright 2016-2017 the Age of Empires Free Software Remake authors. See LEGAL for legal info */

#include "ui.h"

#include <err.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "_build.h"
#include "engine.h"
#include "game.h"
#include "gfx.h"
#include "prompt.h"
#include "string.h"

struct genie_ui genie_ui = {
	.game_title = "???",
	.width = 1024, .height = 768,
	.console_show = 0,
};

#define CONSOLE_PS1 "# "

#define UI_INIT 1
#define UI_INIT_SDL 2

static unsigned ui_init = 0;

void genie_ui_free(struct genie_ui *ui)
{
	if (!ui_init)
		return;
	ui_init &= ~UI_INIT;

	if (ui->gl) {
		SDL_GL_DeleteContext(ui->gl);
		ui->gl = NULL;
	}
	if (ui->win) {
		SDL_DestroyWindow(ui->win);
		ui->win = NULL;
	}

	SDL_Quit();
	ui_init &= ~UI_INIT_SDL;

	if (ui_init)
		warnx(
			"%s: expected state to be zero, but got %d",
			__func__, ui_init
		);
}

static int genie_ui_sdl_init(struct genie_ui *ui)
{
	int error = 0;

	if (SDL_Init(SDL_INIT_VIDEO))
		goto sdl_error;

	if (SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1)) {
sdl_error:
		show_error("SDL failed", SDL_GetError());
		error = GENIE_UI_ERR_SDL;
		goto fail;
	}

	ui->win = SDL_CreateWindow("AoE", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ui->width, ui->height, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
	if (!ui->win) {
		show_error("Engine failed", "No window");
		goto fail;
	}

	ui->gl = SDL_GL_CreateContext(ui->win);
	if (!ui->gl) {
		show_error(
			"Engine failed",
			"The graphics card could not be initialized because OpenGL\n"
			"drivers could not be loaded. Make sure your graphics card\n"
			"supports OpenGL 1.1 or better."
		);
		goto fail;
	}

	error = 0;
fail:
	return error;
}

static void console_init(struct console *c)
{
	c->screen.start = c->screen.index = c->screen.size = 0;
}

static void console_add_line(struct console *c, const char *str)
{
	char text[GENIE_CONSOLE_LINE_MAX];
	strncpy0(text, str, GENIE_CONSOLE_LINE_MAX);
	memcpy(c->screen.text[c->screen.index], text, GENIE_CONSOLE_LINE_MAX);

	if (c->screen.size < GENIE_CONSOLE_ROWS)
		++c->screen.size;
	else
		c->screen.start = (c->screen.start + 1) % GENIE_CONSOLE_ROWS;
	c->screen.index = (c->screen.index + 1) % GENIE_CONSOLE_ROWS;
}

static void console_puts(struct console *c, const char *str)
{
	while (1) {
		const char *end = strchr(str, '\n');
		if (!end) {
			puts(str);
			if (*str)
				console_add_line(c, str);
			break;
		}
		char text[GENIE_CONSOLE_LINE_MAX];
		size_t n = end - str + 1;
		text[0] = '\0';
		strncpy0(text, str, n);

		if (n >= sizeof text)
			--n;
		if (n)
			text[n] = '\0';
		puts(text);
		console_add_line(c, text);
		str = end + 1;
	}
}

int genie_ui_init(struct genie_ui *ui, struct genie_game *game)
{
	int error = 0;

	if (ui_init)
		return 0;

	ui_init = UI_INIT;

	error = genie_ui_sdl_init(ui);
	if (error)
		goto fail;

	menu_nav_start.title = ui->game_title;
	genie_ui_menu_push(ui, &menu_nav_start);
	console_init(&ui->console);

	ui->menu_press = 0;
	ui->game = game;

	error = 0;
fail:
	return error;
}

int genie_ui_is_visible(const struct genie_ui *ui)
{
	return ui->win != NULL && ui->gl != NULL;
}

static void genie_ui_update(struct genie_ui *ui)
{
	SDL_GL_SwapWindow(ui->win);
}

static void draw_console(struct genie_ui *ui)
{
	static int tick_cursor = 0, blink_cursor = 0;

	glColor3f(1, 1, 1);

	genie_gfx_draw_text(GENIE_FONT_TILE_WIDTH, GENIE_FONT_TILE_HEIGHT,
		"Age of Empires Free Software Remake console\n"
		"Built with: " GENIE_BUILD_OPTIONS "\n"
		"Commit: " GENIE_GIT_SHA "\n"
		"Press ` to hide the console"
	);

	const struct console *c = &ui->console;
	unsigned i, j, n;
	for (i = c->screen.start, j = 0, n = c->screen.size; j < n; ++j, i = (i + 1) % GENIE_CONSOLE_ROWS)
		genie_gfx_draw_text(
			2 * GENIE_GLYPH_WIDTH,
			(6 + j) * GENIE_GLYPH_HEIGHT,
			c->screen.text[i]
		);
	j += 6;

	genie_gfx_draw_text(
		2 * GENIE_GLYPH_WIDTH,
		j * GENIE_GLYPH_HEIGHT,
		CONSOLE_PS1
	);
	genie_gfx_draw_text(
		(2 + strlen(CONSOLE_PS1)) * GENIE_GLYPH_WIDTH,
		j * GENIE_GLYPH_HEIGHT,
		ui->console.line_buffer
	);
	if (tick_cursor++ >= 10) {
		tick_cursor = 0;
		blink_cursor ^= 1;
	}
	if (blink_cursor)
		genie_gfx_draw_text(
			(2 + strlen(CONSOLE_PS1) + strlen(ui->console.line_buffer)) * GENIE_GLYPH_WIDTH,
			j * GENIE_GLYPH_HEIGHT,
			"|"
		);
}

static void draw_menu(struct genie_ui *ui)
{
	struct menu_nav *nav;
	const struct menu_list *list;
	unsigned i, n;

	nav = genie_ui_menu_peek(ui);

	if (!nav)
		return;

	list = nav->list;
	n = list->count;

	glColor3f(1, 1, 1);

	genie_gfx_draw_text(
		(ui->width - ui->title_width * GENIE_GLYPH_WIDTH) / 2.0,
		GENIE_GLYPH_HEIGHT,
		nav->title
	);

	for (i = 0; i < n; ++i) {
		if (i == nav->index)
			glColor3ub(255, 255, 0);
		else
			glColor3ub(237, 206, 186);

		genie_gfx_draw_text(
			(ui->width - ui->option_width[i] * GENIE_GLYPH_WIDTH) / 2.0,
			308 + 80 * i,
			list->buttons[i]
		);
	}
}

static void console_clear_line_buffer(struct console *c)
{
	c->line_buffer[0] = '\0';
	c->line_count = 0;
}

static const char *help_console =
	"Commands:\n"
	"  help    This help\n"
	"  info    Print statistics\n"
	"  quit    Quit game without confirmation";

static const char *help_quit =
	"quit: Quit game without confirmation\n"
	"  Does not save anything so make sure you are not doing something important!";

static const char *help_info =
	"info: Show information statistics\n"
	"  Print current state of options and statistics for testing purposes.";

static const char *cmd_next_arg(const char *str)
{
	const unsigned char *ptr = (const unsigned char*)str;
	while (*ptr && !isspace(*ptr))
		++ptr;
	while (*ptr && isspace(*ptr))
		++ptr;
	return *ptr ? (const char*)ptr : NULL;
}

static int cmd_has_arg(const char *str)
{
	return cmd_next_arg(str) != NULL;
}

static void console_dump_info(struct console *c)
{
	char text[256];
	const char *scr_mode = "1024x768";
	const char *music = "yes", *sound = "yes";

	if (genie_mode & GENIE_MODE_1024_768)
		scr_mode = "1024x768";
	else if (genie_mode & GENIE_MODE_800_600)
		scr_mode = "800x600";
	else if (genie_mode & GENIE_MODE_640_480)
		scr_mode = "640x480";

	if (genie_mode & GENIE_MODE_NOMUSIC)
		music = "no";
	if (genie_mode & GENIE_MODE_NOSOUND)
		sound = "no";

	snprintf(text, sizeof text,
		"Screen resolution: %s\n"
		"Music playback: %s\n"
		"Sound playback: %s",
		scr_mode, music, sound
	);
	console_puts(c, text);
}

static void console_run(struct console *c, char *str)
{
	char text[1024];
	/* ignore leading and trailing whitespace */
	unsigned char *ptr = (unsigned char*)str;
	while (*ptr && isspace(*ptr))
		++ptr;
	str = (char*)ptr;
	ptr = (unsigned char*)str + strlen(str);
	while (ptr > str && isspace(ptr[-1]))
		--ptr;
	*ptr = '\0';

	/* print the trimmed line that has been entered */
	snprintf(text, sizeof text, "%s%s", CONSOLE_PS1, str);
	console_puts(c, text);

	if (!*str)
		return;
	if (!strcmp(str, "quit"))
		exit(0);
	else if (strsta(str, "help")) {
		const char *arg = cmd_next_arg(str);
		if (!arg) {
			if (strcmp(str, "help"))
				goto unknown;
			console_puts(c, help_console);
		} else if (!strcmp(arg, "quit"))
			console_puts(c, help_quit);
		else if (!strcmp(arg, "info"))
			console_puts(c, help_info);
		else if (!strcmp(arg, "help"))
			console_puts(c, help_console);
		else {
			snprintf(text, sizeof text, "No help for \"%s\"", arg);
			console_puts(c, text);
		}
	} else if (!strcmp(str, "info"))
		console_dump_info(c);
	else {
unknown:
		snprintf(text, sizeof text, "Unknown command: \"%s\"\nType `help' for help", str);
		console_puts(c, text);
	}
}

static void console_line_pop_last(struct console *c)
{
	if (!c->line_count)
		return;
	c->line_buffer[--c->line_count] = '\0';
}

static void console_line_push_last(struct console *c, int ch)
{
	ch &= 0xff;
	if (c->line_count >= GENIE_CONSOLE_LINE_MAX - 1)
		return;
	c->line_buffer[c->line_count++] = ch;
	c->line_buffer[c->line_count] = '\0';
}

static void console_key_down(struct genie_ui *ui, SDL_Event *ev)
{
	struct console *c = &ui->console;
	switch (ev->key.keysym.sym) {
	case '`':
		ui->console_show = 0;
		break;
	case '\r':
	case '\n':
		console_run(c, c->line_buffer);
		console_clear_line_buffer(c);
		break;
	case '\b':
		console_line_pop_last(c);
		break;
	}
	int ch = ev->key.keysym.sym;
	if (ch > 0xff)
		return;
	ch = ch & 0xff;
	if (ch >= ' ' && ch != '`' && ch != '\t' && isprint(ch))
		console_line_push_last(c, ch);
}

static void menu_key_down(struct genie_ui *ui, SDL_Event *ev)
{
	struct menu_nav *nav = genie_ui_menu_peek(ui);
	if (!nav)
		return;
	if (ui->console_show) {
		console_key_down(ui, ev);
		return;
	}

	switch (ev->key.keysym.sym) {
	case SDLK_DOWN:
		menu_nav_down(nav, MENU_KEY_DOWN);
		break;
	case SDLK_UP:
		menu_nav_down(nav, MENU_KEY_UP);
		break;
	case ' ':
		menu_nav_down(nav, MENU_KEY_SELECT);
		ui->menu_press = 1;
		break;
	case '`':
		ui->console_show = 1;
		ui->menu_press = 0;
		break;
	}
}

static void menu_key_up(struct genie_ui *ui, SDL_Event *ev)
{
	struct menu_nav *nav = genie_ui_menu_peek(ui);
	if (!nav)
		return;
	if (ui->console_show)
		return;

	switch (ev->key.keysym.sym) {
	case ' ':
		menu_nav_up(nav, MENU_KEY_SELECT);
		ui->menu_press = 0;
		break;
	}
}

void genie_ui_key_down(struct genie_ui *ui, SDL_Event *ev)
{
	menu_key_down(ui, ev);
}

void genie_ui_key_up(struct genie_ui *ui, SDL_Event *ev)
{
	menu_key_up(ui, ev);
}

void genie_ui_display(struct genie_ui *ui)
{
	genie_gfx_clear_screen(0, 0, 0, 0);
	genie_gfx_setup_ortho(ui->width, ui->height);

	if (ui->console_show)
		draw_console(ui);
	else
		draw_menu(ui);

	genie_ui_update(ui);
}

void genie_ui_menu_update(struct genie_ui *ui)
{
	const struct menu_nav *nav;
	const struct menu_list *list;

	nav = genie_ui_menu_peek(ui);
	list = nav->list;

	for (unsigned i = 0, n = list->count; i < n; ++i)
		ui->option_width[i] = strlen(list->buttons[i]);

	ui->title_width = strlen(nav->title);
}

void genie_ui_menu_push(struct genie_ui *ui, struct menu_nav *nav)
{
	ui->stack[ui->stack_index++] = nav;

	genie_ui_menu_update(ui);
}

void genie_ui_menu_pop(struct genie_ui *ui)
{
	if (!--ui->stack_index) {
		ui->game->running = 0;
		return;
	}

	genie_ui_menu_update(ui);
}

struct menu_nav *genie_ui_menu_peek(const struct genie_ui *ui)
{
	unsigned i;

	i = ui->stack_index;

	return i ? ui->stack[i - 1] : NULL;
}