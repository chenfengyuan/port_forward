#!/usr/bin/env python
# coding=utf-8
from io import StringIO
from email import message_from_file
from pylru import lrucache
import re
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

    def deactive(self):
        self.active = False
        self.incoming_data = bytearray()
        self.outging_data = bytearray()
        self.on_incoming_socket = self._dummy
        self.on_outgoing_socket = self._dummy

    def deactive_if_need(self):
        if (len(self.incoming_data) > self.MAX_SIZE) or \
                (len(self.outging_data) > self.MAX_SIZE):
            self.deactive()

    def _on_incoming_socket(self, data):
        self.incoming_data += data
        self.on_data(new_incoming_data=data)
        self.deactive_if_need()

    def on_data(self, new_incoming_data=None, new_outgoing_data=None):
        pass

    def _on_outgoing_socket(self, data):
        self.outging_data += data
        self.on_data(new_outgoing_data=data)
        self.deactive_if_need()

    def on_the_end(self):
        pass


def encode(s):
    return """'%s'""" % re.sub("'", """'"'"'""", s)


class HttpRequest:
    def __init__(self, url, headers):
        self.url = url
        self.headers = headers

    def get_curl_cmd(self):
        out = StringIO()
        out.write("curl ")
        out.write(encode(self.url))
        for k, v in self.headers:
            row = '%s: %s' % (k, v)
            out.write(' -H ')
            out.write(encode(row))
        out.write(' --compressed')
        return out.getvalue()


class HTTPDownloadFilter(Filter):

    redirect_trace = lrucache(100)

    def __init__(self, incoming_addr, outgoing_addr):
        super(HTTPDownloadFilter, self).__init__(incoming_addr, outgoing_addr)
        self.request = None
        self.request_url = None

    def on_data(self, new_incoming_data=None, new_outgoing_data=None):
        if new_incoming_data:
            if len(self.incoming_data) >= 4:
                if self.incoming_data[:4] != b'GET ':
                    return self.deactive()
            if b'\r\n\r\n' in self.incoming_data:
                fp = StringIO(self.incoming_data.decode('utf-8'))
                request_line = fp.readline()
                command, path, version = request_line.split(maxsplit=2)
                m = message_from_file(fp)
                headers = m.items()
                # payload = m.get_payload()
                host = None
                for k, v in headers:
                    if k.lower() == 'host':
                        host = v
                        break
                if not host:
                    self.deactive()
                    return
                self.request_url = "http://" + host + path
                if self.request_url in self.redirect_trace:
                    self.request = self.redirect_trace[self.request_url]
                else:
                    self.request = HttpRequest(self.request_url, headers)
                return
        elif new_outgoing_data:
            if not self.request:
                return
            if b'\r\n\r\n' in self.outging_data:
                fp = StringIO(self.outging_data[:self.outging_data.find(b'\r\n\r\n')].decode('utf-8'))
                response_line = fp.readline()
                version, status_code, status_message = response_line.split(maxsplit=2)
                if status_code == '200':
                    m = message_from_file(fp)
                    content_disposition = m.get('Content-Disposition')
                    if content_disposition and 'filename' in content_disposition:
                        if self.request:
                            print(self.request.get_curl_cmd())
                            print('')
                elif status_code == '302':
                    m = message_from_file(fp)
                    location = m.get('Location')
                    if location:
                        self.redirect_trace[location] = self.request
                return self.deactive()

    def on_the_end(self):
        self.on_data()
