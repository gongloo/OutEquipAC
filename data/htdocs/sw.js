self.addEventListener('install', function(event) {
  self.skipWaiting(); // Force the updated service worker to activate immediately
  event.waitUntil(
    caches.open('sw-cache').then(function(cache) {
      return cache.addAll([
        'thermostat',
        'manifest.webmanifest',
        'apple-touch-icon.png',
        'favicon.ico'
      ]);
    })
  );
});

self.addEventListener('activate', function(event) {
  event.waitUntil(self.clients.claim()); // Force immediate page takeover
});
 
self.addEventListener("fetch", event => {
  const url = event.request.url;
  
  // Completely bypass service worker for SSE stream, control commands, and dynamic APIs
  if (
    event.request.method !== 'GET' ||
    url.includes('/events') ||
    url.includes('/climate/') ||
    url.includes('/number/') ||
    url.includes('/select/') ||
    url.includes('/sensor/') ||
    url.includes('/switch/')
  ) {
    return; // Direct browser network pass-through
  }

  event.respondWith(
    fetch(event.request)
      .then(response => {
        if (response && response.status === 200) {
          const responseClone = response.clone();
          caches.open("sw-cache").then(cache => {
            cache.put(event.request, responseClone);
          });
        }
        return response;
      })
      .catch(() => {
        return caches.match(event.request).then(response => {
          if (response) return response;
          
          // Map offline root fetches to the cached thermostat page
          try {
            const urlObj = new URL(event.request.url);
            if (urlObj.pathname === '/' || urlObj.pathname === '' || urlObj.pathname.endsWith('/')) {
              return caches.match('thermostat').then(fallbackRes => {
                if (fallbackRes) return fallbackRes;
                return caches.match(event.request);
              });
            }
          } catch (e) {
            console.error("Error matching offline root fallback:", e);
          }

          const urlStr = event.request.url;
          const alternativeUrl = urlStr.endsWith('/') ? urlStr.slice(0, -1) : urlStr + '/';
          return caches.match(alternativeUrl);
        });
      })
  );
});
