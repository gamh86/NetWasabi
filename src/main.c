#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "btree.h"
#include "buffer.h"
#include "cache.h"
#include "cache_management.h"
#include "fast_mode.h"
#include "hash_bucket.h"
#include "http.h"
#include "malloc.h"
#include "netwasabi.h"
#include "queue.h"
#include "robots.h"
#include "screen_utils.h"
#include "utils_url.h"
#include "xml.h"

static int get_opts(int, char *[]) __nonnull((2)) __wur;

static char *home_dir = NULL;

/*
 * Global vars.
 */
struct netwasabi_ctx nwctx = {0};

size_t httplen;
size_t httpslen;

pthread_t thread_screen_tid;
pthread_attr_t thread_screen_attr;
pthread_mutex_t screen_mutex;

static queue_obj_t *URL_queue = NULL;
static btree_obj_t *tree_archived = NULL;

static volatile sig_atomic_t screen_updater_stop = 0;

struct winsize winsize;

struct url_types url_types[] =
{
	{ "href=\"", '"', 6 },
	{ "HREF=\"", '"', 6 },
	{ "src=\"", '"', 5 },
	{ "SRC=\"", '"', 5 },
	{ "href=\'", '\'', 6 },
	{ "HREF=\'", '\'', 6 },
	{ "src=\'", '\'', 5 },
	{ "SRC=\'", '\'', 5 },
	{ "thumbnail_src\":\"", '"', 16 },
	{ "src\":\"", '"', 6 },
	{ "", 0, 0 }
};

int path_max = 1024;

static void
__ctor __wr_init(void)
{
	path_max = pathconf("/", _PC_PATH_MAX);

	if (!path_max)
		path_max = 1024;

	httplen = strlen("http://");
	httpslen = strlen("https://");

	home_dir = strdup(getenv("HOME"));

/*
 * For calling fcntl() once only in buf_read_socket/tls()
 * to set O_NONBLOCK flag. On a reconnect, reset to zero.
 */
	pthread_mutex_init(&screen_mutex, NULL);

	return;
}

static void
__dtor __wr_fini(void)
{
	pthread_mutex_destroy(&screen_mutex);
}

#define THREAD_SLEEP_TIME_USEC 500000
void *
screen_updater_thread(void *arg)
{
	int go_right = 1;

	static char *string_collection[] =
	{
		"Things you own end up owning you.",
		"Be a better person than you were yesterday.",
		"Welcome to the desert of the real.",
		"Where others have failed, I will not fail.",
		"We're the all-singing, all-dancing crap of the world.",
		"Never send a human to do a machine's job.",
		"There is nothing so eternally adhesive as the memory of power.",
		"We're all living in each other's paranoia.",
		"Somewhere, something incredible is waiting to be known.",
		"To the poet a pearl is a tear of the sea.",
		NULL
	};

	int string_idx = 0;
	int max_right;
	size_t len;

	len = strlen(string_collection[0]);
	max_right = (OUTPUT_TABLE_COLUMNS - (int)len);

	while (!screen_updater_stop)
	{
		usleep(THREAD_SLEEP_TIME_USEC);

		pthread_mutex_lock(&screen_mutex);

		reset_left();
		up(1);
		clear_line();
		right(go_right);
		fprintf(stderr, "%s%.*s%s", COL_DARKCYAN, (int)len, string_collection[string_idx], COL_END);
		reset_left();
		down(1);

		pthread_mutex_unlock(&screen_mutex);

		++go_right;

		if (go_right > max_right)
		{
			--len;

			if ((ssize_t)len < 0)
			{
				go_right = 1;
				++string_idx;

				if (string_collection[string_idx] == NULL)
					string_idx = 0;

				len = strlen(string_collection[string_idx]);

				max_right = (OUTPUT_TABLE_COLUMNS - (int)len);

				sleep(1);
			}
		}
	}

	pthread_exit((void *)0);
}

/*
 * Catch SIGINT
 */
sigjmp_buf main_env;
struct sigaction new_act;
struct sigaction old_sigint;
struct sigaction old_sigquit;

