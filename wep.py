from flask import Flask, request, render_template_string, redirect,jsonify
import sqlite3
import os
# from mfrc522 import SimpleMFRC522
# reader = SimpleMFRC522()
app = Flask(__name__)

# HTML template (kept inside the code for simplicity)
template = """
<!DOCTYPE html>
<html>
<head>
    <title>Laundry Admin Panel</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background: #f3f4f6;
            margin: 0;
            padding: 20px;
        }
        h2 {
            color: #333;
            border-bottom: 2px solid #ccc;
            padding-bottom: 5px;
        }
        form {
            background: #fff;
            padding: 15px;
            margin: 15px 0;
            border-radius: 8px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        input[type="text"], input[type="number"] {
            width: 95%;
            padding: 8px;
            margin: 8px 0;
            border: 1px solid #ccc;
            border-radius: 6px;
        }
        input[type="submit"], button {
            background: #007BFF;
            color: white;
            border: none;
            padding: 10px 16px;
            margin-top: 10px;
            border-radius: 6px;
            cursor: pointer;
        }
        input[type="submit"]:hover, button:hover {
            background: #0056b3;
        }
        table {
            border-collapse: collapse;
            width: 100%;
            margin-top: 15px;
            background: #fff;
            border-radius: 8px;
            overflow: hidden;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        th, td {
            padding: 10px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }
        th {
            background: #007BFF;
            color: white;
        }
        tr:hover {
            background: #f1f1f1;
        }


        .sidebar {
            width: 220px;
            background: #2c3e50;
            color: #ecf0f1;
            height: 100vh;
            padding: 20px 10px;
            position: fixed;
        }
        .sidebar h2 {
            text-align: center;
            margin-bottom: 20px;
            color: white;
        }
        .sidebar a {
            display: block;
            color: #ecf0f1;
            text-decoration: none;
            margin: 10px 0;
            padding: 10px;
            border-radius: 5px;
        }
        .sidebar a:hover {
            background: #34495e;
            cursor:pointer;
        }
        .content {
            margin-left: 240px;
            padding: 20px;
            flex-grow: 1;
        }

        section {
            display: none;
        }
        section.active{
            display: block;
        }
        .sidebar table {
            border-collapse: collapse;
            width: 100%;
            margin-top: 10px;
        }
        .sidebar table, th, td {
            border: 1px solid #ddd;
        }

        button {
            padding: 6px 12px;
            background: #2980b9;
            color: #fff;
            border: none;
            border-radius: 4px;
        }
        button:hover {
            background: #1abc9c;
        }
    </style>
</head>
<body>
    <!-- Sidebar -->
    <div class="sidebar">
        <h2>Admin</h2>
        <a onclick="showSection('update-balance')">Update Balance</a>
        <a onclick="showSection('show-users')">Show Users</a>
        <a onclick="showSection('show-logs')">Show Logs</a>
        <a onclick="showSection('spending')">Spending</a>
        <a onclick="showSection('add-users')">Add User</a>
    </div>


    <div class="content">

        <section id="add-users">
            <h2>Add Users</h2>
            <form method="post" action="/add_user">
                Card ID: <input type="text" name="card_id" id="card_id" autofocus><br>
                Username: <input type="text" name="username"><br>
                Balance: <input type="number" name="balance"><br>
                <input type="submit" value="Add User">
            </form>
            <button type="button" onclick="getCard()">Scan Card</button>
        </section>

        <section id="show-users">
             <h2>Users</h2>
            <table border="1">
                <tr><th>Username</th> <th>Balance</th> <th>Card ID</th> <th>Update</th> </tr>
                {% for user in users %}
                <tr>
                    <td>{{ user[1] }}</td>
                    <td>{{ user[3] }}</td>
                    <td>{{ user[2] }}</td>
                    <td>
                        <form action="/update_balance" method="POST" style="display:flex; gap:5px;">
                            <input type="hidden" name="card_id" value="{{ user[2] }}">
                            <input type="number" name="balance" placeholder="+" min="1" style="width:60px;" required>
                            <button type="submit">+</button>
                        </form>
                    </td>
                </tr>
                {% endfor %}
            </table>
        </section>

        <section id="update-balance">
           <h2>Update Balance</h2>
            <form action="/update_balance" method="post">
                <label>Card ID:</label>
                <input type="text" name="card_id" required><br><br>

                <label>Balance:</label>
                <input type="number" name="balance" required><br><br>

                <button type="submit">Update Balance</button>
            </form>
        </section>

        <section id="show-logs">
             <h2>Logs</h2>
            <table border="2">
                <tr><th>Card ID</th><th>Username</th>
                <th>Action</th><th>Amount</th>
                <th>TimeStamp</th></tr>
                {% for row in logs %}
                <tr>
                    <td>{{ row[1] }}</td>
                    <td>{{ row[2] }}</td>
                    <td>{{ row[3] }}</td>
                    <td>{{ row[4] }}</td>
                    <td>{{ row[5] }}</td>
                </tr>
                {% endfor %}
            </table>
        </section>

        <section id="spending">
            <h2>Spending</h2>
            <form action="/spend" method="POST">
                <label for="card_id">Card ID:</label>
                <input type="text" name="card_id" required><br>

                <label for="hours">Select Hours:</label>
                <select name="hours">
                    <option value="1">1 hour (1 coin)</option>
                    <option value="2">2 hours (2 coins)</option>
                    <option value="3">3 hours (3 coins)</option>
                </select><br>

                <button type="submit">Spend</button>
            </form>
            <hr>
        </section>
    </div>

    <script>
        function showSection(id){
            document.querySelectorAll('section').forEach(sec=>sec.classList.remove('active'));

            document.getElementById(id).classList.add('active');

            window.location.hash=id;
        }
        window.onload=function(){
            const hash = window.location.hash.substring(1);
            if(hash){
                showSection(hash);
            }
        }
        async function getCard(){
            let response = await fetch("/scan_card");
            let data = await response.json();
            document.getElementById("card_id").value = data.card_id;
        }
    </script>
</body>
</html>
"""

