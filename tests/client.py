import socket
import time

host = '127.0.0.1'
port = 8080

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((host, port))

print("Connected to server. Type messages, or 'exit' to quit.")

try:
    while True:
        msg = input("You: ")
        if msg.lower() == 'exit':
            break
        s.send(msg.encode())
        data = s.recv(1024)
        print("Server:", data.decode())
except KeyboardInterrupt:
    pass
finally:
    s.close()
    print("Disconnected")
