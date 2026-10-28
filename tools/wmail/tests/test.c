/* tests/test.c - wmail-sync unit tests.
 *
 * No network, no D-Bus, no wily: GOA parsing runs on a canned busctl
 * reply, IMAP runs against a scripted socketpair peer, maildir runs in
 * a temp dir. */
#include "../goa.h"
#include "../imap.h"
#include "../json.h"
#include "../maildir.h"
#include "../util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0, checks = 0;

#define CHECK(cond, ...) do { \
	checks++; \
	if (cond) { printf("ok %d - ", checks); printf(__VA_ARGS__); printf("\n"); } \
	else { failures++; printf("not ok %d - ", checks); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* ---------- GOA ---------- */

static const char *goa_reply =
	"{\"type\":\"(a{oa{sa{sv}}})\",\"data\":[["
	"[\"/org/gnome/OnlineAccounts/Accounts/1\",{"
	"\"org.gnome.OnlineAccounts.Account\":{"
	"\"Id\":{\"type\":\"s\",\"data\":\"goa_account_1\"},"
	"\"ProviderType\":{\"type\":\"s\",\"data\":\"google\"},"
	"\"ProviderName\":{\"type\":\"s\",\"data\":\"Google\"},"
	"\"MailEnabled\":{\"type\":\"b\",\"data\":true}},"
	"\"org.gnome.OnlineAccounts.Mail\":{"
	"\"EmailAddress\":{\"type\":\"s\",\"data\":\"alice@example.com\"},"
	"\"ImapHost\":{\"type\":\"s\",\"data\":\"imap.example.com\"},"
	"\"ImapUserName\":{\"type\":\"s\",\"data\":\"alice@example.com\"},"
	"\"ImapUseSsl\":{\"type\":\"b\",\"data\":true},"
	"\"ImapUseTls\":{\"type\":\"b\",\"data\":false},"
	"\"SmtpHost\":{\"type\":\"s\",\"data\":\"smtp.example.com\"},"
	"\"SmtpUserName\":{\"type\":\"s\",\"data\":\"alice@example.com\"},"
	"\"SmtpUseSsl\":{\"type\":\"b\",\"data\":true},"
	"\"SmtpUseTls\":{\"type\":\"b\",\"data\":false},"
	"\"SmtpUseAuth\":{\"type\":\"b\",\"data\":true},"
	"\"SmtpAuthXoauth2\":{\"type\":\"b\",\"data\":true}},"
	"\"org.gnome.OnlineAccounts.OAuth2Based\":{}}],"
	"[\"/org/gnome/OnlineAccounts/Accounts/2\",{"
	"\"org.gnome.OnlineAccounts.Account\":{"
	"\"Id\":{\"type\":\"s\",\"data\":\"goa_account_2\"},"
	"\"ProviderType\":{\"type\":\"s\",\"data\":\"generic\"},"
	"\"ProviderName\":{\"type\":\"s\",\"data\":\"Generic\"},"
	"\"MailDisabled\":{\"type\":\"b\",\"data\":false}},"
	"\"org.gnome.OnlineAccounts.Mail\":{"
	"\"EmailAddress\":{\"type\":\"s\",\"data\":\"bob@example.org\"},"
	"\"ImapHost\":{\"type\":\"s\",\"data\":\"imap.example.org\"},"
	"\"ImapUserName\":{\"type\":\"s\",\"data\":\"bob\"},"
	"\"ImapUseSsl\":{\"type\":\"b\",\"data\":false},"
	"\"ImapUseTls\":{\"type\":\"b\",\"data\":true},"
	"\"SmtpHost\":{\"type\":\"s\",\"data\":\"smtp.example.org\"},"
	"\"SmtpUserName\":{\"type\":\"s\",\"data\":\"bob\"},"
	"\"SmtpUseSsl\":{\"type\":\"b\",\"data\":false},"
	"\"SmtpUseTls\":{\"type\":\"b\",\"data\":true},"
	"\"SmtpUseAuth\":{\"type\":\"b\",\"data\":true},"
	"\"SmtpAuthXoauth2\":{\"type\":\"b\",\"data\":false}},"
	"\"org.gnome.OnlineAccounts.PasswordBased\":{}}],"
	"[\"/org/gnome/OnlineAccounts/Accounts/3\",{"
	"\"org.gnome.OnlineAccounts.Account\":{"
	"\"Id\":{\"type\":\"s\",\"data\":\"goa_account_3\"},"
	"\"MailDisabled\":{\"type\":\"b\",\"data\":true}},"
	"\"org.gnome.OnlineAccounts.Mail\":{"
	"\"EmailAddress\":{\"type\":\"s\",\"data\":\"carol@example.net\"}}}],"
	"[\"/org/gnome/OnlineAccounts/Accounts/4\",{"
	"\"org.gnome.OnlineAccounts.Account\":{"
	"\"Id\":{\"type\":\"s\",\"data\":\"goa_account_4\"}}}]"
	"]]}";

static void
test_goa(void)
{
	char *err = NULL;
	Json *root = json_parse(goa_reply, &err);
	GoaAccount *accs = NULL;
	int n = 0, r;

	CHECK(root != NULL, "goa json parses");
	if (!root) {
		printf("# parse error: %s\n", err ? err : "?");
		return;
	}
	r = goa_accounts_from_json(root, &accs, &n, &err);
	CHECK(r == 0, "goa accounts extracted");
	CHECK(n == 2, "goa keeps 2 mail-enabled accounts (got %d)", n);
	if (n >= 2) {
		CHECK(strcmp(accs[0].email, "alice@example.com") == 0,
		    "goa account 0 email");
		CHECK(accs[0].oauth2 && !accs[0].password_based,
		    "goa account 0 is oauth2");
		CHECK(accs[0].imap_ssl && !accs[0].imap_tls,
		    "goa account 0 imap ssl");
		CHECK(accs[0].smtp_xoauth2, "goa account 0 smtp xoauth2");
		CHECK(strcmp(accs[1].email, "bob@example.org") == 0,
		    "goa account 1 email");
		CHECK(!accs[1].oauth2 && accs[1].password_based,
		    "goa account 1 is password based");
		CHECK(!accs[1].imap_ssl && accs[1].imap_tls,
		    "goa account 1 imap starttls");
	}
	goa_free_accounts(accs, n);
	json_free(root);
}

/* ---------- IMAP ---------- */

static void
srv_puts(int fd, const char *s)
{
	size_t len = strlen(s), sent = 0;
	while (sent < len) {
		ssize_t n = write(fd, s + sent, len - sent);
		if (n <= 0)
			_exit(1);
		sent += (size_t)n;
	}
}

static void
srv_fetch_msg(int fd, int seq, unsigned long uid, const char *flags,
    const char *body)
{
	char *head = xstrfmt("* %d FETCH (UID %lu FLAGS (%s) BODY[] {%zu}\r\n",
	    seq, uid, flags, strlen(body));
	srv_puts(fd, head);
	free(head);
	srv_puts(fd, body);
	srv_puts(fd, "\r\n)\r\n");
}

static int
srv_read_line(int fd, char *buf, size_t cap)
{
	size_t len = 0;
	for (;;) {
		char c;
		ssize_t n = read(fd, &c, 1);
		if (n <= 0)
			return -1;
		if (c == '\n')
			break;
		if (len + 1 < cap)
			buf[len++] = c;
	}
	buf[len] = 0;
	if (len > 0 && buf[len - 1] == '\r')
		buf[len - 1] = 0;
	return 0;
}

/* Scripted server: greeting, LOGIN, SELECT, UID FETCH flags, UID FETCH
 * bodies, LOGOUT. Tags are echoed from the client's commands. */
static void
imap_server(int fd)
{
	char line[1024], tag[32];

	srv_puts(fd, "* OK Fake IMAP ready\r\n");
	while (srv_read_line(fd, line, sizeof line) == 0) {
		char *sp = strchr(line, ' ');
		size_t tl = sp ? (size_t)(sp - line) : 0;
		if (tl >= sizeof tag)
			tl = sizeof tag - 1;
		memcpy(tag, line, tl);
		tag[tl] = 0;
		if (strstr(line, "LOGIN")) {
			srv_puts(fd, tag);
			srv_puts(fd, " OK Logged in\r\n");
		} else if (strstr(line, "SELECT")) {
			srv_puts(fd, "* 3 EXISTS\r\n");
			srv_puts(fd, "* OK [UIDVALIDITY 12345]\r\n");
			srv_puts(fd, "* OK [UIDNEXT 104]\r\n");
			srv_puts(fd, tag);
			srv_puts(fd, " OK Selected\r\n");
		} else if (strstr(line, "UID FETCH") &&
		    strstr(line, "BODY[]")) {
			srv_fetch_msg(fd, 1, 101, "\\Seen",
			    "Subject: one\r\n\r\nBody one.");
			srv_fetch_msg(fd, 2, 102, "\\Seen \\Flagged",
			    "Subject: two\r\n\r\nBody two.");
			srv_fetch_msg(fd, 3, 103, "",
			    "Subject: three\r\n\r\nBody three.");
			srv_puts(fd, tag);
			srv_puts(fd, " OK Fetched\r\n");
		} else if (strstr(line, "UID FETCH")) {
			srv_puts(fd, "* 1 FETCH (UID 101 FLAGS (\\Seen))\r\n");
			srv_puts(fd, "* 2 FETCH (UID 102 FLAGS (\\Seen \\Flagged))\r\n");
			srv_puts(fd, "* 3 FETCH (UID 103 FLAGS ())\r\n");
			srv_puts(fd, tag);
			srv_puts(fd, " OK Listed\r\n");
		} else if (strstr(line, "LOGOUT")) {
			srv_puts(fd, "* BYE Logging out\r\n");
			srv_puts(fd, tag);
			srv_puts(fd, " OK Logout\r\n");
			_exit(0);
		} else {
			srv_puts(fd, tag);
			srv_puts(fd, " BAD Unknown\r\n");
		}
	}
	_exit(1);
}

typedef struct {
	int n;
	unsigned long uids[8];
	char bodies[8][64];
	char flags[8][64];
} GotMsgs;

static void
got_cb(void *ctx, unsigned long uid, const char *flags, const char *body,
    size_t len)
{
	GotMsgs *g = ctx;
	int i = g->n;
	if (i >= 8)
		return;
	g->uids[i] = uid;
	snprintf(g->flags[i], sizeof g->flags[i], "%s", flags);
	if (len >= sizeof g->bodies[i])
		len = sizeof g->bodies[i] - 1;
	memcpy(g->bodies[i], body, len);
	g->bodies[i][len] = 0;
	g->n++;
}

static void
test_imap(void)
{
	int sv[2];
	pid_t pid;
	char *err = NULL;
	Imap *im;
	unsigned long uv = 0, un = 0, *uids = NULL;
	char **flags = NULL;
	int n = 0, i, status;
	unsigned long want[3] = { 101, 102, 103 };
	GotMsgs got;

	memset(&got, 0, sizeof got);
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
		CHECK(0, "imap socketpair");
		return;
	}
	pid = fork();
	if (pid == 0) {
		close(sv[0]);
		imap_server(sv[1]);
		_exit(1);
	}
	close(sv[1]);
	im = imap_from_fd(sv[0]);
	CHECK(imap_greeting(im, &err) == 0, "imap greeting");
	CHECK(imap_login(im, "alice@example.com", "secret", &err) == 0,
	    "imap login");
	CHECK(imap_select(im, "INBOX", &uv, &un, &err) == 0,
	    "imap select");
	CHECK(uv == 12345 && un == 104,
	    "imap select values (%lu/%lu)", uv, un);
	CHECK(imap_uid_flags(im, &uids, &flags, &n, &err) == 0,
	    "imap uid flags");
	CHECK(n == 3, "imap uid flags count (%d)", n);
	for (i = 0; i < n && i < 3; i++)
		CHECK(uids[i] == want[i], "imap uid %d is %lu", i, uids[i]);
	if (n >= 3) {
		CHECK(strcmp(flags[0], "\\Seen") == 0, "imap flags 0");
		CHECK(strcmp(flags[1], "\\Seen \\Flagged") == 0,
		    "imap flags 1");
		CHECK(strcmp(flags[2], "") == 0, "imap flags 2");
	}
	CHECK(imap_uid_fetch(im, want, 3, got_cb, &got, &err) == 0,
	    "imap uid fetch");
	CHECK(got.n == 3, "imap fetched 3 bodies (got %d)", got.n);
	if (got.n == 3) {
		CHECK(got.uids[0] == 101 &&
		    strstr(got.bodies[0], "Body one.") != NULL,
		    "imap body 0");
		CHECK(got.uids[1] == 102 &&
		    strcmp(got.flags[1], "\\Seen \\Flagged") == 0,
		    "imap body 1 flags");
		CHECK(got.uids[2] == 103 &&
		    strstr(got.bodies[2], "Body three.") != NULL,
		    "imap body 2");
	}
	CHECK(imap_logout(im) == 0, "imap logout");
	imap_free(im);
	for (i = 0; i < n; i++)
		free(flags[i]);
	free(flags);
	free(uids);
	while (waitpid(pid, &status, 0) < 0)
		;
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
	    "imap server exited clean");
}

