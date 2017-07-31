/* Copyright 2016-2017 the Age of Empires Free Software Remake authors. See LEGAL for legal info */

#include "engine.h"

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <libgen.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "_build.h"
#include "dmap.h"
#include "dbg.h"
#include "ui.h"
#include "gfx.h"
#include "sfx.h"
#include "game.h"
#include "prompt.h"

#define GENIE_INIT 1

static unsigned genie_init = 0;
unsigned genie_mode = 0;

static int has_wine = 0;
static int has_wine_dir = 0;

static const char *home_dir = NULL;

char *root_path = NULL;

static const char *version_info =
	"commit: " GENIE_GIT_SHA "\n"
	"origin: " GENIE_GIT_ORIGIN "\n"
	"configured with: " GENIE_BUILD_OPTIONS
#if GENIE_CFG_TAUNTED
	"\nconfiguration is taunted\n"
	"no support will be provided"
#endif
	;

static const char *general_help =
#ifdef DEBUG
	"commit: " GENIE_GIT_SHA "\n"
#endif
	"common options:\n"
	"ch long     description\n"
	" h help     this help\n"
	" r root     load resources from directory\n"
	" v version  dump version info";

/* Supported command line options */
static struct option long_opt[] = {
	{"help", no_argument, 0, 'h'},
	{"root", required_argument, 0, 'r'},
	{"version", no_argument, 0, 'v'},
	{0, 0, 0, 0},
};

/**
 * \brief General exit-handling
 * Cleans up any resources and other stuff.
 */
static void genie_cleanup(void)
{
	if (!genie_init)
		return;

	ge_sfx_free();
	genie_gfx_free();
	genie_ui_free(&genie_ui);
	dmap_list_free();

	genie_init &= ~GENIE_INIT;

	if (genie_init)
		warnx(
			"%s: expected state to be zero, but got %d",
			__func__, genie_init
		);

	genie_init = 0;
}

#define hasopt(x,a,b) (!strcasecmp(x, a b) || !strcasecmp(x, a "_" b) || !strcasecmp(x, a " " b))

static int ge_parse_opt_legacy(int argp, int argc, char *argv[])
{
	for (; argp < argc; ++argp) {
		const char *arg = argv[argp];
		if (hasopt(arg, "no", "startup"))
			genie_mode |= GENIE_MODE_NOSTART;
		else if (hasopt(arg, "system", "memory"))
			genie_mode |= GENIE_MODE_SYSMEM;
		else if (hasopt(arg, "midi", "music"))
			genie_mode |= GENIE_MODE_MIDI;
		else if (!strcasecmp(arg, "msync"))
			genie_mode |= GENIE_MODE_MSYNC;
		else if (!strcasecmp(arg, "mfill"))
			genie_mode |= GENIE_MODE_MFILL;
		else if (hasopt(arg, "no", "sound"))
			genie_mode |= GENIE_MODE_NOSOUND;
		else if (!strcmp(arg, "640"))
			genie_mode |= GENIE_MODE_640_480;
		else if (!strcmp(arg, "800"))
			genie_mode |= GENIE_MODE_800_600;
		else if (!strcmp(arg, "1024"))
			genie_mode |= GENIE_MODE_1024_768;
		else if (hasopt(arg, "no", "music"))
			genie_mode |= GENIE_MODE_NOMUSIC;
		else if (hasopt(arg, "normal", "mouse"))
			genie_mode |= GENIE_MODE_NMOUSE;
		else if (!strcasecmp(arg, "no") && argp + 1 < argc) {
			arg = argv[argp + 1];
			if (!strcasecmp(arg, "startup"))
				genie_mode |= GENIE_MODE_NOSTART;
			else if (!strcasecmp(arg, "sound"))
				genie_mode |= GENIE_MODE_NOSOUND;
			else if (!strcasecmp(arg, "music"))
				genie_mode |= GENIE_MODE_NOMUSIC;
			else
				break;
		} else
			break;
	}
	return argp;
}

