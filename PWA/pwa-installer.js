/**
 * pwa-installer.js
 * Gerencia instalação do PWA no celular
 */

let deferredPrompt = null;

// Captura o evento beforeinstallprompt
window.addEventListener('beforeinstallprompt', (e) => {
    e.preventDefault();
    deferredPrompt = e;
    console.log('✓ PWA pronta para instalar');

    // Mostra botão de instalação
    const installBtn = document.getElementById('install-pwa-btn');
    if (installBtn) {
        installBtn.style.display = 'flex';
    }
});

// Função para instalar
async function installPWA() {
    if (!deferredPrompt) {
        console.warn('PWA não disponível para instalação');
        return;
    }

    deferredPrompt.prompt();
    const { outcome } = await deferredPrompt.userChoice;

    if (outcome === 'accepted') {
        console.log('✓ PWA instalado com sucesso');
    } else {
        console.log('✗ Instalação cancelada pelo usuário');
    }

    deferredPrompt = null;
}

// Detecta se já está instalado
window.addEventListener('appinstalled', () => {
    console.log('✓ PWA já está instalado');
    const installBtn = document.getElementById('install-pwa-btn');
    if (installBtn) {
        installBtn.style.display = 'none';
    }
});
