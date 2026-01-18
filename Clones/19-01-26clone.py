from flask import Flask, request, render_template_string, redirect, jsonify, send_from_directory
import sqlite3
import os
from datetime import datetime
from werkzeug.utils import secure_filename



app = Flask(__name__)

# Database configuration
DB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "database")
DB_PATH = os.path.join(DB_DIR, "laundry.db")

# OTA Upload configuration
UPLOAD_FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "firmware")
ALLOWED_EXTENSIONS = {'bin'}
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

# Server-side cache for last scanned RFID data
LAST_RFID = None

# HTML template with OTA section
template = """
<!DOCTYPE html>
<html>
<head>
    <title>Laundry Admin Panel</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: #f3f4f6;
            display: flex;
        }
        .sidebar {
            width: 250px;
            background: #2c3e50;
            color: #ecf0f1;
            height: 100vh;
            padding: 20px 0;
            position: fixed;
            left: 0;
            top: 0;
            overflow-y: auto;
        }
        .sidebar h2 {
            text-align: center;
            margin-bottom: 30px;
            color: white;
            font-size: 24px;
            padding: 0 10px;
        }
        .sidebar a {
            display: block;
            color: #ecf0f1;
            text-decoration: none;
            margin: 5px 10px;
            padding: 12px 15px;
            border-radius: 5px;
            transition: background 0.3s;
        }
        .sidebar a:hover, .sidebar a.active {
            background: #34495e;
            cursor: pointer;
        }
        .content {
            margin-left: 250px;
            padding: 30px;
            width: calc(100% - 250px);
            min-height: 100vh;
        }
        h2 {
            color: #2c3e50;
            border-bottom: 3px solid #3498db;
            padding-bottom: 10px;
            margin-bottom: 25px;
        }
        .card {
            background: white;
            padding: 25px;
            margin: 20px 0;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        form {
            background: white;
        }
        .form-group {
            margin-bottom: 15px;
        }
        label {
            display: block;
            margin-bottom: 5px;
            color: #555;
            font-weight: 600;
        }
        input[type="text"], input[type="number"], input[type="file"], select {
            width: 100%;
            padding: 10px;
            border: 2px solid #ddd;
            border-radius: 6px;
            font-size: 14px;
            transition: border-color 0.3s;
        }
        input[type="text"]:focus, input[type="number"]:focus, select:focus {
            outline: none;
            border-color: #3498db;
        }
        .btn {
            background: #3498db;
            color: white;
            border: none;
            padding: 10px 20px;
            border-radius: 6px;
            cursor: pointer;
            font-size: 14px;
            transition: background 0.3s;
            margin-top: 10px;
        }
        .btn:hover {
            background: #2980b9;
        }
        .btn-success {
            background: #27ae60;
        }
        .btn-success:hover {
            background: #229954;
        }
        .btn-danger {
            background: #e74c3c;
        }
        .btn-danger:hover {
            background: #c0392b;
        }
        .btn-small {
            padding: 6px 12px;
            font-size: 13px;
        }
        .btn-scan {
            background: #e74c3c;
            margin-left: 10px;
        }
        .btn-scan:hover {
            background: #c0392b;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            background: white;
            border-radius: 10px;
            overflow: hidden;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        th, td {
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #ecf0f1;
        }
        th {
            background: #3498db;
            color: white;
            font-weight: 600;
            text-transform: uppercase;
            font-size: 13px;
        }
        tr:hover {
            background: #f8f9fa;
        }
        tr:last-child td {
            border-bottom: none;
        }
        section {
            display: none;
        }
        section.active {
            display: block;
        }
        .inline-form {
            display: flex;
            gap: 8px;
            align-items: center;
        }
        .inline-form input {
            width: 80px;
        }
        .stats {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        .stat-card {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 20px;
            border-radius: 10px;
            box-shadow: 0 4px 15px rgba(0,0,0,0.2);
        }
        .stat-card h3 {
            font-size: 14px;
            margin-bottom: 10px;
            opacity: 0.9;
        }
        .stat-card .number {
            font-size: 32px;
            font-weight: bold;
        }
        .alert {
            padding: 12px 20px;
            border-radius: 6px;
            margin-bottom: 20px;
        }
        .alert-success {
            background: #d4edda;
            color: #155724;
            border: 1px solid #c3e6cb;
        }
        .alert-danger {
            background: #f8d7da;
            color: #721c24;
            border: 1px solid #f5c6cb;
        }
        .alert-info {
            background: #d1ecf1;
            color: #0c5460;
            border: 1px solid #bee5eb;
        }
        .firmware-list {
            list-style: none;
            padding: 0;
        }
        .firmware-list li {
            padding: 10px;
            margin: 5px 0;
            background: #f8f9fa;
            border-radius: 5px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .progress {
            width: 100%;
            height: 30px;
            background: #f0f0f0;
            border-radius: 5px;
            overflow: hidden;
            margin-top: 10px;
            display: none;
        }
        .progress-bar {
            height: 100%;
            background: #3498db;
            width: 0%;
            transition: width 0.3s;
            display: flex;
            align-items: center;
            justify-content: center;
            color: white;
            font-weight: bold;
        }
        @media (max-width: 768px) {
            .sidebar {
                width: 100%;
                height: auto;
                position: relative;
            }
            .content {
                margin-left: 0;
                width: 100%;
            }
        }
    </style>
</head>
<body>
    <div class="sidebar">
        <h2>🧺 Laundry Admin</h2>
        <a onclick="showSection('dashboard')" id="link-dashboard">Dashboard</a>
        <a onclick="showSection('add-users')" id="link-add-users">Add User</a>
        <a onclick="showSection('show-users')" id="link-show-users">Manage Users</a>
        <a onclick="showSection('show-logs')" id="link-show-logs">Transaction Logs</a>
        <a onclick="showSection('spending')" id="link-spending">Make Transaction</a>
        <a onclick="showSection('ota-update')" id="link-ota-update">🔧 OTA Update</a>
    </div>

    <div class="content">
        <!-- Dashboard Section -->
        <section id="dashboard" class="active">
            <h2>Dashboard</h2>
            <div class="stats">
                <div class="stat-card">
                    <h3>Total Users</h3>
                    <div class="number">{{ users|length }}</div>
                </div>
                <div class="stat-card" style="background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%);">
                    <h3>Total Balance</h3>
                    <div class="number">{{ total_balance }}</div>
                </div>
                <div class="stat-card" style="background: linear-gradient(135deg, #4facfe 0%, #00f2fe 100%);">
                    <h3>Recent Transactions</h3>
                    <div class="number">{{ logs|length }}</div>
                </div>
            </div>
           
            <div class="card">
                <h3 style="margin-bottom: 15px;">Recent Activity</h3>
                <table>
                    <tr>
                        <th>User</th>
                        <th>Action</th>
                        <th>Balance</th>
                        <th>Time</th>
                    </tr>
                    {% for log in logs[:5] %}
                    <tr>
                        <td>{{ log[2] }}</td>
                        <td>{{ log[3] }}</td>
                        <td>{{ log[4] }}</td>
                        <td>{{ log[5] }}</td>
                    </tr>
                    {% endfor %}
                </table>
            </div>
        </section>

        <!-- Add Users Section -->
        <section id="add-users">
            <h2>Add New User</h2>
            <div class="card">
                <form method="post" action="/add_user">
                    <div class="form-group">
                        <label>Card ID:</label>
                        <div style="display: flex; gap: 10px;">
                            <input type="text" name="card_id" id="card_id" required placeholder="Scan card or enter manually" style="flex: 1;">
                            <button type="button" class="btn btn-scan" onclick="getCard()">📡 Scan Card</button>
                        </div>
                    </div>
                   
                    <div class="form-group">
                        <label>Username:</label>
                        <input type="text" name="username" required placeholder="Enter username">
                    </div>
                   
                    <div class="form-group">
                        <label>Initial Balance:</label>
                        <input type="number" name="balance" value="0" min="0" required>
                    </div>
                   
                    <button type="submit" class="btn btn-success">✓ Add User</button>
                </form>
            </div>
        </section>

        <!-- Show Users Section -->
        <section id="show-users">
            <h2>Manage Users</h2>
            <div class="card">
                <table>
                    <tr>
                        <th>ID</th>
                        <th>Username</th>
                        <th>Card ID</th>
                        <th>Balance</th>
                        <th>Add Balance</th>
                    </tr>
                    {% for user in users %}
                    <tr>
                        <td>{{ user[0] }}</td>
                        <td>{{ user[1] }}</td>
                        <td>{{ user[2] }}</td>
                        <td><strong>{{ user[3] }}</strong></td>
                        <td>
                            <form action="/update_balance" method="POST" class="inline-form">
                                <input type="hidden" name="card_id" value="{{ user[2] }}">
                                <input type="number" name="balance" placeholder="Amount" min="1" required>
                                <button type="submit" class="btn btn-small btn-success">+ Add</button>
                            </form>
                        </td>
                    </tr>
                    {% endfor %}
                </table>
            </div>
        </section>

        <!-- Show Logs Section -->
        <section id="show-logs">
            <h2>Transaction Logs</h2>
            <div class="card">
                <table>
                    <tr>
                        <th>ID</th>
                        <th>Card ID</th>
                        <th>Username</th>
                        <th>Action</th>
                        <th>New Balance</th>
                        <th>Timestamp</th>
                    </tr>
                    {% for row in logs %}
                    <tr>
                        <td>{{ row[0] }}</td>
                        <td>{{ row[1] }}</td>
                        <td>{{ row[2] }}</td>
                        <td>{{ row[3] }}</td>
                        <td>{{ row[4] }}</td>
                        <td>{{ row[5] }}</td>
                    </tr>
                    {% endfor %}
                </table>
            </div>
        </section>

        <!-- Spending Section -->
        <section id="spending">
            <h2>Make Transaction</h2>
            <div class="card">
                <form action="/spend" method="POST">
                    <div class="form-group">
                        <label>Card ID:</label>
                        <div style="display: flex; gap: 10px;">
                            <input type="text" name="card_id" id="spend_card_id" required placeholder="Enter card ID" style="flex: 1;">
                            <button type="button" class="btn btn-scan" onclick="getCardForSpend()">📡 Scan Card</button>
                        </div>
                    </div>

                    <div class="form-group">
                        <label>Select Hours:</label>
                        <select name="hours" required>
                            <option value="1">1 hour (1 coin)</option>
                            <option value="2">2 hours (2 coins)</option>
                            <option value="3">3 hours (3 coins)</option>
                            <option value="4">4 hours (4 coins)</option>
                            <option value="5">5 hours (5 coins)</option>
                            <option value="6">6 hours (6 coins)</option>
                            <option value="7">7 hours (7 coins)</option>
                            <option value="8">8 hours (8 coins)</option>
                            <option value="9">9 hours (9 coins)</option>
                            <option value="10">10 hours (10 coins)</option>
                        </select>
                    </div>

                    <button type="submit" class="btn">Process Transaction</button>
                </form>
            </div>
        </section>

        <!-- OTA Update Section -->
        <section id="ota-update">
            <h2>🔧 ESP32 Firmware Update (OTA)</h2>
            
            <div class="card">
                <div class="alert alert-info">
                    <strong>ℹ️ Instructions:</strong><br>
                    1. Compile your Arduino sketch and export the .bin file<br>
                    2. Upload the .bin file using the form below<br>
                    3. ESP32 will automatically download and install the update<br>
                    4. Make sure ESP32 is connected to network
                </div>

                <h3>Upload Firmware</h3>
                <form id="uploadForm" enctype="multipart/form-data" onsubmit="uploadFirmware(event)">
                    <div class="form-group">
                        <label>Select .bin file:</label>
                        <input type="file" name="firmware" id="firmware" accept=".bin" required>
                    </div>
                    
                    <button type="submit" class="btn btn-success">📤 Upload Firmware</button>
                </form>

                <div class="progress" id="uploadProgress">
                    <div class="progress-bar" id="progressBar">0%</div>
                </div>

                <div id="uploadStatus" style="margin-top: 20px;"></div>
            </div>

            <div class="card">
                <h3>Available Firmware Files</h3>
                <ul class="firmware-list">
                    {% if firmware_files %}
                        {% for file in firmware_files %}
                        <li>
                            <span>{{ file.name }} ({{ file.size }})</span>
                            <div>
                                <button class="btn btn-small btn-danger" onclick="deleteFirmware('{{ file.name }}')">🗑️ Delete</button>
                            </div>
                        </li>
                        {% endfor %}
                    {% else %}
                        <li>No firmware files uploaded yet</li>
                    {% endif %}
                </ul>
            </div>

            <div class="card">
                <h3>ESP32 Connection Info</h3>
                <p><strong>OTA Hostname:</strong> ESP32-RFID-Laundry</p>
                <p><strong>OTA Password:</strong> laundry_ota_2025</p>
                <p><strong>OTA Port:</strong> 3232</p>
                <p><strong>Alternative Method:</strong> You can also use Arduino IDE's "Network Port" feature</p>
            </div>
        </section>
    </div>

    <script>
        function showSection(id) {
            document.querySelectorAll('section').forEach(sec => sec.classList.remove('active'));
            document.querySelectorAll('.sidebar a').forEach(link => link.classList.remove('active'));
            document.getElementById(id).classList.add('active');
            document.getElementById('link-' + id).classList.add('active');
            window.location.hash = id;
        }
       
        window.onload = function() {
            const hash = window.location.hash.substring(1);
            if (hash) {
                showSection(hash);
            } else {
                showSection('dashboard');
            }
        }
       
        async function getCard() {
            try {
                const response = await fetch("/get_last_card");
                const data = await response.json();
               
                if (data.card_id) {
                    document.getElementById("card_id").value = data.card_id;
                    alert("Card scanned: " + data.card_id);
                } else {
                    alert("No card detected. Please scan a card first.");
                }
            } catch (error) {
                alert("Error: " + error.message);
            }
        }

        async function getCardForSpend() {
            try {
                const response = await fetch("/get_last_card");
                const data = await response.json();
               
                if (data.card_id) {
                    document.getElementById("spend_card_id").value = data.card_id;
                    alert("Card scanned: " + data.card_id);
                } else {
                    alert("No card detected. Please scan a card first.");
                }
            } catch (error) {
                alert("Error: " + error.message);
            }
        }

        async function uploadFirmware(event) {
            event.preventDefault();
            
            const formData = new FormData();
            const fileInput = document.getElementById('firmware');
            const file = fileInput.files[0];
            
            if (!file) {
                alert('Please select a file');
                return;
            }
            
            formData.append('firmware', file);
            
            const progressDiv = document.getElementById('uploadProgress');
            const progressBar = document.getElementById('progressBar');
            const statusDiv = document.getElementById('uploadStatus');
            
            progressDiv.style.display = 'block';
            progressBar.style.width = '0%';
            progressBar.textContent = '0%';
            
            try {
                const xhr = new XMLHttpRequest();
                
                xhr.upload.addEventListener('progress', (e) => {
                    if (e.lengthComputable) {
                        const percent = Math.round((e.loaded / e.total) * 100);
                        progressBar.style.width = percent + '%';
                        progressBar.textContent = percent + '%';
                    }
                });
                
                xhr.addEventListener('load', () => {
                    if (xhr.status === 200) {
                        const response = JSON.parse(xhr.responseText);
                        statusDiv.innerHTML = '<div class="alert alert-success">✓ ' + response.message + '</div>';
                        setTimeout(() => location.reload(), 2000);
                    } else {
                        statusDiv.innerHTML = '<div class="alert alert-danger">✗ Upload failed</div>';
                    }
                    progressDiv.style.display = 'none';
                });
                
                xhr.addEventListener('error', () => {
                    statusDiv.innerHTML = '<div class="alert alert-danger">✗ Upload error</div>';
                    progressDiv.style.display = 'none';
                });
                
                xhr.open('POST', '/upload_firmware');
                xhr.send(formData);
                
            } catch (error) {
                statusDiv.innerHTML = '<div class="alert alert-danger">✗ Error: ' + error.message + '</div>';
                progressDiv.style.display = 'none';
            }
        }

        async function deleteFirmware(filename) {
            if (!confirm('Delete ' + filename + '?')) return;
            
            try {
                const response = await fetch('/delete_firmware', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({filename: filename})
                });
                
                if (response.ok) {
                    alert('Firmware deleted');
                    location.reload();
                } else {
                    alert('Error deleting firmware');
                }
            } catch (error) {
                alert('Error: ' + error.message);
            }
        }
    </script>
</body>
</html>
"""

