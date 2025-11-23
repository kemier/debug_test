/*  TLS client that connects twice: first handshake creates a session,
    second handshake re-uses it (session reuse).                     */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>

#define HOST "session-reuse-server"
#define PORT 4433

SSL_CTX *create_ctx(void)
{
    const SSL_METHOD *meth = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(meth);
    if (!ctx) { ERR_print_errors_fp(stderr); exit(1); }
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT);
    return ctx;
}

void do_handshake(SSL_CTX *ctx, SSL_SESSION **sess)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct hostent *h = gethostbyname(HOST);
    struct sockaddr_in addr = { .sin_family = AF_INET,
                                .sin_port   = htons(PORT),
                                .sin_addr   = { *(in_addr_t *)h->h_addr } };
    connect(fd, (struct sockaddr *)&addr, sizeof(addr));

    SSL *ssl = SSL_new(ctx);
    if (*sess) SSL_set_session(ssl, *sess);   /*  TRY RE-USE  */
    SSL_set_fd(ssl, fd);

    if (SSL_connect(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
    } else {
        unsigned int len;
        const unsigned char *id = SSL_SESSION_get_id(SSL_get_session(ssl), &len);
        printf("CLIENT: session-id ");
        for (unsigned int i = 0; i < len; ++i) printf("%02X", id[i]);
        printf("  reused=%s\n", SSL_session_reused(ssl) ? "YES" : "NO");
        if (!*sess) *sess = SSL_get1_session(ssl);   /*  save for next time  */
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
        close(fd);
}

int main(void)
{
    SSL_library_init();
    SSL_load_error_strings();

    SSL_CTX *ctx = create_ctx();
    SSL_SESSION *sess = NULL;

    printf("=== FIRST HANDSHAKE ===\n");
    do_handshake(ctx, &sess);

    sleep(1);

    printf("=== SECOND HANDSHAKE (should reuse) ===\n");
    do_handshake(ctx, &sess);

    SSL_SESSION_free(sess);
    SSL_CTX_free(ctx);
    return 0;
}