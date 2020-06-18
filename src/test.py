import socket

ip = "127.0.0.1"
port = 8080

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind((ip, port))
s.listen(1)
print("Serve start ...")
while True:
    client_connection, client_address = s.accept()
    request = client_connection.recv(1024)
    print(request)

    http_response = b"""
    HTTP/1.1 200 OK\r\n
    \r\n
    Hello, world!
    """
    client_connection.send(http_response)
    client_connection.close()
