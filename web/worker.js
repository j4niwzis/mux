/* SPDX-License-Identifier: AGPL-3.0-only */
// Supply the headers a static Pages origin cannot set. Assets are fetched
// normally, so a deployment does not leave an old client in an app cache.
self.addEventListener('install',event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate',event => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch',event => {
  const request = event.request;
  if (new URL(request.url).origin !== self.location.origin) return;
  event.respondWith((async () => {
    const response = await fetch(request);
    if (response.status === 0) return response;
    const headers = new Headers(response.headers);
    headers.set('Cross-Origin-Opener-Policy','same-origin');
    headers.set('Cross-Origin-Embedder-Policy','require-corp');
    headers.set('Cross-Origin-Resource-Policy','same-origin');
    return new Response(response.body,{status:response.status,statusText:response.statusText,headers});
  })());
});
