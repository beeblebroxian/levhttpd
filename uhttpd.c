#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <netinet/in.h>


#define BUF 8192


static void send_error(int c, int code, const char *msg)
{
    dprintf(c,
        "HTTP/1.1 %d %s\r\n"
        "Connection: close\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        code, msg);
}


static void serve_file(
    int c,
    const char *root,
    char *name,
    char *request
)
{
    char path[2048];
    char buf[BUF];

    struct stat st;

    long long start = 0;
    long long end;
    long long total;

    int f;
    ssize_t n;


    if (strstr(name, ".."))
    {
        send_error(c, 400, "Bad Request");
        return;
    }


    snprintf(
        path,
        sizeof(path),
        "%s/%s",
        root,
        name
    );

    printf("OPEN: %s\n", path);

    f = open(path, O_RDONLY);

    if (f < 0)
    {
        send_error(c, 404, "Not Found");
        return;
    }


    if (fstat(f, &st) < 0)
    {
        close(f);
        send_error(c, 500, "Internal Server Error");
        return;
    }


    total = (long long)st.st_size;
    end = total - 1;


    /*
       Range: bytes=start-
    */
    char *r = strstr(request, "Range:");

    if (r)
    {
        if (sscanf(
                r,
                "Range: bytes=%lld-",
                &start
            ) == 1)
        {
            if (start >= total)
                start = 0;


            lseek(
                f,
                start,
                SEEK_SET
            );


            dprintf(c,
                "HTTP/1.1 206 Partial Content\r\n"
                "Content-Length: %lld\r\n"
                "Content-Range: bytes %lld-%lld/%lld\r\n"
                "Accept-Ranges: bytes\r\n"
                "Connection: close\r\n"
                "\r\n",
                end - start + 1,
                start,
                end,
                total
            );
        }
    }
    else
    {
        dprintf(c,
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: %lld\r\n"
            "Accept-Ranges: bytes\r\n"
            "Connection: close\r\n"
            "\r\n",
            total
        );
    }


    while ((n = read(f, buf, sizeof(buf))) > 0)
    {
        if (start + n > end + 1)
            n = end + 1 - start;


        if (n <= 0)
            break;


        if (write(c, buf, n) <= 0)
            break;


        start += n;


        if (start > end)
            break;
    }


    close(f);
}



int main(int argc, char **argv)
{
    int port;
    int s;
    int c;

    struct sockaddr_in addr;

    char request[BUF];
    char path[1024];


    if (argc != 3)
    {
        printf(
            "usage: %s port directory\n",
            argv[0]
        );

        return 1;
    }


    port = atoi(argv[1]);


    signal(SIGPIPE, SIG_IGN);


    s = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );


    if (s < 0)
        return 1;


    int yes = 1;

    setsockopt(
        s,
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
            s,
            (struct sockaddr *)&addr,
            sizeof(addr)
        ) < 0)
        return 1;


    if (listen(s, 8) < 0)
        return 1;


    printf(
        "uhttpd listening on %d\n",
        port
    );


    while (1)
    {
        c = accept(
            s,
            NULL,
            NULL
        );


        if (c < 0)
            continue;


        int n = read(
            c,
            request,
            sizeof(request)-1
        );


        if (n > 0)
        {
            request[n] = 0;


            if (sscanf(
                    request,
                    "GET /%1023s",
                    path
                ) == 1)
            {
                serve_file(
                    c,
                    argv[2],
                    path,
                    request
                );
            }
            else
            {
                send_error(
                    c,
                    400,
                    "Bad Request"
                );
            }
        }


        close(c);
    }


    return 0;
}
