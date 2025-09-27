#pragma once

const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
  <head>
    <meta charset="utf-8">
    <title>ESP32-CAM Веб-сервер</title>
    <style>
      :root { --bg:#222; --fg:#eee; --panel:#2a2a2a; --line:#444; --accent:#09f; --ok:#219653; --err:#d32f2f; }
      html,body { height:100%; }
      body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 0; background: var(--bg); color: var(--fg); }
      h1 { background: #444; margin: 0; padding: 16px; }
      button, input { margin: 6px; padding: 8px 14px; font-size: 14px; }
      #stream { width: 100%; max-width: 480px; margin: 16px auto; display: block; border:1px solid var(--line); }
      .panel { width:100%; max-width: 560px; margin:16px auto; background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:12px; text-align:left; }
      .panel h3 { margin:6px 0 10px; }
      .row { display:flex; flex-wrap:wrap; align-items:center; gap:8px; }
      .muted{opacity:.75;font-size:12px}

      /* toast */
      #toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:rgba(0,0,0,.85);color:#fff;padding:10px 16px;border-radius:6px;opacity:0;pointer-events:none;transition:opacity .2s;max-width:90vw;white-space:pre-line;z-index:10}
      #toast.show{opacity:1} #toast.success{background:rgba(33,150,83,.95)} #toast.error{background:rgba(211,47,47,.95)}

      /* loader */
      #loader{position:fixed;inset:0;background:rgba(0,0,0,.45);display:flex;justify-content:center;align-items:center;opacity:0;pointer-events:none;transition:opacity .2s;z-index:9}
      #loader.show{opacity:1;pointer-events:auto}
      .spinner{width:48px;height:48px;border:6px solid #ccc;border-top-color:var(--accent);border-radius:50%;animation:spin 1s linear infinite}
      @keyframes spin{to{transform:rotate(360deg)}}

      /* progress */
      .progress{display:none;width:100%;height:12px;background:#111;border:1px solid var(--line);border-radius:6px;overflow:hidden;margin-top:8px}
      .progress.show{display:block}
      .progress>div{height:100%;width:0;background:linear-gradient(90deg,var(--accent),#35c1ff);transition:width .1s linear}
      pre#ota-log{background:#111;color:#0f0;text-align:left;white-space:pre-wrap;max-height:220px;overflow:auto;border-radius:6px;padding:10px;display:none}
    </style>
  </head>
  <body>
    <h1>ESP32-CAM</h1>

    <div class="panel">
      <h3>Камера</h3>
      <div class="row">
        <button id="toggle-stream">Запустить поток</button>
        <button id="get-still">Сделать снимок</button>
      </div>

      <h3>Фото → Яндекс.Диск</h3>
      <div class="row">
        <input id="photo-name" type="text" placeholder="имя фото (необязательно)">
        <button id="save-yandex">Сохранить на Яндекс.Диск</button>
      </div>

      <h3>Папки</h3>
      <div class="row">
        <input id="folder-name" type="text" placeholder="имя папки">
        <button id="create-folder">Создать папку</button>
      </div>

      <h3>Список файлов</h3>
      <div class="row">
        <input id="list-folder" type="text" placeholder="имя папки (пусто = YD_BASE_DIR)">
        <button id="list-files">Показать файлы</button>
      </div>
      <div id="list-area" class="muted"></div>
    </div>

    <div class="panel">
      <h3>OTA обновление прошивки</h3>
      <div class="row">
        <input type="file" id="firmware" accept=".bin">
        <button id="upload-fw">Загрузить прошивку</button>
      </div>
      <div id="ota-progress" class="progress"><div id="ota-bar"></div></div>
      <div id="ota-status" class="muted"></div>
      <div class="row">
        <button id="show-ota-log">Показать лог OTA</button>
      </div>
      <pre id="ota-log"></pre>
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
      const listArea = document.getElementById('list-area');

      // OTA elems
      const fwInput = document.getElementById('firmware');
      const fwBtn   = document.getElementById('upload-fw');
      const progBox = document.getElementById('ota-progress');
      const progBar = document.getElementById('ota-bar');
      const progTxt = document.getElementById('ota-status');

      function showToast(msg, type='success') {
        toast.className = '';
        toast.classList.add('show', type);
        toast.textContent = msg;
        setTimeout(() => toast.classList.remove('show'), 2500);
      }
      function showLoader(show=true) {
        loader.classList.toggle('show', !!show);
      }
      function setDisabled(el, dis){ el.disabled = !!dis; el.style.opacity = dis ? .6 : 1; }

      // STREAM
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

      // CAPTURE
      stillBtn.addEventListener('click', () => {
        fetch(`${baseHost}/capture`)
          .then(r => { if (!r.ok) throw new Error('HTTP ' + r.status); return r.blob(); })
          .then(b => { const url = URL.createObjectURL(b); streamEl.src = url; showToast('Снимок получен'); })
          .catch(e => showToast('Ошибка снимка: ' + e.message, 'error'));
      });

      // SAVE to Yandex
      document.getElementById('save-yandex').addEventListener('click', () => {
        const name = document.getElementById('photo-name').value.trim();
        let url = `${baseHost}/save`; if (name) url += `?name=${encodeURIComponent(name)}`;
        showLoader(true);
        fetch(url)
          .then(r => { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
          .then(j => { if (j.ok) showToast(`Сохранено:\n${j.remote||''}\n${j.bytes} байт`, 'success'); else showToast('Сохранение не удалось', 'error'); })
          .catch(err => showToast('Ошибка запроса: ' + err.message, 'error'))
          .finally(() => showLoader(false));
      });

      // MKDIR (POST body plain text в серверной части, если оставили GET — поменяйте тут)
      document.getElementById('create-folder').addEventListener('click', () => {
        const name = document.getElementById('folder-name').value.trim();
        if (!name) { showToast('Введите имя папки', 'error'); return; }
        showLoader(true);
        fetch(`${baseHost}/mkdir`, { method:'POST', body:name })
          .then(r => { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
          .then(j => { if (j.ok) showToast(`Папка готова:\n${j.dir||name}`, 'success'); else showToast('Ошибка создания папки', 'error'); })
          .catch(err => showToast('Ошибка запроса: ' + err.message, 'error'))
          .finally(() => showLoader(false));
      });

      // LIST files
      document.getElementById('list-files').addEventListener('click', () => {
        const dir = document.getElementById('list-folder').value.trim();
        let url = `${baseHost}/ydlist`; if (dir) url += `?dir=${encodeURIComponent(dir)}`;
        showLoader(true);
        fetch(url)
          .then(r => { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
          .then(j => {
            let items = (j && j._embedded && Array.isArray(j._embedded.items)) ? j._embedded.items : [];
            listArea.textContent = items.length ? items.map(it => it.name || '(без имени)').join('\\n') : '(папка пуста/нет доступа)';
            showToast('Готово', 'success');
          })
          .catch(err => showToast('Ошибка запроса: ' + err.message, 'error'))
          .finally(() => showLoader(false));
      });

      // OTA upload (octet-stream, XHR с прогрессом)
      fwBtn.addEventListener('click', () => {
        const file = fwInput.files && fwInput.files[0];
        if (!file) { showToast('Выберите .bin файл!', 'error'); return; }
        setDisabled(fwBtn, true); setDisabled(fwInput, true);
        progBar.style.width = '0%'; progBox.classList.add('show'); progTxt.textContent = 'Подготовка…';

        const xhr = new XMLHttpRequest();
        xhr.open('POST', `${baseHost}/update`, true);
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');

        xhr.upload.onprogress = (e) => {
          if (e.lengthComputable) {
            const pct = Math.min(100, Math.round((e.loaded / e.total) * 100));
            progBar.style.width = pct + '%';
            progTxt.textContent = `Загрузка: ${pct}% (${e.loaded} / ${e.total} байт)`;
          } else {
            progTxt.textContent = 'Загрузка…';
          }
        };

        xhr.onload = () => {
          let ok = (xhr.status >= 200 && xhr.status < 300);
          try { const j = JSON.parse(xhr.responseText || '{}'); ok = ok && j.ok; if (j.bytes) progTxt.textContent = `Загружено ${j.bytes} байт. Перезагрузка…`; } catch(_) {}
          if (ok) { showToast('OTA успешно. Перезагрузка…', 'success'); setTimeout(()=>location.reload(), 6000); }
          else { showToast('Ошибка OTA: ' + (xhr.responseText || xhr.status), 'error'); setDisabled(fwBtn, false); setDisabled(fwInput, false); }
        };

        xhr.onerror = () => { showToast('Сетевая ошибка OTA', 'error'); setDisabled(fwBtn, false); setDisabled(fwInput, false); };
        xhr.send(file);
      });

      // Показать лог OTA
      document.getElementById('show-ota-log').addEventListener('click', () => {
        fetch(`${baseHost}/otalog`)
          .then(r => { if (!r.ok) throw new Error("HTTP " + r.status); return r.json(); })
          .then(j => { const pre = document.getElementById('ota-log'); pre.style.display='block'; pre.textContent = JSON.stringify(j, null, 2); })
          .catch(e => showToast("Ошибка OTA-лога: " + e.message, "error"));
      });
    </script>
  </body>
</html>
)rawliteral";
