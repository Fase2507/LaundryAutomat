from flask import Flask, request, render_template_string, redirect, jsonify
import sqlite3
import os
from datetime import datetime

app = Flask(__name__)

# Database configuration
DB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "database")
DB_PATH = os.path.join(DB_DIR, "laundry.db")

# Server-side cache for last scanned RFID data
LAST_RFID = None

# HTML template
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
        input[type="text"], input[type="number"], select {
            width: 100%;
            padding: 10px;
            border: 2px solid #ddd;
            border-radius: 6px;
            font-size: 10px;
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
                        <div>
                            <input type="text" name="card_id" id="card_id" required placeholder="Scan card or enter manually">
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
                        <input type="text" name="card_id" required placeholder="Enter card ID">
                        <button type="button" class="btn btn-scan" onclick="getCard()">📡 Scan Card</button>

                    </div>

                    <div class="form-group">
                        <label>Select Hours:</label>
                        <select name="hours" required>
                            <option value="1">1 hour (1 coin)</option>
                            <option value="2">2 hours (2 coins)</option>
                            <option value="3">3 hours (3 coins)</option>
                        </select>
                    </div>

                    <button type="submit" class="btn">Process Transaction</button>
                </form>
            </div>
        </section>
    </div>

    <script>
        function showSection(id) {
            // Remove active class from all sections and links
            document.querySelectorAll('section').forEach(sec => sec.classList.remove('active'));
            document.querySelectorAll('.sidebar a').forEach(link => link.classList.remove('active'));
           
            // Add active class to selected section and link
            document.getElementById(id).classList.add('active');
            document.getElementById('link-' + id).classList.add('active');
           
            // Update URL hash
            window.location.hash = id;
        }
       
        // Load section from URL hash on page load
        window.onload = function() {
            const hash = window.location.hash.substring(1);
            if (hash) {
                showSection(hash);
            } else {
                showSection('dashboard');
            }
        }
       
        // Scan card function
        async function getCard() {
            try {
                const response = await fetch("/scan_card");
                const data = await response.json();
               
                if (data.card_id) {
                    document.getElementById("card_id").value = data.card_id;
                    alert("Card scanned: " + data.card_id);
                } else {
                    alert("No card detected. Please try again.");
                }
            } catch (error) {
                alert("Error scanning card: " + error.message);
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
   
    # Create users table
    c.execute('''CREATE TABLE IF NOT EXISTS USERS (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT NOT NULL,
        card_id TEXT UNIQUE NOT NULL,
        balance INTEGER DEFAULT 0
    )''')
   
    # Create logs table
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
    print("Database initialized successfully at:", DB_PATH)

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

@app.route("/")
def index():
    """Main dashboard page"""
    try:
        conn = get_db()
        c = conn.cursor()
       
        # Get all users
        c.execute("SELECT id, username, card_id, balance FROM USERS ORDER BY id DESC")
        users = c.fetchall()
       
        # Get recent logs
        c.execute("SELECT * FROM logs ORDER BY timestamp DESC LIMIT 100")
        logs = c.fetchall()
       
        # Calculate total balance
        c.execute("SELECT SUM(balance) FROM USERS")
        total_balance = c.fetchone()[0] or 0
       
        conn.close()
       
        return render_template_string(template, users=users, logs=logs, total_balance=total_balance)
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
       
        # Check if card already exists
        c.execute("SELECT id FROM USERS WHERE card_id=?", (card_id,))
        if c.fetchone():
            conn.close()
            return "Card ID already exists!", 400
       
        # Insert new user
        c.execute("INSERT INTO USERS (username, card_id, balance) VALUES (?, ?, ?)",
                  (username, card_id, balance))
        conn.commit()
        conn.close()
       
        # Log action
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
       
        # Update balance
        c.execute("UPDATE USERS SET balance = balance + ? WHERE card_id = ?", (added, card_id))
       
        # Get updated balance and username
        c.execute("SELECT balance, username FROM USERS WHERE card_id=?", (card_id,))
        row = c.fetchone()
       
        if not row:
            conn.close()
            return "User not found", 404
       
        balance, username = row
        conn.commit()
        conn.close()
       
        # Log action
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
        cost = hours  # 1 hour = 1 coin
       
        if not card_id:
            return "Card ID required", 400
       
        conn = get_db()
        c = conn.cursor()
       
        # Get current balance
        c.execute("SELECT balance, username FROM USERS WHERE card_id=?", (card_id,))
        row = c.fetchone()
       
        if not row:
            conn.close()
            return "User not found", 404
       
        balance, username = row
       
        # Check sufficient balance
        if balance < cost:
            conn.close()
            return f"Insufficient balance! Current: {balance}, Required: {cost}", 400
       
        # Deduct balance
        new_balance = balance - cost
        c.execute("UPDATE USERS SET balance=? WHERE card_id=?", (new_balance, card_id))
        conn.commit()
        conn.close()
       
        # Log action
        log_action(card_id, username, f"Used {hours} hour(s) - {cost} coin(s)", new_balance)
       
        return redirect("/#spending")
    except Exception as e:
        return f"Error: {str(e)}", 500

@app.route("/scan_card", methods=["GET", "POST"])
def scan_card():
    """Handle RFID card scanning from ESP32"""
    global LAST_RFID
   
    # POST: ESP32 pushes card data
    if request.method == "POST":
        try:
            data = request.get_json(silent=True) or {}
            card_id = data.get("card_id", "").strip()
           
            if not card_id:
                return jsonify({"user_exists": False, "message": "No card_id provided"}), 400
           
            # Cache the card data
            LAST_RFID = {"card_id": card_id, "timestamp": datetime.now().isoformat()}
           
            # Check if user exists
            conn = get_db()
            c = conn.cursor()
            c.execute("SELECT username, balance FROM USERS WHERE card_id=?", (card_id,))
            row = c.fetchone()
            conn.close()
           
            if row:
                username, balance = row
                print(f"✓ Card recognized: {username} (Balance: {balance})")
                return jsonify({
                    "user_exists": True,
                    "message": f"Welcome {username}",
                    "username": username,
                    "balance": balance
                })
            else:
                print(f"✗ Unregistered card: {card_id}")
                return jsonify({
                    "user_exists": False,
                    "message": "Card not registered",
                    "card_id": card_id
                })
        except Exception as e:
            print("Error processing card:", e)
            return jsonify({"error": str(e)}), 500
   
    # GET: Web interface polls for last scanned card
    if LAST_RFID:
        response = {"card_id": LAST_RFID.get("card_id")}
        LAST_RFID = None  # Clear after reading
        return jsonify(response)
    else:
        return jsonify({"error": "No card data available"}), 404

if __name__ == "__main__":
    print("=== Laundry Management System ===")
    init_db()
    # print("Starting Flask server on http://0.0.0.0:5000")
    app.run(host="0.0.0.0", port=5000, debug=False)
	