static void
__noret usage(int exit_status)
{
	fprintf(stderr,
		"netwasabi <url> [options]\n\n"
		"-cD/--crawl-delay       Delay (seconds) between each request (default is\n"
		"                        3 seconds; ignored if in fast mode).\n\n"
		"-D/--depth              Set maximum crawl-depth (one \"layer\" of depth\n"
		"                        is defined as emptying a single cache of all its\n"
		"                        URLs and switching to the sibling cache.\n\n"
		"--cache-set-threshold   Set threshold above which no more URLs will\n"
		"                        be added to the sibling cache while draining\n"
		"                        the other.\n\n"
		"--cache-no-threshold    Remove threshold completely from the cache;\n"
		"                        an unlimited number of URLs can be placed in the\n"
		"                        sibling cache while draining the other.\n\n"
		"-fm/--fast-mode         Request more than one URL per second\n"
		"                        (this option supercedes any crawl delay\n"
		"                        specified).\n\n"
		"-X/--xdomain            Follow URLs into other domains.\n\n"
		"-B/--blacklist          Blacklist tokens in URLs.\n\n"
		"-T/--tls                Use a TLS connection.\n\n"
		"--help/-h               Display this information\n");

	exit(exit_status);
}

static void
__print_information_layout(void)
{
	fprintf(stderr,
		"\n\n"
		"         ;;;   ;;  ;;;;; ;;;;;;;;   ,; ;; ;,   ;;;;    ;;;;;   ;;;;    ;;;;;;   ,;,  \n"
		"         ;;;;  ;; ;;        ;;      ;; ;; ;;  ;;  ;;  ;;      ;;  ;;  ;;;    ;  ;;;  \n"
		"         ;; ;; ;; ;;;;;;    ;;      ;; ;; ;;  ;;;;;;  ;;;;;   ;;;;;;  ;;;;;;;   ;;;  \n"
		"         ;;   ;;; ;;        ;;      ;; ;; ;;  ;;  ;;      ;;  ;;  ;;  ;;;    ;  ;;;  \n"
		"         ;;    ;;  ;;;;;    ;;       ;;;;;;   ;;  ;;  ;;;;;   ;;  ;;   ;;;;;;   ;;;  \n\n"
		"   %sv%s%s\n\n",
		COL_DARKRED,
		NETWASABI_BUILD,
		COL_END);

#define COL_HEADINGS COL_DARKORANGE
if (!option_set(OPT_FAST_MODE))
{
	fprintf(stderr,
	" ==========================================================================================\n"
	"  %sDisconnected%s\n"
	" ==========================================================================================\n"
  "  %sCache 1%s: %4d | %sCache 2%s: %4d | %sData%s: %12lu B | %sCrawl-Delay%s: %ds | %sStatus%s: %d\n"
	"   %s%10s%s   | %s%10s%s    |                      |                 |                     \n"
	" ------------------------------------------------------------------------------------------\n"
	"\n"
	"\n" /* current URL goes here */
	"\n" /* general status messages can go here */
	" ==========================================================================================\n\n",
	COL_LIGHTGREY, COL_END,
	COL_HEADINGS, COL_END, (int)0, COL_HEADINGS, COL_END, (int)0, COL_HEADINGS, COL_END, (size_t)0,
	COL_HEADINGS, COL_END, crawl_delay(&nwctx), COL_HEADINGS, COL_END, 0,
	COL_DARKGREEN, "(filling)", COL_END, COL_LIGHTGREY, "(empty)", COL_END);
}
else
{
	fprintf(stderr,
	" ==========================================================================================\n"
	"  %sDisconnected%s\n"
	" ==========================================================================================\n"
  "  %sCache 1%s: %4d | %sCache 2%s: %4d | %sData%s: %12lu B | %s🗲%s  FAST MODE %s🗲%s  | %sStatus%s: %d\n"
	"   %s%10s%s   | %s%10s%s    |                      |   (%d threads)   |                     \n"
	" ------------------------------------------------------------------------------------------\n"
	"\n"
	"\n" /* current URL goes here */
	"\n" /* general status messages can go here */
	" ==========================================================================================\n\n",
	COL_LIGHTGREY, COL_END,
	COL_HEADINGS, COL_END, (int)0, COL_HEADINGS, COL_END, (int)0, COL_HEADINGS, COL_END, (size_t)0,
	COL_HEADINGS, COL_END, COL_HEADINGS, COL_END, COL_HEADINGS, COL_END, 0,
	COL_DARKGREEN, "(filling)", COL_END, COL_LIGHTGREY, "(empty)", COL_END,
	FAST_MODE_NR_WORKERS);
}

	return;
}

//static bucket_obj_t *bObj = NULL;

