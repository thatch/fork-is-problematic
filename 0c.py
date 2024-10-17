import multiprocessing
import socket

socket_pool = []

def get_homepage(n=0):
    try:
        sock = socket_pool.pop(0)
    except IndexError:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(("google.com", 80))

    print("Fetching with", sock)

    sock.send(b"GET /generate_204 HTTP/1.1\nHost: www.google.com\n\n")
    data = sock.recv(130)
    print("Read", data[:5], len(data))

    socket_pool.append(sock)

if __name__ == "__main__":
    # Multiple calls are fine
    get_homepage()
    get_homepage()

    with multiprocessing.get_context("spawn").Pool() as p:
        p.map(get_homepage, range(3))
