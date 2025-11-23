/*  Simple TLS 1.3 server that prints session-id once and keeps
    the socket open so you can re-connect many times.            */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>

#define PORT 4433
#define CERT "server.crt"
#define KEY  "server.key"

SSL_CTX *create_ctx(void)
{
    const SSL_METHOD *meth = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(meth);
    if (!ctx) { ERR_print_errors_fp(stderr); exit(1); }
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER);
    SSL_CTX_set_session_id_context(ctx, (unsigned char *)"SRV", 3);
    return ctx;
}

int main(void)
{
    SSL_library_init();
    SSL_load_error_strings();

    SSL_CTX *ctx = create_ctx();
    if (SSL_CTX_use_certificate_file(ctx, CERT, SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx, KEY, SSL_FILETYPE_PEM)  <= 0 ||
        !SSL_CTX_check_private_key(ctx)) {
        ERR_print_errors_fp(stderr); exit(1);
    }

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = { .sin_family = AF_INET,
                                .sin_port   = htons(PORT),
                                .sin_addr   = { INADDR_ANY } };
    bind(listenfd, (struct sockaddr *)&addr, sizeof(addr));
    listen(listenfd, 10);
    printf("Server listening on :%d\n", PORT);

    while (1) {
        int conn = accept(listenfd, NULL, NULL);
        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, conn);

        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
        } else {
            const SSL_SESSION *sess = SSL_get_session(ssl);
            unsigned int len;
            const unsigned char *id = SSL_SESSION_get_id(sess, &len);
            printf("NEW SESSION: ");
            for (unsigned int i = 0; i < len; ++i) printf("%02X", id[i]);
            printf("\n");
        }

        /*  keep socket open so client can re-connect  */
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(conn);
    }
    SSL_CTX_free(ctx);
    return 0;
}