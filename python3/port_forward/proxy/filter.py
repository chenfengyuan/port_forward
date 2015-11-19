#!/usr/bin/env python
# coding=utf-8
__author__ = 'chenfengyuan'


class Filter:
    MAX_SIZE = 4096

    def _dummy(self, *args, **kwargs):
        pass

    def __init__(self, incoming_addr, outgoing_addr):
        del incoming_addr
        del outgoing_addr
        self.incoming_data = bytearray()
        self.outging_data = bytearray()
        self.active = True
        self.on_incoming_socket = self._on_incoming_socket
        self.on_outgoing_socket = self._on_outgoing_socket

    def deactive_if_need(self):
        if (len(self.incoming_data) > self.MAX_SIZE) or \
                (len(self.outging_data) > self.MAX_SIZE):
            self.active = False
            self.incoming_data = bytearray()
            self.outging_data = bytearray()

    def _on_incoming_socket(self, data):
        self.incoming_data += data
        self.on_data()
        self.deactive_if_need()

    def on_data(self):
        pass

    def _on_outgoing_socket(self, data):
        self.outging_data += data
        self.on_data()
        self.deactive_if_need()

    def on_the_end(self):
        pass


class HTTPDownloadFilter(Filter):

    def __init__(self, incoming_addr, outgoing_addr):
        super(HTTPDownloadFilter, self).__init__(incoming_addr, outgoing_addr)

    def on_data(self):
        print(self.incoming_data)
        print(self.outging_data)

    def on_the_end(self):
        self.on_data()

