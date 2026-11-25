const CACHE_NAME = 'outequipac-v1';
const PRECACHE_URLS = [
  'thermostat',
  'manifest.webmanifest',
  'apple-touch-icon.png',
  'icon-96.png',
  'favicon.ico'
];
const resolvePath = (asset) => new URL(asset, self.location.href).pathname;
const THERMOSTAT_PATH = resolvePath('thermostat');
const THERMOSTAT_HTML_PATH = resolvePath('thermostat.html');
const OWNED_PATHS = new Set([
  ...PRECACHE_URLS.map(resolvePath),
  THERMOSTAT_HTML_PATH
]);

self.addEventListener('install', (event) => {
  self.skipWaiting();
  event.waitUntil(
    (async () => {
      try {
        const cache = await caches.open(CACHE_NAME);
        for (const url of PRECACHE_URLS) {
          try {
            await cache.add(url);
          } catch (err) {
            // Ignore individual asset failures (e.g. local static dev or socket limits)
          }
        }
      } catch (err) {
        // Ignore cache storage errors
      }
    })()
  );
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.filter((k) => k !== CACHE_NAME).map((k) => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (event) => {
  const req = event.request;
  if (req.method !== 'GET') return;

  let pathname = '';
  try {
    const url = new URL(req.url);
    if (url.origin !== self.location.origin) return;
    pathname = url.pathname;
  } catch (err) {
    return;
  }

  // Only intercept assets owned by OutEquipAC (supports reverse-proxy subpaths)
  if (!OWNED_PATHS.has(pathname)) return;

  event.respondWith(
    fetch(req)
      .then((res) => {
        if (res && res.status === 200) {
          const clone = res.clone();
          event.waitUntil(
            caches.open(CACHE_NAME).then((cache) => cache.put(req, clone)).catch(() => {})
          );
        }
        return res;
      })
      .catch(async () => {
        const cached = (await caches.match(req)) || (await caches.match(req, { ignoreSearch: true }));
        if (cached) return cached;
        if (pathname === THERMOSTAT_PATH || pathname === THERMOSTAT_HTML_PATH) {
          return (await caches.match('thermostat')) || (await caches.match('thermostat.html')) || Response.error();
        }
        return Response.error();
      })
  );
});
