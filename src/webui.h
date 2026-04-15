// webui.h
#ifndef WEBUI_H
#define WEBUI_H

// ===== LOGIN PAGE =====
const char HTML_LOGIN[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="uk">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Fazenda - Вхід</title>
    <style>
        :root {
            --primary: #4CAF50;
            --danger: #f44336;
            --dark: #212121;
        }

        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 1rem;
        }

        .card {
            background: white;
            padding: 2rem;
            border-radius: 16px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            width: 100%;
            max-width: 400px;
            animation: slideUp 0.3s ease-out;
        }

        @keyframes slideUp {
            from { opacity: 0; transform: translateY(20px); }
            to { opacity: 1; transform: translateY(0); }
        }

        h1 {
            color: var(--dark);
            margin-bottom: 0.5rem;
            font-size: 1.75rem;
        }

        .subtitle {
            color: #666;
            margin-bottom: 2rem;
            font-size: 0.9rem;
        }

        .form-group {
            margin-bottom: 1.5rem;
        }

        label {
            display: block;
            color: var(--dark);
            margin-bottom: 0.5rem;
            font-weight: 500;
            font-size: 0.9rem;
        }

        input[type="password"] {
            width: 100%;
            padding: 0.75rem 1rem;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 1rem;
            transition: all 0.2s;
        }

        input[type="password"]:focus {
            outline: none;
            border-color: var(--primary);
            box-shadow: 0 0 0 3px rgba(76, 175, 80, 0.1);
        }

        button {
            width: 100%;
            padding: 0.875rem;
            background: var(--primary);
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s;
        }

        button:hover {
            background: #45a049;
            transform: translateY(-1px);
            box-shadow: 0 4px 12px rgba(76, 175, 80, 0.3);
        }

        button:active {
            transform: translateY(0);
        }

        .error {
            background: #ffebee;
            color: var(--danger);
            padding: 0.75rem;
            border-radius: 8px;
            margin-bottom: 1rem;
            display: none;
            font-size: 0.9rem;
        }

        .footer {
            margin-top: 2rem;
            text-align: center;
            color: #999;
            font-size: 0.8rem;
        }
    </style>
</head>
<body>
    <div class="card">
        <h1>🌱 Fazenda</h1>
        <p class="subtitle">Система кліматконтролю</p>

        <div id="error" class="error"></div>

        <form id="loginForm">
            <div class="form-group">
                <label for="password">Пароль для OTA</label>
                <input type="password" id="password" name="password"
                       placeholder="Введіть пароль" required autofocus>
            </div>

            <button type="submit">Увійти</button>
        </form>

        <div class="footer">
            Fazenda Climate Control v2.0
        </div>
    </div>

    <script>
        document.getElementById('loginForm').onsubmit = async (e) => {
            e.preventDefault();
            const password = document.getElementById('password').value;
            const error = document.getElementById('error');

            try {
                const response = await fetch('/login', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: 'password=' + encodeURIComponent(password)
                });

                if (response.ok) {
                    window.location.href = '/update';
                } else {
                    error.textContent = '❌ Невірний пароль';
                    error.style.display = 'block';
                    document.getElementById('password').value = '';
                    document.getElementById('password').focus();
                }
            } catch (err) {
                error.textContent = '❌ Помилка з\'єднання';
                error.style.display = 'block';
            }
        };
    </script>
</body>
</html>
)rawliteral";

