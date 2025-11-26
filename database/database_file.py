
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
                timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
)
 ''')
# c.execute(''' ALTER TABLE USERS ADD COLUMN username TEXT ''')
# c.execute(''' DELETE from   LOGS  ''')
# hours = int(input("saat"))
# card_id = input("ID giriniz")#rfid.read()
# username = input("Username gir")
# balance = int(input("Tutar giriniz"))
# action = f"{balance-hours}"
# log_balance = balance-hours

# c.execute("INSERT OR IGNORE INTO USERS (username,card_id, balance) VALUES (?,?, ?)", (username,card_id, balance))
# c.execute("INSERT OR IGNORE INTO logs (card_id,action,balance) VALUES (?,?,?)", (card_id, action,log_balance))

conn.commit()
conn.close()
print("database initialized")