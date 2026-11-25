const CACHE_NAME = 'outequipac-v1';
const PRECACHE_URLS = [
  'thermostat',
  'manifest.webmanifest',
  'apple-touch-icon.png',
  'icon-96.png',
  'favicon.ico'
];
const OWNED_PATHS = [...PRECACHE_URLS, 'thermostat.html'];

self.addEventListener('install', (event) => {
  self.skipWaiting();
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => cache.addAll(PRECACHE_URLS)).catch(() => {})
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
  const isOwnedAsset = OWNED_PATHS.some((asset) => pathname.endsWith(`/${asset}`));
  if (!isOwnedAsset) return;

  event.respondWith(
    fetch(req)
      .then((res) => {
        if (res && res.status === 200) {
          const clone = res.clone();
          caches.open(CACHE_NAME).then((cache) => cache.put(req, clone));
        }
        return res;
      })
      .catch(() =>
        caches.match(req).then((cached) => {
          if (cached) return cached;
          if (pathname.endsWith('/thermostat') || pathname.endsWith('/thermostat.html')) {
            return caches.match('thermostat');
          }
          return Response.error();
        })
      )
  );
});
