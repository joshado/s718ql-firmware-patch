/* tcpsh - minimal TCP bind shell for owner-authorized root access.
 *
 * Context: the Dell S718QL "Network FW" (Actions Micro AM8250) ships busybox
 * v1.15.1 built WITHOUT telnetd, dropbear, inetd, httpd or even nc, so there is
 * no stock applet that can open a shell over the network. This tiny daemon
 * provides one. It is launched from /etc/init.d/rcS (backgrounded, non-fatal)
 * so it is reachable on boot and persists across reboot.
 *
 * Behaviour: listen on 0.0.0.0:<port> (argv[1], default 23); each accepted
 * connection is handed an interactive /bin/sh with stdin/stdout/stderr wired to
 * the socket, running as whatever uid tcpsh runs as (root, from rcS). There is
 * NO authentication by design - this is an owner device the owner controls; the
 * blank root password and standby network behaviour are the access controls.
 * Raw TCP (no telnet IAC negotiation): connect with `nc`/`socat`; a `telnet`
 * client also works (it emits a few harmless negotiation bytes at connect).
 *
 * Built static (o32, mips32r2, little-endian) so it depends only on the stable
 * Linux syscall ABI, not on the device's exact libc. Verified under
 * qemu-mipsel before shipping; see docs/rebuilding.md 2.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int port = (argc > 1) ? atoi(argv[1]) : 23;
	int s, c, one = 1;
	struct sockaddr_in a;

	signal(SIGCHLD, SIG_IGN); /* auto-reap children, no zombies */
	signal(SIGPIPE, SIG_IGN);

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	a.sin_port = htons((unsigned short)port);
	if (bind(s, (struct sockaddr *)&a, sizeof a) < 0)
		return 1;
	if (listen(s, 4) < 0)
		return 1;

	for (;;) {
		c = accept(s, (struct sockaddr *)0, (socklen_t *)0);
		if (c < 0)
			continue;
		if (fork() == 0) {
			close(s);
			dup2(c, 0);
			dup2(c, 1);
			dup2(c, 2);
			if (c > 2)
				close(c);
			setsid();
			execl("/bin/sh", "sh", "-i", (char *)0);
			_exit(127);
		}
		close(c);
	}
	return 0;
}
