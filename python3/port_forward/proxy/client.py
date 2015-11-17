#!/usr/bin/env python
# coding=utf-8
from gevent import socket
__author__ = 'chenfengyuan'


class DirectClient:
    def __init__(self, host, port):
        self.socket = socket.create_connection((host, port))

    def send(self, bytes_):
        self.socket.send(bytes_)

    def recv(self, bufsize):
        return self.socket.recv(bufsize)

    def close(self):
        self.socket.close()
