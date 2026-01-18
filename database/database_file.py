
# print(sqlite3.sqlite_version)

import sqlite3
import os

# print(sqlite3.sqlite_version)

db_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "laundry.db"))
conn = sqlite3.connect("../laundry.db")
c = conn.cursor()

c.execute("""CREATE TABLE IF NOT EXISTS USERS(
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                username TEXT,
                card_id TEXT UNIQUE,
                balance INTEGER
)
""")

c.execute('''CREATE TABLE IF NOT EXISTS logs(
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                card_id TEXT,
                username TEXT,
                action TEXT,
                balance INTEGER,
	        timestamp DATETIME DEFAULT (datetime('now', '+3 hours'))

)
 ''')

conn.commit()
conn.close()
print("database initialized")
