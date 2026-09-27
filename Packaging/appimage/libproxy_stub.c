/*
 * Stub replacement for libproxy.so.1.
 *
 * Steam Deck's Game Mode launches the app with Steam's runtime in
 * LD_LIBRARY_PATH, which contains an old libcurl.so.4 (pinned_libs_64).
 * The real libproxy pulls in its backend library (libpxbackend-1.0.so),
 * which requires libcurl.so.4 with the CURL_OPENSSL_4 symbol version ->
 * the dynamic loader aborts the process before Qt even starts.
 *
 * DoomRunnerPlus only needs Qt's QNetworkAccessManager and works fine
 * with a direct connection, so we replace libproxy with a stub that
 * always reports "direct://" and has no dependencies beyond libc.
 *
 * Build: gcc -shared -fPIC -o libproxy.so.1 libproxy_stub.c
 */

#include <stdlib.h>
#include <string.h>

typedef struct px_proxy_factory_s pxProxyFactory;

pxProxyFactory *px_proxy_factory_new(void)
{
	return (pxProxyFactory *)1;
}

void px_proxy_factory_free(pxProxyFactory *factory)
{
	(void)factory;
}

char **px_proxy_factory_get_proxies(pxProxyFactory *factory, const char *url)
{
	(void)factory;
	(void)url;
	char **proxies = malloc(2 * sizeof(char *));
	if (proxies == NULL)
		return NULL;
	proxies[0] = strdup("direct://");
	proxies[1] = NULL;
	return proxies;
}

void px_proxy_factory_free_proxies(char **proxies)
{
	if (proxies == NULL)
		return;
	for (char **p = proxies; *p != NULL; ++p)
		free(*p);
	free(proxies);
}

pxProxyFactory *px_proxy_factory_copy(pxProxyFactory *factory)
{
	return factory;
}