// ===== UPDATE PAGE =====
const char HTML_UPDATE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="uk">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Fazenda - OTA Update</title>
    <style>
        :root {
            --primary: #4CAF50;
            --danger: #f44336;
            --dark: #212121;
            --light: #f5f5f5;
        }

        * { margin: 0; padding: 0; box-sizing: border-box; }

        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 2rem 1rem;
        }

        .container {
            max-width: 800px;
            margin: 0 auto;
        }

        .card {
            background: white;
            padding: 2rem;
            border-radius: 16px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            margin-bottom: 1.5rem;
        }

        h1 {
            color: var(--dark);
            margin-bottom: 1.5rem;
        }

        .status-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 1rem;
            margin: 1.5rem 0;
        }

        .status-item {
            background: var(--light);
            padding: 1rem;
            border-radius: 8px;
            text-align: center;
        }

        .status-label {
            color: #666;
            font-size: 0.8rem;
            margin-bottom: 0.5rem;
        }

        .status-value {
            font-size: 1.5rem;
            font-weight: bold;
            color: var(--dark);
        }

        .upload-area {
            border: 2px dashed #ccc;
            border-radius: 12px;
            padding: 2rem;
            text-align: center;
            transition: all 0.3s;
            cursor: pointer;
        }

        .upload-area:hover {
            border-color: var(--primary);
            background: #f0f9f0;
        }

        .upload-area.dragover {
            border-color: var(--primary);
            background: #e8f5e9;
        }

        input[type="file"] {
            display: none;
        }

        .progress-container {
            display: none;
            margin-top: 1.5rem;
        }

        .progress-bar {
            width: 100%;
            height: 30px;
            background: #e0e0e0;
            border-radius: 15px;
            overflow: hidden;
            position: relative;
        }

        .progress-fill {
            height: 100%;
            background: linear-gradient(90deg, var(--primary), #45a049);
            width: 0%;
            transition: width 0.3s;
            display: flex;
            align-items: center;
            justify-content: center;
            color: white;
            font-weight: bold;
        }

        .btn {
            padding: 0.875rem 2rem;
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s;
            display: inline-block;
            text-decoration: none;
        }

        .btn-primary {
            background: var(--primary);
            color: white;
        }

        .btn-primary:hover {
            background: #45a049;
            transform: translateY(-2px);
            box-shadow: 0 4px 12px rgba(76, 175, 80, 0.3);
        }

        .btn-danger {
            background: var(--danger);
            color: white;
        }

        .actions {
            display: flex;
            gap: 1rem;
            margin-top: 1.5rem;
        }

        .alert {
            padding: 1rem;
            border-radius: 8px;
            margin-bottom: 1rem;
            display: none;
        }

        .alert-success {
            background: #e8f5e9;
            color: #2e7d32;
        }

        .alert-error {
            background: #ffebee;
            color: #c62828;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="card">
            <h1>🌱 Fazenda OTA Update</h1>

            <div id="alert" class="alert"></div>

            <div class="status-grid">
                <div class="status-item">
                    <div class="status-label">Температура</div>
                    <div class="status-value" id="temp">--</div>
                </div>
                <div class="status-item">
                    <div class="status-label">Вологість</div>
                    <div class="status-value" id="hum">--</div>
                </div>
                <div class="status-item">
                    <div class="status-label">Вентилятор</div>
                    <div class="status-value" id="fan">--</div>
                </div>
                <div class="status-item">
                    <div class="status-label">Режим</div>
                    <div class="status-value" id="mode">--</div>
                </div>
            </div>

            <div class="upload-area" id="uploadArea">
                <h3>📦 Виберіть файл прошивки</h3>
                <p style="color: #666; margin-top: 0.5rem;">
                    Перетягніть .bin файл або натисніть для вибору
                </p>
                <input type="file" id="fileInput" accept=".bin">
            </div>

            <div class="progress-container" id="progressContainer">
                <div class="progress-bar">
                    <div class="progress-fill" id="progressFill">0%</div>
                </div>
            </div>

            <div class="actions">
                <button class="btn btn-primary" id="uploadBtn" disabled>
                    Завантажити прошивку
                </button>
                <a href="/logs" class="btn btn-secondary" style="background: #2196F3;">📋 Логи</a>
                <a href="/logout" class="btn btn-danger">Вийти</a>
            </div>
        </div>
    </div>

    <script>
        async function updateStatus() {
            try {
                const response = await fetch('/status');
                const data = await response.json();

                document.getElementById('temp').textContent = data.temp.toFixed(1) + '°C';
                document.getElementById('hum').textContent = data.hum.toFixed(0) + '%';
                document.getElementById('fan').textContent = 'CH' + data.fan;
                document.getElementById('mode').textContent = data.mode;
            } catch (e) {
                console.error('Status update failed:', e);
            }
        }

        updateStatus();
        setInterval(updateStatus, 5000);

        const uploadArea = document.getElementById('uploadArea');
        const fileInput = document.getElementById('fileInput');
        const uploadBtn = document.getElementById('uploadBtn');
        let selectedFile = null;

        uploadArea.onclick = () => fileInput.click();

        uploadArea.ondragover = (e) => {
            e.preventDefault();
            uploadArea.classList.add('dragover');
        };

        uploadArea.ondragleave = () => {
            uploadArea.classList.remove('dragover');
        };

        uploadArea.ondrop = (e) => {
            e.preventDefault();
            uploadArea.classList.remove('dragover');
            const files = e.dataTransfer.files;
            if (files.length > 0) {
                handleFile(files[0]);
            }
        };

        fileInput.onchange = (e) => {
            if (e.target.files.length > 0) {
                handleFile(e.target.files[0]);
            }
        };

        function handleFile(file) {
            if (!file.name.endsWith('.bin')) {
                showAlert('Будь ласка, виберіть .bin файл', 'error');
                return;
            }

            selectedFile = file;
            uploadArea.innerHTML = '<h3>✅ ' + file.name + '</h3><p>' +
                                   (file.size / 1024).toFixed(2) + ' KB</p>';
            uploadBtn.disabled = false;
        }

        uploadBtn.onclick = async () => {
            if (!selectedFile) return;

            uploadBtn.disabled = true;
            const progressContainer = document.getElementById('progressContainer');
            const progressFill = document.getElementById('progressFill');
            progressContainer.style.display = 'block';

            const xhr = new XMLHttpRequest();

            xhr.upload.onprogress = (e) => {
                if (e.lengthComputable) {
                    const percent = (e.loaded / e.total * 100).toFixed(0);
                    progressFill.style.width = percent + '%';
                    progressFill.textContent = percent + '%';
                }
            };

            xhr.onload = () => {
                if (xhr.status === 200) {
                    showAlert('✅ Прошивка успішно завантажена! Пристрій перезавантажується...', 'success');
                    setTimeout(() => window.location.href = '/', 10000);
                } else {
                    showAlert('❌ Помилка завантаження: ' + xhr.statusText, 'error');
                    uploadBtn.disabled = false;
                }
            };

            xhr.onerror = () => {
                showAlert('❌ Помилка з\'єднання', 'error');
                uploadBtn.disabled = false;
            };

            xhr.open('POST', '/update');
            xhr.send(selectedFile);
        };

        function showAlert(message, type) {
            const alert = document.getElementById('alert');
            alert.textContent = message;
            alert.className = 'alert alert-' + type;
            alert.style.display = 'block';
        }
    </script>
</body>
</html>
)rawliteral";

#endif // WEBUI_H