static void
get_config(void)
{
	char config_file[1024];

	sprintf(config_file, "%s/.NetWasabi/config.xml", home_dir);
	if (access(config_file, F_OK) != 0)
		return;

	xml_tree_t *tree = parse_xml_file(config_file);

	if (!tree)
		return;

	xml_node_t *node = XML_find_node(tree, "options");
	if (!node)
		goto fail;

	fprintf(stderr, "Found node with value \"options\": %s\n", XML_NODE_VALUE(node));

#if 0

	bObj = BUCKET_object_new();
	assert(bObj);
#endif

	//free_xml_tree(tree);
fail:
	free_xml_tree(tree);
	//BUCKET_object_destroy(bObj);

	return;
}

static void
catch_signal(int signo)
{
	if (signo != SIGINT && signo != SIGQUIT)
		return;

	siglongjmp(main_env, 1);
}

static void
check_directory(void)
{
	buf_t tmp;

	buf_init(&tmp, path_max);
	buf_append(&tmp, home_dir);
	buf_append(&tmp, "/" NETWASABI_DIR);

	if (access(tmp.buf_head, F_OK) != 0)
		mkdir(tmp.buf_head, S_IRWXU);

	buf_destroy(&tmp);

	return;
}

#if 0
static int
__get_robots(connection_t *conn)
{
	assert(http);

	int status_code = 0;

	update_operation_status("Requesting robots.txt file from server");

	strcpy(http->page, "robots.txt");

	buf_t full_url;

	buf_init(&full_url, HTTP_URL_MAX);

	if (option_set(OPT_USE_TLS))
		buf_append(&full_url, "https://");
	else
		buf_append(&full_url, "http://");

	assert(http->host[0]);
	buf_append(&full_url, http->host);
	buf_append(&full_url, "/robots.txt");

	assert(full_url.data_len < HTTP_URL_MAX);
	strcpy(http->full_url, full_url.buf_head);

	buf_destroy(&full_url);
	nwctx.got_token_graph = 0;

	status_code = __do_request(conn);

	switch(status_code)
	{
		case HTTP_OK:
			update_operation_status("Got robots.txt file");
			break;
		default:
			update_operation_status("No robots.txt file");
	}

/*
 * This initialises the graphs.
 */
	allowed = NULL;
	forbidden = NULL;

	if (create_token_graphs(&allowed, &forbidden, &http_rbuf(http)) < 0)
	{
		put_error_msg("Failed to create graph for URL tokens");
		goto out_destroy_graphs;
	}

	nwctx.got_token_graph = 1;
	return 0;

	out_destroy_graphs:

	if (allowed)
		destroy_graph(allowed);

	if (forbidden)
		destroy_graph(forbidden);

	return 0;
}
#endif

static int
valid_url(char *url)
{
	assert(url);

	if (strlen(url) >= HTTP_URL_MAX)
		return 0;

	if (!strstr(url, "http://") && !strstr(url, "https://"))
		return 0;

	if (!memchr(url, '.', strlen(url)))
		return 0;

	if (strstr(url, "mailto"))
		return 0;

	return 1;
}

/*
 * ./netwasabi <url> [options]
 */
int
main(int argc, char *argv[])
{
	if (argc < 2)
	{
		usage(EXIT_FAILURE);
	}

	get_config();
	return 0;

	if (get_opts(argc, argv) < 0)
	{
		fprintf(stderr, "main: failed to parse program options\n");
		goto fail;
	}

	if (!valid_url(argv[1]))
	{
		fprintf(stderr, "\"%s\" is not a valid URL\n", argv[1]);
		goto fail;
	}

	/*
	 * Must be done here and not in the constructor function
	 * because the dimensions are not known before main()
	 */
	clear_struct(&winsize);
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsize);

/*
 * Print the operation display box.
 */
	__print_information_layout();

	//pthread_attr_setdetachstate(&thread_screen_attr, PTHREAD_CREATE_DETACHED);
	//pthread_create(&thread_screen_tid, &thread_screen_attr, screen_updater_thread, NULL);

/*
 * Check for existence of the WR_Reaped directory
 * in the user's home directory.
 */
	check_directory();

	if (option_set(OPT_FAST_MODE))
	{
		do_fast_mode(argv[1]);
		goto out;
	}

