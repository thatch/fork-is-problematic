import random
import threading
import os
import time

lock = threading.Lock()

with lock:
    time.sleep(0.1)
    
print("About to fork, locked=", lock.locked())
rv = os.fork()

with lock:
    print("got lock", os.getpid())
if rv != 0:
    os.wait()
