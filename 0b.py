import random
import threading
import os
import time

lock = threading.Lock()

def t():
    while True:
        with lock:
            time.sleep(0.1)
        time.sleep(0.9)
    
threading.Thread(target=t, daemon=True).start()
time.sleep(random.random())

print("About to fork, locked=", lock.locked())
rv = os.fork()

with lock:
    print("got lock", os.getpid())
if rv != 0:
    os.wait()
