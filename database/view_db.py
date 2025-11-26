import pandas as pd


import sqlite3
import os

db_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "laundry.db"))
conn = sqlite3.connect(db_path)
# c = conn.cursor()
# print("USERS: ")
# for row in c.execute("SELECT * FROM USERS"):
#     print(row)

users = pd.read_sql_query("SELECT * FROM USERS", conn)
logs = pd.read_sql_query("SELECT * FROM logs ORDER BY timestamp DESC LIMIT 20", conn)
conn.close()

print("=== USERS TABLE ===")
print(users.to_string(index=False))  # formatted like Excel

print("\n=== LOGS TABLE ===")
print(logs.to_string(index=False))   # formatted like Excel