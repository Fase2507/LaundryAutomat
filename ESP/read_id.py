from machine import Pin, SPI
from mfrc522 import MFRC522
from time import sleep

# ESP32 RFID-RC522 Connection
# SPI Bus pins
sck = Pin(18, Pin.OUT)    # Clock
mosi = Pin(23, Pin.OUT)   # MOSI (Master Out Slave In)
miso = Pin(19, Pin.IN)    # MISO (Master In Slave Out)
spi = SPI(2, baudrate=115200, polarity=0, phase=0, sck=sck, mosi=mosi, miso=miso)

# Chip Select pin
sda = Pin(5, Pin.OUT)     # SDA (Serial Data Signal)

def read_rfid():
    try:
        rdr = MFRC522(spi=spi, gpioRst=4, gpioCs=sda)
        print("Looking for cards...")
        
        while True:
            (stat, tag_type) = rdr.request(rdr.REQIDL)
            
            if stat == rdr.OK:
                (stat, uid) = rdr.anticoll()
                if stat == rdr.OK:
                    card_id = "".join(map(str, uid))
                    print(f"Card detected! UID: {card_id}")
                    sleep(2)  # Wait before next read
                    
    except KeyboardInterrupt:
        print("\nProgram terminated by user")
    finally:
        pass  # No cleanup needed for ESP32

if __name__ == "__main__":
    read_rfid()
        