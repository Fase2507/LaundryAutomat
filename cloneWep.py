from flask import Flask, g, request, render_template_string, redirect, jsonify
import sqlite3
import os
from datetime import datetime
import threading
import time

app = Flask(__name__)

# Thread lock for database operations
db_lock = threading.Lock()

# Database configuration
DB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "database")
DB_PATH = os.path.join(DB_DIR, "laundry.db")

# Server-side cache for last scanned RFID data
LAST_RFID = None

# HTML template (same as before, keeping it intact)
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
                            <input type="text" name="card_id" class="card_id_input"  required placeholder="Scan card or enter manually">
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
                        <input type="text" name="card_id" class="card_id_input"  required placeholder="Enter card ID">
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
                    document.querySelectorAll(".card_id_input").forEach(input=>{
                        if(input.closest("section").classList.contains("active")) {
                            input.value = data.card_id;
                        }
                    });
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
    """Initialize database with required tables and WAL mode"""
    os.makedirs(DB_DIR, exist_ok=True)
    with db_lock:
        conn = sqlite3.connect(DB_PATH, timeout=30.0)
        c = conn.cursor()
        
        # Enable WAL mode for better concurrency
        c.execute("PRAGMA journal_mode=WAL")
        c.execute("PRAGMA synchronous=NORMAL")
        
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

def get_db_connection():
    """Get a fresh database connection with timeout"""
    return sqlite3.connect(DB_PATH, timeout=10.0)

def log_action(card_id, username, action, balance):
    """Thread-safe log action with retry mechanism"""
    max_attempts = 3
    for attempt in range(max_attempts):
        try:
            with db_lock:
                conn = get_db_connection()
                c = conn.cursor()
                c.execute("INSERT INTO logs (card_id, username, action, balance) VALUES (?, ?, ?, ?)",
                         (card_id, username, action, balance))
                conn.commit()
                conn.close()
            return True
        except sqlite3.OperationalError as e:
            if "locked" in str(e) and attempt < max_attempts - 1:
                print(f"Database locked, retrying logging ({attempt + 1}/{max_attempts})...")
                time.sleep(0.1 * (attempt + 1))
            else:
                print(f"Failed to log after {max_attempts} attempts: {e}")
                return False
        except Exception as e:
            print(f"Logging error: {e}")
            return False
    return False

def execute_db_query(query, params=(), fetchone=False, fetchall=False):
    """Execute database query with thread safety and retry"""
    max_attempts = 3
    for attempt in range(max_attempts):
        try:
            with db_lock:
                conn = get_db_connection()
                c = conn.cursor()
                c.execute(query, params)
                
                if fetchone:
                    result = c.fetchone()
                elif fetchall:
                    result = c.fetchall()
                else:
                    result = None
                
                conn.commit()
                conn.close()
                return result
                
        except sqlite3.OperationalError as e:
            if "locked" in str(e) and attempt < max_attempts - 1:
                print(f"Database locked, retrying query ({attempt + 1}/{max_attempts})...")
                time.sleep(0.1 * (attempt + 1))
            else:
                raise e
        except Exception as e:
            raise e

@app.route("/")
def index():
    """Main dashboard page"""
    try:
        with db_lock:
            conn = get_db_connection()
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
        
        # Use thread-safe query
        existing_user = execute_db_query(
            "SELECT id FROM USERS WHERE card_id=?", 
            (card_id,), 
            fetchone=True
        )
        
        if existing_user:
            return "Card ID already exists!", 400
        
        # Insert new user
        execute_db_query(
            "INSERT INTO USERS (username, card_id, balance) VALUES (?, ?, ?)",
            (username, card_id, balance)
        )
       
        # Log action (non-blocking)
        log_action(card_id, username, f"User added with balance {balance}", balance)
       
        return redirect("/#show-users")
    except ValueError:
        return "Invalid balance value", 400
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
        
        # Update balance
        execute_db_query(
            "UPDATE USERS SET balance = balance + ? WHERE card_id = ?", 
            (added, card_id)
        )
        
        # Get updated info
        user = execute_db_query(
            "SELECT balance, username FROM USERS WHERE card_id=?", 
            (card_id,), 
            fetchone=True
        )
        
        if not user:
            return "User not found", 404
       
        balance, username = user
        
        # Log action
        log_action(card_id, username, f"Balance added +{added}", balance)
       
        return redirect("/#show-users")
    except ValueError:
        return "Invalid amount", 400
    except Exception as e:
        return f"Error: {str(e)}", 500