def init_db():
    """Initialize database with required tables"""
    os.makedirs(DB_DIR, exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
   
    c.execute('''CREATE TABLE IF NOT EXISTS USERS (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT NOT NULL,
        card_id TEXT UNIQUE NOT NULL,
        balance INTEGER DEFAULT 0
    )''')
   
    c.execute('''CREATE TABLE IF NOT EXISTS logs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        card_id TEXT,
        username TEXT,
        action TEXT,
        balance INTEGER,
        timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
    )''')
   
    conn.commit()
    conn.close()
    print("✓ Database initialized at:", DB_PATH)

def get_db():
    """Get database connection"""
    conn = sqlite3.connect(DB_PATH)
    return conn

def log_action(card_id, username, action, balance):
    """Log an action to the database"""
    try:
        conn = get_db()
        c = conn.cursor()
        c.execute("INSERT INTO logs (card_id, username, action, balance) VALUES (?, ?, ?, ?)",
                  (card_id, username, action, balance))
        conn.commit()
        conn.close()
        return True
    except Exception as e:
        print("Error logging action:", e)
        return False

def allowed_file(filename):
    return '.' in filename and filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS

def get_firmware_files():
    """Get list of uploaded firmware files"""
    files = []
    if os.path.exists(UPLOAD_FOLDER):
        for filename in os.listdir(UPLOAD_FOLDER):
            if filename.endswith('.bin'):
                filepath = os.path.join(UPLOAD_FOLDER, filename)
                size = os.path.getsize(filepath)
                size_str = f"{size / 1024:.1f} KB" if size < 1024*1024 else f"{size / (1024*1024):.1f} MB"
                files.append({'name': filename, 'size': size_str})
    return files

@app.route("/")
def index():
    """Main dashboard page"""
    try:
        conn = get_db()
        c = conn.cursor()
       
        c.execute("SELECT id, username, card_id, balance FROM USERS ORDER BY id DESC")
        users = c.fetchall()
       
        c.execute("SELECT * FROM logs ORDER BY timestamp DESC LIMIT 100")
        logs = c.fetchall()
       
        c.execute("SELECT SUM(balance) FROM USERS")
        total_balance = c.fetchone()[0] or 0
       
        conn.close()
       
        firmware_files = get_firmware_files()
       
        return render_template_string(template, users=users, logs=logs, 
                                     total_balance=total_balance, firmware_files=firmware_files)
    except Exception as e:
        return f"Error: {str(e)}", 500

@app.route("/add_user", methods=["POST"])
def add_user():
    """Add a new user"""
    try:
        card_id = request.form.get("card_id", "").strip()
        username = request.form.get("username", "").strip()
        balance = int(request.form.get("balance", 0))
       
        if not card_id or not username:
            return "Card ID and username are required", 400
       
        conn = get_db()
        c = conn.cursor()
       
        c.execute("SELECT id FROM USERS WHERE card_id=?", (card_id,))
        if c.fetchone():
            conn.close()
            return "Card ID already exists!", 400
       
        c.execute("INSERT INTO USERS (username, card_id, balance) VALUES (?, ?, ?)",
                  (username, card_id, balance))
        conn.commit()
        conn.close()
       
        log_action(card_id, username, f"User added with balance {balance}", balance)
       
        return redirect("/#show-users")
    except sqlite3.IntegrityError:
        return "Card ID already exists!", 400
    except Exception as e:
        return f"Error: {str(e)}", 500

@app.route("/update_balance", methods=["POST"])
def update_balance():
    """Update user balance"""
    try:
        card_id = request.form.get("card_id", "").strip()
        added = int(request.form.get("balance", 0))
       
        if not card_id or added <= 0:
            return "Invalid input", 400
       
        conn = get_db()
        c = conn.cursor()
       
        c.execute("UPDATE USERS SET balance = balance + ? WHERE card_id = ?", (added, card_id))
       
        c.execute("SELECT balance, username FROM USERS WHERE card_id=?", (card_id,))
        row = c.fetchone()
       
        if not row:
            conn.close()
            return "User not found", 404
       
        balance, username = row
        conn.commit()
        conn.close()
       
        log_action(card_id, username, f"Balance added +{added}", balance)
       
        return redirect("/#show-users")
    except Exception as e:
        return f"Error: {str(e)}", 500

@app.route("/spend", methods=["POST"])
def spend():
    """Process spending transaction"""
    try:
        card_id = request.form.get("card_id", "").strip()
        hours = int(request.form.get("hours", 1))
        cost = hours
       
        if not card_id:
            return "Card ID required", 400
       
        conn = get_db()
        c = conn.cursor()
       
        c.execute("SELECT balance, username FROM USERS WHERE card_id=?", (card_id,))
        row = c.fetchone()
       
        if not row:
            conn.close()
            return "User not found", 404
       
        balance, username = row
       
        if balance < cost:
            conn.close()
            return f"Insufficient balance! Current: {balance}, Required: {cost}", 400
       
        new_balance = balance - cost
        c.execute("UPDATE USERS SET balance=? WHERE card_id=?", (new_balance, card_id))
        conn.commit()
        conn.close()
       
        log_action(card_id, username, f"Used {hours} hour(s) - {cost} coin(s)", new_balance)
       
        return redirect("/#spending")
    except Exception as e:
        return f"Error: {str(e)}", 500

@app.route("/scan_card", methods=["POST"])
def scan_card():
    """
    Handle RFID card scanning from ESP32
    This is the main endpoint ESP32 uses for transactions
    """
    global LAST_RFID
    
    try:
        data = request.get_json(silent=True) or {}
        card_id = data.get("card_id", "").strip()
        coins = data.get("coins", 0)  # Number of coins from ESP32
        machine_id = data.get("machine_id", "unknown")
       
        if not card_id:
            return jsonify({
                "success": False,
                "user_exists": False,
                "activate_machine": False,
                "message": "No card_id provided"
            }), 400
       
        # Cache the card for web interface
        LAST_RFID = {
            "card_id": card_id,
            "coins": coins,
            "machine_id": machine_id,
            "timestamp": datetime.now().isoformat()
        }
       
        # Get user info
        conn = get_db()
        c = conn.cursor()
        c.execute("SELECT username, balance FROM USERS WHERE card_id=?", (card_id,))
        row = c.fetchone()
       
        if not row:
            conn.close()
            print(f"✗ Unregistered card: {card_id}")
            return jsonify({
                "success": False,
                "user_exists": False,
                "activate_machine": False,
                "message": "Card not registered",
                "card_id": card_id
            })
       
        username, balance = row
       
        # Check if this is just a display scan (no coins) or actual transaction
        if coins == 0:
            # Just displaying card info
            conn.close()
            print(f"✓ Card displayed: {username} (Balance: {balance})")
            return jsonify({
                "success": True,
                "user_exists": True,
                "activate_machine": False,
                "message": f"Welcome {username}",
                "username": username,
                "balance": balance
            })
       
        # Transaction with coins - check balance
        if balance < coins:
            conn.close()
            print(f"✗ Insufficient balance: {username} needs {coins}, has {balance}")
            return jsonify({
                "success": False,
                "user_exists": True,
                "activate_machine": False,
                "message": f"Insufficient balance. Need {coins}, have {balance}",
                "username": username,
                "balance": balance,
                "coins_required": coins
            })
       
        # Sufficient balance - deduct and activate machine
        new_balance = balance - coins
        c.execute("UPDATE USERS SET balance=? WHERE card_id=?", (new_balance, card_id))
        conn.commit()
        conn.close()
       
        # Log transaction
        log_action(card_id, username, 
                  f"Machine {machine_id} used {coins} coin(s)", 
                  new_balance)
       
        print(f"✓ Transaction approved: {username} used {coins} coins, new balance: {new_balance}")
       
        return jsonify({
            "success": True,
            "user_exists": True,
            "activate_machine": True,
            "message": f"Transaction successful. Enjoy your laundry!",
            "username": username,
            "balance": new_balance,
            "coins_used": coins,
            "machine_id": machine_id
        })
       
    except Exception as e:
        print("Error processing card:", e)
        return jsonify({
            "success": False,
            "user_exists": False,
            "activate_machine": False,
            "error": str(e)
        }), 500

@app.route("/get_last_card", methods=["GET"])
def get_last_card():
    """Web interface polls for last scanned card"""
    global LAST_RFID
    
    if LAST_RFID:
        response = {"card_id": LAST_RFID.get("card_id")}
        LAST_RFID = None  # Clear after reading
        return jsonify(response)
    else:
        return jsonify({"error": "No card data available"}), 404

@app.route("/upload_firmware", methods=["POST"])
def upload_firmware():
    """Upload firmware .bin file for OTA"""
    try:
        if 'firmware' not in request.files:
            return jsonify({"error": "No file provided"}), 400
        
        file = request.files['firmware']
        
        if file.filename == '':
            return jsonify({"error": "No file selected"}), 400
        
        if not allowed_file(file.filename):
            return jsonify({"error": "Only .bin files allowed"}), 400
        
        # Secure filename and save
        filename = secure_filename(file.filename)
        
        # Add timestamp to avoid conflicts
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        base_name = filename.rsplit('.', 1)[0]
        filename = f"{base_name}_{timestamp}.bin"
        
        filepath = os.path.join(UPLOAD_FOLDER, filename)
        file.save(filepath)
        
        file_size = os.path.getsize(filepath)
        print(f"✓ Firmware uploaded: {filename} ({file_size} bytes)")
        
        return jsonify({
            "success": True,
            "message": f"Firmware uploaded: {filename}",
            "filename": filename,
            "size": file_size
        })
        
    except Exception as e:
        print(f"✗ Upload error: {e}")
        return jsonify({"error": str(e)}), 500

@app.route("/firmware/<filename>")
def download_firmware(filename):
    """Download firmware file (for ESP32 OTA)"""
    try:
        return send_from_directory(UPLOAD_FOLDER, filename, as_attachment=True)
    except Exception as e:
        return jsonify({"error": str(e)}), 404

@app.route("/delete_firmware", methods=["POST"])
def delete_firmware():
    """Delete firmware file"""
    try:
        data = request.get_json()
        filename = data.get("filename", "")
        
        if not filename:
            return jsonify({"error": "No filename provided"}), 400
        
        filepath = os.path.join(UPLOAD_FOLDER, secure_filename(filename))
        
        if os.path.exists(filepath):
            os.remove(filepath)
            print(f"✓ Firmware deleted: {filename}")
            return jsonify({"success": True, "message": "File deleted"})
        else:
            return jsonify({"error": "File not found"}), 404
            
    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == "__main__":
    print("╔════════════════════════════════════════╗")
    print("║   Laundry Management System v2.0       ║")
    print("║   With OTA Update Support              ║")
    print("╚════════════════════════════════════════╝")
    init_db()
    print("✓ Starting Flask server on http://0.0.0.0:5000")
    print("✓ OTA firmware folder:", UPLOAD_FOLDER)
    app.run(host="0.0.0.0", port=5000, debug=False)