/*
 * Set up signal handlers for SIGINT and SIGQUIT
 * to avoid segmentation faults when the user
 * does ctrl^C/ctrl^\ at a bad time.
 */
	clear_struct(&new_act);
	clear_struct(&old_sigint);
	clear_struct(&old_sigquit);

	new_act.sa_flags = 0;
	new_act.sa_handler = catch_signal;
	sigemptyset(&new_act.sa_mask);

	if (sigaction(SIGINT, &new_act, &old_sigint) < 0)
	{
		put_error_msg("main: failed to set SIGINT handler (%s)", strerror(errno));
		goto fail;
	}

	if (sigaction(SIGQUIT, &new_act, &old_sigquit) < 0)
	{
		put_error_msg("main: failed to set SIGQUIT handler (%s)", strerror(errno));
		goto fail;
	}

	struct http_t *http;
	int rv;
	size_t url_len;
	buf_t *rbuf = NULL;
	buf_t *wbuf = NULL;

#define MAIN_THREAD_ID 0x4e49414du
	if (!(http = HTTP_new(MAIN_THREAD_ID)))
	{
		fprintf(stderr, "main: failed to obtain new HTTP object\n");
		goto fail;
	}

	http->followRedirects = 1;
	http->usingSecure = 1;
	http->verb = GET;

	url_len = strlen(argv[1]);
	assert(url_len < HTTP_URL_MAX);
	strcpy(http->URL, argv[1]);
	http->URL_len = url_len;

	http->ops->URL_parse_host(argv[1], http->host);
	http->ops->URL_parse_page(argv[1], http->page);

	strcpy(http->primary_host, http->host);

	if (http_connect(http) < 0)
		goto fail;

	rbuf = &http_rbuf(http);
	wbuf = &http_wbuf(http);

	/*
	 * Catch SIGINT and SIGQUIT so we can release cache memory, etc.
	 */
	if (sigsetjmp(main_env, 0) != 0)
	{
		fprintf(stderr, "%c%c%c%c%c%c", 0x08, 0x20, 0x08, 0x08, 0x20, 0x08);
		put_error_msg("Signal caught");
		goto out_disconnect;
	}

	/*
	 * Check if the webserver has a robots.txt file
	 * and if so, use it to create a directed network
	 * of URL tokens.
	 *
	 * XXX	Update: We will create a regex module
	 *	specifically for dealing abiding by
	 *	the rules in the robots.txt file
	 *	on webservers. Also, we can then
	 *	use it for other features such
	 *	as only archiving pages containing
	 *	certain text, etc.
	 */
	//if (__get_robots(&conn) < 0)
		//put_error_msg("No robots.txt file");

	buf_clear(rbuf);
	buf_clear(wbuf);

	update_current_url(http->URL);

	URL_queue = QUEUE_object_new();
	tree_archived = BTREE_object_new();

	http->ops->send_request(http);
	http->ops->recv_response(http);

	update_status_code(http->code);

	if (HTTP_OK != http->code)
	{
		fprintf(stderr, "Error (%d %s)\n", http->code, http->ops->code_as_string(http));
		goto out_disconnect;
	}

	BTREE_put_data(tree_archived, (void *)http->URL, http->URL_len);

	if (URL_parseable(http->URL))
	{
		parse_URLs(http, URL_queue, tree_archived);
		transform_document_URLs(http);
		archive_page(http);
	}
	else
	{
		update_operation_status("No URLs to parse from document");
		goto out_disconnect;
	}

	rv = Crawl_WebSite(http, URL_queue, tree_archived);

	if (rv < 0)
	{
		goto fail_disconnect;
	}

	update_operation_status("Finished crawling site");

out_disconnect:

	http_disconnect(http);
	HTTP_delete(http);

	sigaction(SIGINT, &old_sigint, NULL);
	sigaction(SIGQUIT, &old_sigquit, NULL);

out:

	screen_updater_stop = 1;

	usleep(100000);
	exit(EXIT_SUCCESS);

fail_disconnect:

	screen_updater_stop = 1;
	http_disconnect(http);
	HTTP_delete(http);

fail:

	fprintf(stderr, "Failed...\n");
	sigaction(SIGINT, &old_sigint, NULL);
	sigaction(SIGQUIT, &old_sigquit, NULL);

	exit(EXIT_FAILURE);
}

