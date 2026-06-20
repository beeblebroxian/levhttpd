/*
 * mini_httpd standalone derivato da micro_httpd
 * uso:
 *
 *   httpserver PORT DIRECTORY
 *
 * esempio:
 *
 *   httpserver 18192 /sdcard/Movies
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>


#define SERVER_NAME "android_httpserver"
#define PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"


static void send_error(
    int fd,
    int status,
    const char *title,
    const char *text
);

static void send_headers(
    int fd,
    int status,
    const char *title,
    const char *mime_type,
    off_t length,
    time_t mod
);

static char *get_mime_type(char *name);


/*
 * invia un file sul socket
 */
static void send_file(
    int fd,
    const char *root,
    char *name
)
{
    char path[4096];
    struct stat sb;
    int file;
    char buf[8192];
    ssize_t n;


    snprintf(
        path,
        sizeof(path),
        "%s/%s",
        root,
        name
    );


    if (stat(path, &sb) < 0)
    {
        send_error(
            fd,
            404,
            "Not Found",
            "File not found"
        );
        return;
    }


    if (!S_ISREG(sb.st_mode))
    {
        send_error(
            fd,
            403,
            "Forbidden",
            "Not a file"
        );
        return;
    }


    file = open(path, O_RDONLY);

    if (file < 0)
    {
        send_error(
            fd,
            403,
            "Forbidden",
            "Cannot open file"
        );
        return;
    }


    send_headers(
        fd,
        200,
        "OK",
        get_mime_type(path),
        sb.st_size,
        sb.st_mtime
    );


    while ((n = read(file, buf, sizeof(buf))) > 0)
    {
        ssize_t off = 0;

        while (off < n)
        {
            ssize_t w = write(
                fd,
                buf + off,
                n - off
            );

            if (w <= 0)
                break;

            off += w;
        }
    }


    close(file);
}



/*
 * gestisce una richiesta HTTP
 */
static void handle_client(
    int fd,
    const char *root
)
{
    char request[8192];
    char method[32];
    char path[4096];


    int n = read(
        fd,
        request,
        sizeof(request)-1
    );


    if (n <= 0)
        return;


    request[n] = 0;


    if (sscanf(
            request,
            "%31s %4095s",
            method,
            path
        ) != 2)
    {
        send_error(
            fd,
            400,
            "Bad Request",
            "Invalid request"
        );
        return;
    }


    if (strcmp(method, "GET") != 0)
    {
        send_error(
            fd,
            501,
            "Not Implemented",
            "Only GET supported"
        );
        return;
    }


    if (path[0] == '/')
        memmove(
            path,
            path + 1,
            strlen(path)
        );


    /*
     * evita ../
     */
    if (strstr(path, ".."))
    {
        send_error(
            fd,
            400,
            "Bad Request",
            "Invalid path"
        );
        return;
    }


    send_file(
        fd,
        root,
        path
    );

static void send_error(
    int fd,
    int status,
    const char *title,
    const char *text
)
{
    dprintf(
        fd,
        "%s %d %s\r\n"
        "Server: %s\r\n"
        "Connection: close\r\n"
        "Content-Type: text/html\r\n"
        "\r\n"
        "<html><body>"
        "<h1>%d %s</h1>"
        "<p>%s</p>"
        "</body></html>\r\n",
        PROTOCOL,
        status,
        title,
        SERVER_NAME,
        status,
        title,
        text
    );
}



static void send_headers(
    int fd,
    int status,
    const char *title,
    const char *mime_type,
    off_t length,
    time_t mod
)
{
    char timebuf[128];
    time_t now;


    dprintf(
        fd,
        "%s %d %s\r\n",
        PROTOCOL,
        status,
        title
    );


    dprintf(
        fd,
        "Server: %s\r\n",
        SERVER_NAME
    );


    now = time(NULL);

    strftime(
        timebuf,
        sizeof(timebuf),
        RFC1123FMT,
        gmtime(&now)
    );

    dprintf(
        fd,
        "Date: %s\r\n",
        timebuf
    );


    if (mime_type)
        dprintf(
            fd,
            "Content-Type: %s\r\n",
            mime_type
        );


    if (length >= 0)
        dprintf(
            fd,
            "Content-Length: %lld\r\n",
            (long long)length
        );


    if (mod != (time_t)-1)
    {
        strftime(
            timebuf,
            sizeof(timebuf),
            RFC1123FMT,
            gmtime(&mod)
        );

        dprintf(
            fd,
            "Last-Modified: %s\r\n",
            timebuf
        );
    }


    dprintf(
        fd,
        "Connection: close\r\n"
        "\r\n"
    );
}



static char *get_mime_type(char *name)
{
    char *dot = strrchr(name, '.');


    if (!dot)
        return "application/octet-stream";


    if (!strcasecmp(dot, ".mp4"))
        return "video/mp4";


    if (!strcasecmp(dot, ".mkv"))
        return "video/x-matroska";


    if (!strcasecmp(dot, ".avi"))
        return "video/x-msvideo";


    if (!strcasecmp(dot, ".jpg") ||
        !strcasecmp(dot, ".jpeg"))
        return "image/jpeg";


    if (!strcasecmp(dot, ".png"))
        return "image/png";


    if (!strcasecmp(dot, ".html") ||
        !strcasecmp(dot, ".htm"))
        return "text/html";


    return "application/octet-stream";
}




int main(
    int argc,
    char **argv
)
{
    int port;
    int server;
    int yes = 1;

    struct sockaddr_in addr;


    if (argc != 3)
    {
        fprintf(
            stderr,
            "usage: %s PORT DIRECTORY\n",
            argv[0]
        );

        return 1;
    }


    port = atoi(argv[1]);


    signal(
        SIGCHLD,
        SIG_IGN
    );


    server = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );


    if (server < 0)
    {
        perror("socket");
        return 1;
    }


    setsockopt(
        server,
        SOL_SOCKET,
        SO_REUSEADDR,
        &yes,
        sizeof(yes)
    );


    memset(
        &addr,
        0,
        sizeof(addr)
    );


    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);


    if (bind(
            server,
            (struct sockaddr *)&addr,
            sizeof(addr)
        ) < 0)
    {
        perror("bind");
        return 1;
    }


    if (listen(server, 8) < 0)
    {
        perror("listen");
        return 1;
    }


    printf(
        "HTTP server listening on port %d\n",
        port
    );


    while (1)
    {
        int client;


        client = accept(
            server,
            NULL,
            NULL
        );


        if (client < 0)
            continue;



        if (fork() == 0)
        {
            close(server);

            handle_client(
                client,
                argv[2]
            );

            close(client);

            exit(0);
        }


        close(client);
    }


    return 0;
}
