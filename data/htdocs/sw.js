self.addEventListener("install", function (event) {
  event.waitUntil(
    caches.open("sw-cache").then(function (cache) {
      return cache.addAll(["index.html"]);
    })
  );
});

self.addEventListener("fetch", (event) => {
  event.respondWith(
    caches.match(event.request).then((cachedResponse) => {
      const networkFetch = fetch(event.request).then((response) => {
        // update the cache with a clone of the network response
        caches.open("sw-cache").then((cache) => {
          cache.put(event.request, response.clone());
        });
      });
      // prioritize cached response over network
      return cachedResponse || networkFetch;
    })
  );
});