/* ---------- maildir ---------- */

static int
file_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

static void
test_maildir(void)
{
	char tmpl[] = "/tmp/wmail-test-XXXXXX";
	char *dir, *err = NULL, *name1 = NULL, *name2 = NULL, *newname = NULL;
	char md[16];
	MailState st, st2;
	const char *body = "Subject: hi\r\n\r\nHello.\r\n";

	CHECK(mkdtemp(tmpl) != NULL, "maildir mkdtemp");
	dir = tmpl;
	CHECK(maildir_init(dir, &err) == 0, "maildir init");
	CHECK(maildir_store(dir, body, strlen(body), "", &name1, &err) == 0,
	    "maildir store unseen");
	{
		char *p = xstrfmt("%s/new/%s", dir, name1);
		CHECK(file_exists(p), "maildir unseen lands in new/");
		free(p);
	}
	CHECK(maildir_store(dir, body, strlen(body), "S", &name2, &err) == 0,
	    "maildir store seen");
	{
		char *p = xstrfmt("%s/cur/%s", dir, name2);
		CHECK(file_exists(p), "maildir seen lands in cur/");
		free(p);
	}
	/* Reflag unseen -> seen: new/ -> cur/ with :2,S suffix. */
	CHECK(maildir_reflag(dir, name1, "S", &newname, &err) == 0,
	    "maildir reflag to seen");
	{
		char *p = xstrfmt("%s/cur/%s", dir, newname);
		CHECK(file_exists(p), "maildir reflagged file in cur/");
		free(p);
		CHECK(strstr(newname, ":2,S") != NULL,
		    "maildir reflagged name has :2,S");
	}
	free(newname);
	/* Flag mapping. */
	imap_flags_to_maildir("\\Seen \\Flagged", md, sizeof md);
	CHECK(strcmp(md, "FS") == 0, "maildir flag map FS (got %s)", md);
	imap_flags_to_maildir("\\Answered", md, sizeof md);
	CHECK(strcmp(md, "R") == 0, "maildir flag map R");
	imap_flags_to_maildir("", md, sizeof md);
	CHECK(strcmp(md, "") == 0, "maildir flag map empty");
	/* State round-trip. */
	memset(&st, 0, sizeof st);
	state_put(&st, 101, name1, "");
	state_put(&st, 102, name2, "S");
	st.uidvalidity = 12345;
	st.uidnext = 104;
	{
		char *sp = xstrfmt("%s/.wmail-state", dir);
		CHECK(state_save(sp, &st) == 0, "maildir state save");
		memset(&st2, 0, sizeof st2);
		CHECK(state_load(sp, &st2) == 0, "maildir state load");
		CHECK(st2.uidvalidity == 12345 && st2.uidnext == 104,
		    "maildir state header");
		CHECK(state_find(&st2, 101) != NULL &&
		    state_find(&st2, 102) != NULL,
		    "maildir state entries");
		free(sp);
		state_free(&st2);
	}
	state_free(&st);
	free(name1);
	free(name2);
	{
		char *cmd = xstrfmt("rm -rf %s", dir);
		if (system(cmd) != 0)
			printf("# cleanup failed\n");
		free(cmd);
	}
}

int
main(void)
{
	test_goa();
	test_imap();
	test_maildir();
	printf("1..%d\n", checks);
	if (failures)
		printf("# %d failures\n", failures);
	return failures ? 1 : 0;
}
