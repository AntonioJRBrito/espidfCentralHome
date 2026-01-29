/**
 * service-worker.js
 * Service Worker para PWA inteligente
 * - Cache de recursos
 * - Fallback offline
 * - Sincronização dinâmica local/web
 */

const CACHE_NAME = 'automacao-v1';
const URLS_TO_CACHE = [
    '/',
    '/index.html',
    '/manifest.json',
    '/css/igra.css',
    '/css/bootstrap.min.css',
    '/js/messages.js',
    '/js/icons.js',
    '/js/health-check.js',
    '/js/pwa-installer.js',
    '/service-worker.js'
];

// Instalação: cacheia recursos
self.addEventListener('install', (event) => {
    console.log('🔧 Service Worker instalando...');
    event.waitUntil(
        caches.open(CACHE_NAME).then((cache) => {
            console.log('📦 Cacheando recursos...');
            return cache.addAll(URLS_TO_CACHE).catch((err) => {
                console.warn('⚠️ Alguns recursos não foram cacheados:', err);
            });
        })
    );
    self.skipWaiting();
});

// Ativação: limpa caches antigos
self.addEventListener('activate', (event) => {
    console.log('✨ Service Worker ativando...');
    event.waitUntil(
        caches.keys().then((cacheNames) => {
            return Promise.all(
                cacheNames.map((cacheName) => {
                    if (cacheName !== CACHE_NAME) {
                        console.log('🗑️ Removendo cache antigo:', cacheName);
                        return caches.delete(cacheName);
                    }
                })
            );
        })
    );
    self.clients.claim();
});

// Fetch: estratégia network-first com fallback para cache
self.addEventListener('fetch', (event) => {
    const { request } = event;
    const url = new URL(request.url);

    // Ignora requisições não-GET
    if (request.method !== 'GET') {
        return;
    }

    // Para a raiz "/" e "index.html": cache-first com fallback network
    if (url.pathname === '/' || url.pathname === '/index.html') {
        event.respondWith(
            caches.match('/index.html').then((cached) => {
                if (cached) {
                    console.log('📦 Servindo index.html do cache');
                    return cached;
                }
                return fetch(request)
                    .then((response) => {
                        if (response.ok) {
                            const cache_clone = response.clone();
                            caches.open(CACHE_NAME).then((cache) => {
                                cache.put('/index.html', cache_clone);
                            });
                            return response;
                        }
                        return response;
                    })
                    .catch(() => {
                        console.warn('❌ Falha ao buscar index.html');
                        return new Response('Offline - index.html não disponível', {
                            status: 503,
                            statusText: 'Service Unavailable'
                        });
                    });
            })
        );
        return;
    }

    // Para requisições de API/dados: network-first
    if (url.pathname.startsWith('/GET/') || url.pathname.startsWith('/api')) {
        event.respondWith(
            fetch(request)
                .then((response) => {
                    if (response.ok) {
                        return response;
                    }
                    return caches.match(request).then((cached) => cached || response);
                })
                .catch(() => {
                    return caches.match(request).then((cached) => {
                        if (cached) {
                            console.log('📦 Usando cache para:', url.pathname);
                            return cached;
                        }
                        return new Response('Offline - recurso não disponível', {
                            status: 503,
                            statusText: 'Service Unavailable'
                        });
                    });
                })
        );
        return;
    }

    // Para recursos estáticos (CSS/JS/IMG): cache-first
    event.respondWith(
        caches.match(request).then((cached) => {
            if (cached) {
                return cached;
            }
            return fetch(request)
                .then((response) => {
                    if (response.ok) {
                        const cache_clone = response.clone();
                        caches.open(CACHE_NAME).then((cache) => {
                            cache.put(request, cache_clone);
                        });
                        return response;
                    }
                    return response;
                })
                .catch(() => {
                    console.warn('❌ Falha ao buscar:', url.pathname);
                    return new Response('Offline - recurso não disponível', {
                        status: 503,
                        statusText: 'Service Unavailable'
                    });
                });
        })
    );
});
