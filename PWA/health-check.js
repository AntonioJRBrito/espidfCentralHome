/**
 * health-check.js
 * Testa conexão com central local (automacao.local via mDNS)
 * Se falhar, redireciona para app.html no servidor web externo
 */

const HEALTH_CHECK_TIMEOUT = 2000; // 2 segundos
const LOCAL_ENDPOINT = '/health';
const WEB_ENDPOINT = 'https://automacao-ia.app/index.html';
const LOCAL_APP = '/app.html';

async function checkLocalHealth() {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), HEALTH_CHECK_TIMEOUT);

    try {
        const response = await fetch(LOCAL_ENDPOINT, {
            method: 'GET',
            signal: controller.signal,
            cache: 'no-store'
        });
        clearTimeout(timeoutId);
        return response.ok;
    } catch (error) {
        clearTimeout(timeoutId);
        console.warn('Health check local falhou:', error.message);
        return false;
    }
}

async function init() {
    const statusEl = document.getElementById('status');
    const errorEl = document.getElementById('error');

    try {
        statusEl.textContent = 'Testando central local...';

        const isLocalOk = await checkLocalHealth();

        if (isLocalOk) {
            statusEl.textContent = 'Central encontrada! Carregando...';
            console.log('✓ Central local respondendo. Redirecionando para app.html local.');
            window.location.href = LOCAL_APP;
        } else {
            statusEl.textContent = 'Central não encontrada. Tentando web...';
            console.log('✗ Central local não respondeu. Redirecionando para web.');
            window.location.href = WEB_ENDPOINT;
        }
    } catch (error) {
        console.error('Erro durante health check:', error);
        errorEl.textContent = 'Erro ao conectar. Redirecionando...';
        errorEl.style.display = 'block';
        setTimeout(() => {
            window.location.href = WEB_ENDPOINT;
        }, 1000);
    }
}

// Inicia ao carregar
window.addEventListener('load', init);