def get_db():
    db_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "database/laundry.db"))
    print("DB PATH:", db_path)
    conn = sqlite3.connect(db_path)
    return conn

@app.route("/")
def index():
    conn = get_db()
    c = conn.cursor()
    c.execute("SELECT id,username, card_id, balance  FROM USERS")
    users = c.fetchall()

    #LOGS
    c.execute("SELECT * FROM logs ORDER BY timestamp DESC LIMIT 20")
    logs = c.fetchall()
    conn.close()
    return render_template_string(template, users=users, logs=logs)


@app.route("/add_user", methods=["POST"])
def add_user():
    card_id = request.form["card_id"]#rfid.read()
    username = request.form["username"]
    balance = request.form["balance"]
    conn = get_db()
    c = conn.cursor()
    c.execute("INSERT OR IGNORE INTO USERS ( username,card_id,balance) VALUES (?, ?, ?)",
              (username,card_id, balance))
    conn.commit()
    conn.close()

    #Log action
    log_action(card_id,username,f"User added ({username}), Balance={balance}",balance)
    return redirect("/#add-users")

@app.route("/update_balance", methods=["POST"])
def update_balance():
    card_id = request.form.get("card_id")
    added = request.form.get("balance")

    if not card_id or not added:
        return redirect("/#show-users")

    conn = get_db()
    c = conn.cursor()

    # Update balance directly in SQL
    c.execute("UPDATE USERS SET balance = balance + ? WHERE card_id = ?", (added, card_id))

    # Fetch updated balance and username
    c.execute("SELECT balance, username FROM USERS WHERE card_id=?", (card_id,))
    row = c.fetchone()
    balance = row[0] if row else 0
    username = row[1] if row else "Unknown"

    conn.commit()
    conn.close()

    # Log with new balance
    log_action(card_id, username, f"Money added ({username}), +{added}", balance)

    return redirect("/#show-users")

#
# @app.route("/update_balance", methods=["POST"])
# def update_balance():
#     card_id = request.form.get("card_id")
#     added = request.form.get("balance")
#     # username = request.form.get("username")
#     conn = get_db()
#     c = conn.cursor()
#     #fetch balance
#     c.execute("SELECT balance FROM USERS WHERE card_id=?",(card_id,))
#     bal_row = c.fetchone()
#     balance = bal_row[0]
#     new_balance = int(added)+int(balance)
#     # fetch username
#     c.execute("SELECT username FROM USERS WHERE card_id=?", (card_id,))
#     row = c.fetchone()
#     username = row[0] if row else "Unknown"
#
#     # update balance
#     c.execute("UPDATE USERS SET balance=? WHERE card_id=?", (new_balance, card_id))
#
#
#     conn.commit()
#     conn.close()
#
#     log_action(card_id, username,f"Money added ({username}), Added={added}", new_balance)
#
#     return redirect("/#update-balance")

def log_action(card_id,username,action,balance):
    conn = get_db()
    c = conn.cursor()
    c.execute("INSERT INTO logs (card_id,username,action,balance) VALUES (?,?,?,?)",(card_id,username,action,balance))
    conn.commit()
    conn.close()
    return redirect("/#show-logs")
@app.route("/spend", methods=["POST"])
def spend():
    card_id = request.form.get("card_id")
    hours = int(request.form.get("hours"))

    # coin cost = hours (1h=1, 2h=2, 3h=3)
    cost = hours

    conn = get_db()
    c = conn.cursor()

    # check current balance
    c.execute("SELECT balance, username FROM USERS WHERE card_id=?", (card_id,))
    row = c.fetchone()

    if not row:
        conn.close()
        return "User not found", 400

    balance, username = row
    if balance < cost:
        conn.close()
        return f"Not enough balance! Current={balance}, Need={cost}", 400

    # deduct balance
    new_balance = balance - cost
    c.execute("UPDATE USERS SET balance=? WHERE card_id=?", (new_balance, card_id))

    # log action
    action = f"{username} {hours} hour coin"
    c.execute("INSERT INTO logs (username,card_id, action, balance) VALUES (?,?, ?, ?)",
              (username,card_id, action, new_balance))

    conn.commit()
    conn.close()

    return redirect("/#spending")


# def read_rfid():
#     # This will block until a card is scanned
#     card_id, text = reader.read()
#     return str(card_id)
#
# @app.route("/scan_card")
# def scan_card():
#     card_id = read_rfid()
#     return jsonify({"card_id": card_id})


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000,debug=False)
