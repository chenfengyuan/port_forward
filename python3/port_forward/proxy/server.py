#!/usr/bin/env python
# coding=utf-8
from gevent.server import StreamServer
from gevent import spawn, joinall
from socket import socket
import ipaddress
import struct
from port_forward.utils.ignore_exception import ignore_closed_socket_error
__author__ = 'chenfengyuan'


class BaseServer:
    BUFSIZE = 4096

    def run(self):
        raise NotImplementedError

    def pipe(self, incoming_socket, outgoing_socket, filter_):
        @ignore_closed_socket_error
        def on_incoming():
            while True:
                data = incoming_socket.recv(self.BUFSIZE)
                if not data:
                    outgoing_socket.close()
                    break
                outgoing_socket.send(data)
                if filter_:
                    filter_.on_incoming_socket(data)

        @ignore_closed_socket_error
        def on_outgoing():
            while True:
                data = outgoing_socket.recv(self.BUFSIZE)
                if not data:
                    incoming_socket.close()
                    break
                incoming_socket.send(data)
                if filter_:
                    filter_.on_outgoing_socket(data)
        joinall((spawn(on_outgoing), spawn(on_incoming)))


class DirectServer(BaseServer):

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
        self.pipe(socket_, client_socket, None)


class SOCKSError(RuntimeError):
    pass


class SOCKS5Server(BaseServer):

    def __init__(self, host, port, client_cls, filter_cls=None):
        self.host = host
        self.port = port
        self.server = StreamServer((host, port), self.handle)
        self.client_cls = client_cls
        self.filter_cls = filter_cls
        self.filter = None

    def _socks5_establish_connection(self, socket_):
        # check SOCKS5 version
        data = socket_.recv(1)
        if data != b'\x05':
            raise SOCKSError("WrongSOCKSVersion")
        # get number of authentication methods supported, 1 byte
        data = socket_.recv(1)
        if data[0] == 0:
            raise SOCKSError("WrongNumberOfAuthenticationMethods")
        # get authentication methods, variable length, 1 byte per method supported
        tmp = data[0]
        data = socket_.recv(tmp)
        if len(data) != tmp:
            raise SOCKSError("WrongNumberOfAuthenticationMethods")

        for tmp in data:
            if tmp == 0:
                break
        else:
            socket_.send(b'\x05\xff')
            raise SOCKSError("NoUsableAuthMethod")
        socket_.send(b'\x05\x00')
        data = socket_.recv(4)
        if data[:2] != b'\x05\x01':
            socket_.send(b'\x05\x08')
            raise SOCKSError("CommandNotSupported")
        dst_host_type = data[3:4]
        if data[3] == 0x01:
            data = socket_.recv(4)
            dst_host = ipaddress.IPv4Address(data).compressed
            dst_host_raw = data
        elif data[3] == 0x03:
            dst_host_len = socket_.recv(1)
            dst_host = socket_.recv(dst_host_len[0])
            dst_host_raw = dst_host_len + dst_host
            del dst_host_len
        elif data[3] == 0x04:
            data = socket_.recv(16)
            dst_host = ipaddress.IPv6Address(data).compressed
            dst_host_raw = data
        else:
            socket_.send(b'\x05\x08')
            raise SOCKSError("AddressTypeNotSupported")
        dst_port_raw = socket_.recv(2)
        dst_port = struct.unpack("!H", dst_port_raw)[0]
        try:
            outgoing_socket = self.client_cls(dst_host, dst_port)
        except:
            socket_.close()
            raise
        socket_.send(b'\x05\x00\x00')
        socket_.send(dst_host_type)
        socket_.send(dst_host_raw)
        socket_.send(dst_port_raw)
        self.filter = self.filter_cls((self.host, self.port), (dst_host, dst_port))
        return outgoing_socket

    def run(self):
        self.server.serve_forever()

    def handle(self, socket_, address):
        """
        :type socket_: socket
        :type address: str
        """
        del address
        outgoing_socket = self._socks5_establish_connection(socket_)
        self.pipe(socket_, outgoing_socket, self.filter)