int ge_print_options(char *str, size_t size)
{
	size_t n, max = size - 1;
	if (!size)
		return 0;
	*str = '\0';
	if (genie_mode & GENIE_MODE_NOSTART)
		strncat(str, "no_startup ", max);
	if (genie_mode & GENIE_MODE_SYSMEM)
		strncat(str, "system_memory ", max);
	if (genie_mode & GENIE_MODE_MIDI)
		strncat(str, "midi_music ", max);
	if (genie_mode & GENIE_MODE_MSYNC)
		strncat(str, "msync ", max);
	if (genie_mode & GENIE_MODE_MFILL)
		strncat(str, "mfill ", max);
	if (genie_mode & GENIE_MODE_NOSOUND)
		strncat(str, "nosound ", max);
	if (genie_mode & GENIE_MODE_640_480)
		strncat(str, "640 ", max);
	if (genie_mode & GENIE_MODE_800_600)
		strncat(str, "800 ", max);
	if (genie_mode & GENIE_MODE_1024_768)
		strncat(str, "1024 ", max);
	str[(n = strlen(str)) - 1] = '\0';
	return n;
}

/**
 * \brief Process command options and return index to first non-parsed argument
 */
static int ge_parse_opt(int argc, char *argv[], unsigned options)
{
	const char *game_title;
	int c, option_index;

	game_title = genie_ui.game_title;

	while (1) {
		/* Get next argument */
		c = getopt_long(argc, argv, "hr:v", long_opt, &option_index);

		if (c == -1)
			break;

		switch (c) {
		case 'v':
			printf("%s\n%s\n", game_title, version_info);
			exit(0);
			break;
		case 'h':
			printf("%s\n%s\n", game_title, general_help);
			exit(0);
			break;
		case 'r':
			root_path = strdup(optarg);
			if (!root_path) {
				fputs("Out of memory", stderr);
				exit(1);
			}
			break;
		default:
			fprintf(stderr, "Unknown option: 0%o (%c)\n", c, c);
			break;
		}
	}

	if (options & GE_INIT_LEGACY_OPTIONS)
		optind = ge_parse_opt_legacy(optind, argc, argv);

	return optind;
}

int ge_init(int argc, char **argv, const char *title, unsigned options)
{
	int argp;

	if (genie_init) {
		warnx("%s: already initialized", __func__);
		return 0;
	}

	genie_init |= GENIE_INIT;
	genie_ui.game_title = title;
	atexit(genie_cleanup);

	if (argc) {
		char *wd = dirname(argv[0]);
		if (chdir(wd))
			warn("Could not cd to \"%s\"", wd);
	}

	argp = ge_parse_opt(argc, argv, options);
#ifdef DEBUG
	char buf[256];
	ge_print_options(buf, sizeof buf);
	printf("options = \"%s\"\n", buf);
#endif
	return argp;
}

static void init_home_dir(void)
{
	if (home_dir)
		return;
	home_dir = getenv("HOME");
	if (!home_dir) {
		struct passwd *pwd = getpwuid(getuid());
		if (pwd)
			home_dir = pwd->pw_dir;
		else
			err(1, "failed to get pwd");
	}
}

static int is_run_as_root(void)
{
	init_home_dir();
	if (!strncmp(home_dir, "/root", 5) || !getuid()) {
		/* drop privileges before continuing */
		if (setgid(getgid()) || setuid(getuid()))
			err(1, "can't drop root privileges");
		return 1;
	}
	return 0;
}

static void try_find_wine(void)
{
	char path[4096];
	struct stat st;

	init_home_dir();
	if (stat("/usr/bin/wine", &st))
		goto no_wine;
	has_wine = 1;

	snprintf(path, sizeof path, "%s/.wine", home_dir);
	if (stat(path, &st))
		goto no_wine;
	has_wine_dir = 1;
	return;
no_wine:
	warnx("wine executable or wine home not found, falling back to CD-ROM only");
}

int ge_main(void)
{
	int error = 1;

	if (is_run_as_root()) {
		/*
		 * We could continue here since is_run_as_root drops root
		 * privileges if run as root, but we don't want this because
		 * we want to discourage running any game as root in general.
		 */
		show_error("Fatal error", "Cannot run application as root");
		goto fail;
	}
	try_find_wine();

	error = genie_ui_init(&genie_ui, &genie_game);
	if (error)
		goto fail;

	error = dmap_list_init();
	if (error)
		goto fail;

	error = genie_gfx_init();
	if (error)
		goto fail;

	error = ge_sfx_init();
	if (error)
		goto fail;

	genie_game_init(&genie_game, &genie_ui);
	error = genie_game_main(&genie_game);
fail:
	return error;
}