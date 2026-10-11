#!/usr/bin/python
import socket

PORT = 31415

def client_program():
    host = socket.gethostname()  # as both code is running on same pc
    port = PORT  # socket server port number

    try:
        client_socket = socket.socket()  # instantiate
        client_socket.connect((host, port))  # connect to the server

        message = raw_input(" -> ")  # take input

        while message.lower().strip() != 'q':
            if not message:
                message = raw_input(" -> ")  # again take input
                continue

            client_socket.send(message.encode())  # send message
            data = client_socket.recv(1024).decode()  # receive response

            print('Received from server: ' + data)  # show in terminal

            message = raw_input(" -> ")  # again take input

        client_socket.close()  # close the connection
    except Exception as e:
        print(e)

if __name__ == '__main__':
    client_program()
