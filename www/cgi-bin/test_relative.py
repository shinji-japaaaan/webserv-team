#!/usr/bin/python3
import os

print("Status: 200 OK")
print("Content-Type: text/plain")
print()

print("CWD =", os.getcwd())

try:
    with open("test_data.txt", "r") as f:
        print("[SUCCESS] Read ./test_data.txt:")
        print(f.read())
except Exception as e:
    print("[ERROR] Cannot read ./test_data.txt:", e)