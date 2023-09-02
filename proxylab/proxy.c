#include <stdio.h>

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname,char *path,int port,rio_t *client_rio);
static inline int connect_server(char *hostname, int port, char *http_header)
{
	char portStr[100];
	sprintf(portStr, "%d", port);
	return Open_clientfd(hostname, portStr);
}

void *thread_routine(void *arg)
{
	int connfd = (int)arg;
	Pthread_detach(pthread_self());
	doit(connfd);
	Close(connfd);
	return NULL;
}


int main(int argc, char **argv) 
{
	int                         listenfd;
	int                         connfd;
	char                        hostname[MAXLINE];
	char                        port[MAXLINE];
	socklen_t                   clientlen;
	struct sockaddr_storage     clientaddr;

	/* Check command line args */
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	listenfd = Open_listenfd(argv[1]);
	while (1) {
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		printf("Accepted connection from (%s, %s)\n", hostname, port);

		pthread_t	tid = 0;
		Pthread_create(&tid, NULL, thread_routine, (void *)connfd);
		// doit(connfd);
		// Close(connfd);
	}

	return 0;
}

/* notice it's proxy here */
void doit(int connfd) 
{
	int	port;
	int	serverfd;
	char    buf[MAXLINE];
	char    uri[MAXLINE];
	char    method[MAXLINE];
	char    server_http_header[MAXLINE];
	char    version[MAXLINE];
	char	hostname[MAXLINE];
	char	path[MAXLINE];
	rio_t   rio;
	rio_t   server_rio;

	Rio_readinitb(&rio, connfd);
	Rio_readlineb(&rio, buf, MAXLINE);
	sscanf(buf, "%s %s %s", method, uri, version);

	if (strcasecmp(method, "GET")) {
		printf("Proxy does not implement the method %s", method);
		return;
	}

	// Fixme: get hostname \0 port 80  path /
	parse_uri(uri, hostname, path, &port);

	build_http_header(server_http_header, hostname, path, port, &rio);

	/* Fixme: 
	hostname \0   
	port 80
	server_http_header
	GET / HTTP/1.0\r\nHost: 172.16.25.128:15213\r\nConnection: close\r\nProxy-Connection: close\r\nUser-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n

	*/
	serverfd = connect_server(hostname, port, server_http_header);
	if (serverfd < 0) {
		printf("proxy connect server failed\n");
		return;
	}

	Rio_readinitb(&server_rio, serverfd);
	Rio_writen(serverfd, server_http_header, strlen(server_http_header));

	int	n;
	while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
		printf("proxy received %d bytes, now forward\n", n);
		Rio_writen(connfd, buf, n);
	}

	Close(serverfd);
}



/* 
usual uri
http://www.example.com/index.html

http://www.example.com:80/page.html --> hostname:www.example.com  port:80  path:/pate.html

https://sub.example.com
ftp://ftp.example.com/files/file.txt

our
/xxx.x
/
*/
void parse_uri(char *uri, char *hostname, char *path, int *port)
{
	// default port if not specified
	*port = 80;
	char *pos = strstr(uri, "//");
	pos = (pos == NULL ? uri : pos+2);

	char *pos2 = strstr(pos, ":");
	if (pos2 != NULL) {
		*pos2 = '\0';
		sscanf(pos, "%s", hostname);
		sscanf(pos2+1, "%d%s", port, path);
	} else {
		pos2 = strstr(pos, "/");
		if (pos2 != NULL) {
			*pos2 = '\0';
			sscanf(pos, "%s", hostname);
			*pos2 = '/';
			sscanf(pos, "%s", path);
		} else {
			sscanf(pos, "%s", hostname);
		}
	}
	return;
}

/* http header for example: */
// GET /index.html HTTP/1.1
// Host: www.example.com
// Connection: keep-alive
// Proxy-Connection: keep-alive
// User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.3
// Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8
// Accept-Encoding: gzip, deflate, sdch
// Accept-Language: en-US,en;q=0.8

void build_http_header(char *http_header, char *hostname,char *path,int port,rio_t *client_rio)
{
	char buf[MAXLINE],request_hdr[MAXLINE],other_hdr[MAXLINE],host_hdr[MAXLINE];
	
	sprintf(request_hdr, requestlint_hdr_format, path);

	while(Rio_readlineb(client_rio, buf, MAXLINE)>0) {
		if(strcmp(buf, endof_hdr)==0)
			break;

		if(!strncasecmp(buf, host_key, strlen(host_key))) {	/*Host:*/
			strcpy(host_hdr, buf);
			continue;
		}

		// do nothing
		if(!strncasecmp(buf, connection_key, strlen(connection_key))
			&&!strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key))
			&&!strncasecmp(buf, user_agent_key, strlen(user_agent_key))) {
			
			strcat(other_hdr,buf);
		}
	}

	if(strlen(host_hdr)==0)
		sprintf(host_hdr, host_hdr_format, hostname);

	sprintf(http_header,"%s%s%s%s%s%s%s",
		request_hdr,
		host_hdr,
		conn_hdr,
		prox_hdr,
		user_agent_hdr,
		other_hdr,
		endof_hdr);

	return ;
}






















