/* SPDX-License-Identifier: AGPL-3.0-only */
// Supply the headers a static Pages origin cannot set. Assets are fetched
// normally, so a deployment does not leave an old client in an app cache.
self.addEventListener('install',event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate',event => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch',event => {
  const request = event.request;
  if (new URL(request.url).origin !== self.location.origin) return;
  // Asked of the cache alone, not as the page's own (devtools, some of
  // Firefox's): fetching it again throws, and the page got a network error.
  if (request.cache === 'only-if-cached' && request.mode !== 'same-origin') return;
  event.respondWith((async () => {
    let response;
    try {response = await fetch(request);} catch {return Response.error();}
    if (response.status === 0) return response;
    const headers = new Headers(response.headers);
    headers.set('Cross-Origin-Opener-Policy','same-origin');
    headers.set('Cross-Origin-Embedder-Policy','require-corp');
    headers.set('Cross-Origin-Resource-Policy','same-origin');
    return new Response(response.body,{status:response.status,statusText:response.statusText,headers});
  })());
});