int
get_opts(int argc, char *argv[])
{
	int		i;

	cache_thresh(&nwctx) = CACHE_DEFAULT_THRESHOLD;

	for (i = 1; i < argc; ++i)
	{
		while (i < argc && argv[i][0] != '-')
			++i;

		if (i == argc)
			break;

		if (!strcmp("--help", argv[i])
			|| !strcmp("-h", argv[i]))
		{
			usage(EXIT_SUCCESS);
		}
		else
		if (!strcmp("--depth", argv[i])
		|| !strcmp("-D", argv[i]))
		{
			++i;

			if (i == argc || argv[i][0] == '-')
			{
				fprintf(stderr, "-D/--depth requires an argument\n");
				usage(EXIT_FAILURE);
			}

			crawl_depth(&nwctx) = atoi(argv[i]);
			assert(crawl_depth(&nwctx) > 0);
			assert(crawl_depth(&nwctx) <= INT_MAX);
		}
		else
		if (!strcmp("--crawl-delay", argv[i])
		|| !strcmp("-cD", argv[i]))
		{
			++i;

			if (i == argc || argv[i][0] == '-')
			{
				fprintf(stderr, "-cD/--crawl-delay requires an argument\n");
				usage(EXIT_FAILURE);
			}

			crawl_delay(&nwctx) = atoi(argv[i]);
			assert(crawl_delay(&nwctx) >= 0);
			assert(crawl_delay(&nwctx) < MAX_CRAWL_DELAY);
		}
		else
		if (!strcmp("--fast-mode", argv[i])
		|| !strcmp("-fm", argv[i]))
		{
			set_option(OPT_FAST_MODE);
			assert(option_set(OPT_FAST_MODE));
		}
#if 0
		else
		if (!strcmp("--blacklist", argv[i])
		|| !strcmp("-B", argv[i]))
		{
			int nr_tokens = 10;
			int idx = 0;
			size_t token_len;
			USER_BLACKLIST_NR_TOKENS = 0;

			++i;

			if (i == argc || !strncmp("--", argv[i], 2) || !strncmp("-", argv[i], 1))
			{
				fprintf(stderr, "--blacklist/-B requires an argument\n");
				usage(EXIT_FAILURE);
			}

			MATRIX_INIT(user_blacklist, nr_tokens, TOKEN_MAX, char);

			while (i < argc && strncmp("--", argv[i], 2) && strncmp("-", argv[i], 1))
			{
				token_len = strlen(argv[i]);
				assert(token_len < TOKEN_MAX);

				MATRIX_CHECK_CAPACITY(user_blacklist, idx, nr_tokens, TOKEN_MAX, char);

				strncpy(user_blacklist[idx], argv[i], token_len);
				user_blacklist[idx][token_len] = 0;

				++USER_BLACKLIST_NR_TOKENS;
				++idx;
				++i;
			}

			--i;
		}
#endif
		else
		if (!strcmp("--xdomain", argv[i])
			|| !strcmp("-X", argv[i]))
		{
			set_option(OPT_ALLOW_XDOMAIN);
		}
		else
		if (!strcmp("--cache-no-threshold", argv[i]))
		{
			unset_option(OPT_CACHE_THRESHOLD);
			assert(!option_set(OPT_CACHE_THRESHOLD));
		}
		else
		if (!strcmp("--cache-set-threshold", argv[i]))
		{
			++i;

			if (i == argc || argv[i][0] == '-')
			{
				fprintf(stderr, "--cache-set-threshold requires an argument\n");
				usage(EXIT_FAILURE);
			}

			cache_thresh(&nwctx) = (unsigned int)atoi(argv[i+1]);
			set_option(OPT_CACHE_THRESHOLD);
		}
#if 0
		else
		if (!strcmp("-oH", argv[i])
			|| !strcmp("--req-head", argv[i]))
		{
			set_option(OPT_SHOW_REQ_HEADER);
		}
		else
		if (!strcmp("-iH", argv[i])
			|| !strcmp("--res-head", argv[i]))
		{
			set_option(OPT_SHOW_RES_HEADER);
		}
#endif
		else
		if (!strcmp("-T", argv[i])
			|| strcmp("--tls", argv[i]))
		{
			set_option(OPT_USE_TLS);
		}
		else
		{
			continue;
		}
	}

	if (crawl_delay(&nwctx) > 0 && option_set(OPT_FAST_MODE))
	{
			crawl_delay(&nwctx) = 0;
	}

	if (!crawl_depth(&nwctx))
		crawl_depth(&nwctx) = CRAWL_DEPTH_DEFAULT;

	return 0;
}
