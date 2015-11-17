#!/usr/bin/env python
# coding=utf-8
from gevent.server import StreamServer
from gevent import spawn, joinall
from socket import socket
from port_forward.utils.ignore_exception import ignore_closed_socket_error
__author__ = 'chenfengyuan'


class DirectServer:
    BUFSIZE = 4096

    def __init__(self, host, port, dst_host, dst_port, client_cls):
        self.host = host
        self.port = port
        self.dst_host = dst_host
        self.dst_port = dst_port
        self.client_cls = client_cls
        self.server = StreamServer((host, port), self.handle)

    def run(self):
        self.server.serve_forever()

    def handle(self, socket_, address):
        """
        :type socket_: socket
        :type address: str
        """
        del address
        client_socket = self.client_cls(self.dst_host, self.dst_port)

        @ignore_closed_socket_error
        def on_outgoing():
            while True:
                data = socket_.recv(self.BUFSIZE)
                if not data:
                    client_socket.close()
                    break
                client_socket.send(data)

        @ignore_closed_socket_error
        def on_incoming():
            while True:
                data = client_socket.recv(self.BUFSIZE)
                if not data:
                    socket_.close()
                    break
                socket_.send(data)
        joinall((spawn(on_outgoing), spawn(on_incoming)))
