#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <string.h>

#define MAXEVENTS 64

static int create_and_bind(char *port_ptr)
{
	struct addrinfo hints;
	struct addrinfo *result_ptr, *rp_ptr;
	int s, sfd;

	::memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	s = getaddrinfo(NULL, port_ptr, &hints, &result_ptr);
	if (s != 0)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		return -1;
	}

	for (rp_ptr = result_ptr; rp_ptr != NULL; rp_ptr = rp_ptr->ai_next)
	{
		sfd = socket(rp_ptr->ai_family, rp_ptr->ai_socktype, rp_ptr->ai_protocol);
		if (sfd == -1)
			continue;

		s = bind(sfd, rp_ptr->ai_addr, rp_ptr->ai_addrlen);
		if (s == 0)
			break;

		close(sfd);
	}

	if (rp_ptr == NULL)
	{
		fprintf(stderr, "Could not bind\n");
		return -1;
	}

	freeaddrinfo(result_ptr);
	return sfd;
}

static int make_socket_non_blocking(int sfd)
{
	int flags, s;

	flags = fcntl(sfd, F_GETFL, 0);
	if (flags == -1)
	{
		perror("fcntl");
		return -1;
	}

	flags |= O_NONBLOCK;
	s = fcntl(sfd, F_SETFL, flags);
	if (s == -1)
	{
		perror("fcntl");
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int sfd, s;
	int efd;
	struct epoll_event event;
	struct epoll_event *event_ptr;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s [port]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	sfd = create_and_bind(argv[1]);
	if (sfd == -1)
		abort();

	s = make_socket_non_blocking(sfd);
	if (s == -1)
		abort();

	s = listen(sfd, SOMAXCONN);
	if (s == -1)
	{
		perror("listen");
		abort();
	}

	efd = epoll_create1(0);
	if (efd == -1)
	{
		perror("listen");
		abort();
	}

	event.data.fd = sfd;
	event.events = EPOLLIN | EPOLLET;
	s = epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event);
	if (s == -1)
	{
		perror("epoll_ctl");
		abort();
	}

	event_ptr = (epoll_event *)calloc(MAXEVENTS, sizeof(event));
	while (1)
	{
		int n, i;
		n = epoll_wait(efd, event_ptr, MAXEVENTS, -1);
		for (i = 0; i < n; ++i)
		{
			if ((event_ptr[i].events & EPOLLERR) ||
				(event_ptr[i].events & EPOLLHUP) ||
				(!(event_ptr[i].events & EPOLLIN)))
			{
				fprintf(stderr, "epoll error\n");
				close(event_ptr[i].data.fd);
				continue;
			}
			else if (sfd == event_ptr[i].data.fd)
			{
				while (1)
				{
					struct sockaddr in_addr;
					socklen_t in_len;
					int infd;
					char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

					in_len = sizeof(in_addr);
					infd = accept(sfd, &in_addr, &in_len);
					if (infd == -1)
					{
						if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
							perror("accept");

						break;
					}

					s = getnameinfo(&in_addr, in_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
					if (s == 0)
						printf("accepted connection on descriptor %d (host = %s, port = %s)\n", infd, hbuf, sbuf);

					s = make_socket_non_blocking(infd);
					if (s == -1)
						abort();

					event.data.fd = infd;
					event.events = EPOLLIN | EPOLLET;
					s = epoll_ctl(efd, EPOLL_CTL_ADD, infd, &event);
					if (s == -1)
					{
						perror("epoll_ctl");
						abort();
					}
				}
				continue;
			}
			else
			{
				int done = 0;
				while (1)
				{
					size_t count;
					char buf[512];
					count = read(event_ptr[i].data.fd, buf, sizeof(buf));
					if (count == -1)
					{
						if (errno != EAGAIN)
						{
							perror("read");
							done = 1;
						}

						break;
					}
					else if (count == 0)
					{
						done = 1;
						break;
					}

					s = write(1, buf, count);
					if (s == -1)
					{
						perror("write");
						abort();
					}
				}

				if (done)
				{
					printf("closed connection on descriptor %d\n", event_ptr[i].data.fd);
					close(event_ptr[i].data.fd);
				}
			}
		}
	}

	free(event_ptr);
	close(sfd);
	return EXIT_SUCCESS;
}