@app.route("/spend", methods=["POST"])
def spend():
    """Process spending transaction (web interface)"""
    try:
        card_id = request.form.get("card_id", "").strip()
        hours = int(request.form.get("hours", 1))
        cost = hours  # 1 hour = 1 coin
       
        if not card_id:
            return "Card ID required", 400
        
        # Get user info
        user = execute_db_query(
            "SELECT balance, username FROM USERS WHERE card_id=?", 
            (card_id,), 
            fetchone=True
        )
        
        if not user:
            return "User not found", 404
       
        balance, username = user
       
        # Check sufficient balance
        if balance < cost:
            return f"Insufficient balance! Current: {balance}, Required: {cost}", 400
       
        # Deduct balance
        new_balance = balance - cost
        execute_db_query(
            "UPDATE USERS SET balance=? WHERE card_id=?", 
            (new_balance, card_id)
        )
       
        # Log action
        log_action(card_id, username, f"Used {hours} hour(s) - {cost} coin(s)", new_balance)
       
        return redirect("/#spending")
    except ValueError:
        return "Invalid hours value", 400
    except Exception as e:
        return f"Error: {str(e)}", 500

@app.route("/scan_card", methods=["GET", "POST"])
def scan_card():
    """Handle RFID card scanning from ESP32 and web interface"""
    global LAST_RFID
    
    # POST: ESP32 pushes card data
    if request.method == "POST":
        try:
            data = request.get_json(silent=True)
            if not data:
                return jsonify({
                    "success": False,
                    "message": "No JSON data received"
                }), 400
            
            # Extract and validate data
            card_id = data.get("card_id", "").strip()
            coins_requested = int(data.get("coins", 1))
            machine_id = data.get("machine_id", "laundry_machine_1")
            
            if not card_id:
                return jsonify({
                    "success": False,
                    "message": "Card ID is required"
                }), 400
            
            if coins_requested <= 0:
                return jsonify({
                    "success": False,
                    "message": "Coins must be greater than 0"
                }), 400
            
            # Cache for web interface
            LAST_RFID = {
                "card_id": card_id,
                "coins": coins_requested,
                "timestamp": datetime.now().isoformat(),
                "machine_id": machine_id
            }
            
            # Check if user exists
            user = execute_db_query(
                "SELECT username, balance FROM USERS WHERE card_id=?",
                (card_id,),
                fetchone=True
            )
            
            if not user:
                return jsonify({
                    "success": False,
                    "user_exists": False,
                    "activate_machine": False,
                    "message": "Card not registered. Please register first.",
                    "card_id": card_id
                })
            
            username, balance = user
            
            # Check if user has sufficient balance
            if balance < coins_requested:
                return jsonify({
                    "success": False,
                    "user_exists": True,
                    "activate_machine": False,
                    "balance": balance,
                    "coins_requested": coins_requested,
                    "message": f"Insufficient balance. Need {coins_requested}, have {balance}"
                })
            
            # SUCCESS: Deduct coins
            new_balance = balance - coins_requested
            execute_db_query(
                "UPDATE USERS SET balance=? WHERE card_id=?",
                (new_balance, card_id)
            )
            
            # Log transaction (non-blocking)
            log_action(
                card_id=card_id,
                username=username,
                action=f"Used {coins_requested} coin(s) on {machine_id}",
                balance=new_balance
            )
            
            print(f"✓ Transaction successful: {username} used {coins_requested} coin(s). New balance: {new_balance}")
            
            return jsonify({
                "success": True,
                "user_exists": True,
                "activate_machine": True,
                "balance": new_balance,
                "coins_used": coins_requested,
                "coins_requested": coins_requested,
                "username": username,
                "message": f"Transaction successful. {coins_requested} coin(s) deducted. New balance: {new_balance}"
            })
            
        except ValueError:
            return jsonify({
                "success": False,
                "message": "Invalid data format. Coins must be a number."
            }), 400
        except Exception as e:
            return jsonify({
                "success": False,
                "message": f"Server error: {str(e)}"
            }), 500
    
    # GET: Web interface polls for last scanned card
    else:
        if LAST_RFID:
            response = {
                "success": True,
                "card_id": LAST_RFID.get("card_id"),
                "coins": LAST_RFID.get("coins", 1),
                "timestamp": LAST_RFID.get("timestamp"),
                "machine_id": LAST_RFID.get("machine_id", "laundry_machine_1")
            }
            # Don't clear immediately to allow multiple reads
            return jsonify(response)
        else:
            return jsonify({
                "success": False,
                "message": "No recent card scan detected"
            }), 404

@app.route("/spend_api", methods=["GET", "POST"])
def spend_api():
    """Legacy API endpoint - now use /scan_card instead"""
    return jsonify({
        "success": False,
        "message": "This endpoint is deprecated. Use /scan_card instead."
    }), 410  # 410 Gone

@app.teardown_appcontext
def close_db(error):
    """Close any remaining database connections"""
    pass  # Connections are now managed in each function

if __name__ == "__main__":
    print("=== Laundry Management System ===")
    print("Initializing database...")
    init_db()
    print("Starting Flask server on http://0.0.0.0:5000")
    print("Press Ctrl+C to stop")
    app.run(host="0.0.0.0", port=5000, debug=False, threaded=True)