#pragma once

const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
  <head>
    <meta charset="utf-8">
    <title>ESP32-CAM Веб-сервер</title>
    <style>
      body { font-family: Arial; text-align: center; margin: 0; padding: 0; background: #222; color: #eee; }
      h1 { background: #444; margin: 0; padding: 16px; }
      #controls { margin: 20px; }
      button, input { margin: 5px; padding: 8px 16px; font-size: 14px; }
      #stream { width: 100%; max-width: 480px; margin: 10px auto; display: block; }

      /* Toast */
      #toast {
        position: fixed; left: 50%; bottom: 24px; transform: translateX(-50%);
        background: rgba(0,0,0,0.85); color: #fff; padding: 10px 16px; border-radius: 6px;
        opacity: 0; pointer-events: none; transition: opacity .2s ease;
        max-width: 90vw; text-align: left; font-size: 14px; white-space: pre-line;
      }
      #toast.show { opacity: 1; }
      #toast.success { background: rgba(33, 150, 83, 0.95); }
      #toast.error   { background: rgba(211, 47, 47, 0.95); }

      /* Loader */
      #loader {
        position: fixed; top: 0; left: 0; right: 0; bottom: 0;
        background: rgba(0,0,0,0.6);
        display: flex; justify-content: center; align-items: center;
        opacity: 0; pointer-events: none; transition: opacity .2s ease;
      }
      #loader.show { opacity: 1; pointer-events: auto; }
      .spinner {
        width: 48px; height: 48px; border: 6px solid #ccc; border-top-color: #09f;
        border-radius: 50%; animation: spin 1s linear infinite;
      }
      @keyframes spin { 100% { transform: rotate(360deg); } }
    </style>
  </head>
  <body>
    <h1>ESP32-CAM</h1>
    <div id="controls">
      <button id="toggle-stream">Запустить поток</button>
      <button id="get-still">Сделать снимок</button>
      <br>
      <input id="photo-name" type="text" placeholder="имя фото (необязательно)">
      <button id="save-yandex">Сохранить на Яндекс.Диск</button>
      <br><br>
      <input id="folder-name" type="text" placeholder="имя папки">
      <button id="create-folder">Создать папку</button>
    </div>
    <img id="stream" src="">
    <div id="toast"></div>
    <div id="loader"><div class="spinner"></div></div>
    <script>
      const baseHost = window.location.origin;
      const streamUrl = baseHost + ':81/stream';
      const streamEl = document.getElementById('stream');
      const toggleBtn = document.getElementById('toggle-stream');
      const stillBtn = document.getElementById('get-still');
      const toast = document.getElementById('toast');
      const loader = document.getElementById('loader');

      function showToast(msg, type='success') {
        toast.className = '';
        toast.classList.add('show');
        toast.classList.add(type);
        toast.textContent = msg;
        setTimeout(() => toast.classList.remove('show'), 2500);
      }
      function showLoader(show=true) {
        if (show) loader.classList.add('show');
        else loader.classList.remove('show');
      }

      toggleBtn.addEventListener('click', () => {
        if (streamEl.src === streamUrl) {
          streamEl.src = '';
          toggleBtn.innerText = 'Запустить поток';
          showToast('Поток остановлен');
        } else {
          streamEl.src = streamUrl;
          toggleBtn.innerText = 'Остановить поток';
          showToast('Поток запущен');
        }
      });

      stillBtn.addEventListener('click', () => {
        fetch(`${baseHost}/capture`)
          .then(r => { if (!r.ok) throw new Error('HTTP ' + r.status); return r.blob(); })
          .then(b => { const url = URL.createObjectURL(b); streamEl.src = url; showToast('Снимок получен'); })
          .catch(e => showToast('Ошибка снимка: ' + e.message, 'error'));
      });

      // сохранить в Яндекс.Диск
      document.getElementById('save-yandex').addEventListener('click', () => {
        const name = document.getElementById('photo-name').value.trim();
        let url = `${baseHost}/save`; if (name) url += `?name=${encodeURIComponent(name)}`;
        showLoader(true);
        fetch(url)
          .then(r => { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
          .then(j => { if (j.ok) showToast(`Сохранено на Диск:\n${j.remote}\n${j.bytes} байт`, 'success');
                       else showToast('Сохранение не удалось', 'error'); })
          .catch(err => showToast('Ошибка запроса: ' + err.message, 'error'))
          .finally(() => showLoader(false));
      });

      // создать папку
      document.getElementById('create-folder').addEventListener('click', () => {
        const name = document.getElementById('folder-name').value.trim();
        if (!name) { showToast('Введите имя папки', 'error'); return; }
        showLoader(true);
        fetch(`${baseHost}/mkdir?name=${encodeURIComponent(name)}`)
          .then(r => { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
          .then(j => { if (j.ok) showToast(`Папка готова:\n${j.dir}`, 'success');
                       else showToast('Ошибка создания папки', 'error'); })
          .catch(err => showToast('Ошибка запроса: ' + err.message, 'error'))
          .finally(() => showLoader(false));
      });
    </script>
  </body>
</html>
)rawliteral";
