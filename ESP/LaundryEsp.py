from machine import Pin, SoftSPI
from mfrc522 import MFRC522
import usocket as socket 
import network
import json

SSID = "yourAp"
PASSWORD = "yourPassword"

def conn_wifi():
	wlan = network.WLAN(network.STA_IF)
	wlan.active(True)
	if not wlan.isconnected():
		print("Connecting...")
		wlan.connect(SSID,PASSWORD)
		while not wlan.isconnected():
			pass
	print("Network config: ",wlan.ifconfig())

# initialize rfid
rdr = MFRC522(
	sck = Pin(18),
	mosi = Pin(23),
	miso = Pin(19),
	rst = Pin(22),
	cs = Pin(5)
)

def read_rfid_id():
	""" read rfid and return UID """
	(stat, tag_type) = rdr.request(rdr.REQDL)
	if stat == rdr.ok:
		(stat, raw_uid) = rdr.anticoll()
		if stat == rdr.ok:
			uid = ":".join("%02x" % b for b in raw_uid)
			return uid
	return None

def start_server():
	connect_wifi()
	s = socket.socket()
	s.blind(('0.0.0.0',8080))
	s.listen(5)
	print("RFID server running on 8080 PORT")
	
	while True:
		conn, addr = s.accept()
		request = conn.recv(1024)
		
		#check if it's a scan request
		if b'/scan' in request:
			uid = read_rfid()
			if uid:
				response = json.dumps({"card_id": uid})
				conn.send("HTTP/1.1 200 Ok\r\n")
				conn.send("Content-Type: application/json\r\n")
				conn.send("Connection: close\r\n\r\n")
				conn.send(response)
			else:
				conn.send("HTTP/1.1 404 Not Found\r\n\r\n")
		conn.close()
start_server()
