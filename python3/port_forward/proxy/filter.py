#!/usr/bin/env python
# coding=utf-8
from io import StringIO
from email import message_from_file
from pylru import lrucache
import re
import os.path
import urllib.parse
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


def get_filename(s):
    s = re.findall('filename="?([^"]+)"?', s)
    if not s:
        return None
    return os.path.basename(urllib.parse.unquote(s[0]))


class HttpDownload:
    __fields__ = ['url', 'headers', 'content_length', 'filename']

    def __init__(self, url, headers, content_length=None, filename=None):
        self.url = url
        self.headers = headers
        self.content_length = content_length
        self.filename = filename

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

    def __repr__(self):
        return "HttpDownload(%s, %s, %s, %s)" % (repr(self.url), repr(self.headers),
                                                 repr(self.content_length), repr(self.filename))

    def get_dict(self):
        rv = {}
        for k in self.__fields__:
            rv[k] = getattr(self, k)
        return rv


class HTTPDownloadFilter(Filter):

    redirect_trace = lrucache(100)
    download_num = 0
    downloads = lrucache(100)

    def __init__(self, incoming_addr, outgoing_addr):
        super(HTTPDownloadFilter, self).__init__(incoming_addr, outgoing_addr)
        self.download = None

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
                request_url = "http://" + host + path
                if request_url in self.redirect_trace:
                    self.download = self.redirect_trace[request_url]
                else:
                    self.download = HttpDownload(request_url, headers)
                return
        elif new_outgoing_data:
            if not self.download:
                return
            if b'\r\n\r\n' in self.outging_data:
                fp = StringIO(self.outging_data[:self.outging_data.find(b'\r\n\r\n')].decode('utf-8'))
                response_line = fp.readline()
                version, status_code, status_message = response_line.split(maxsplit=2)
                if status_code == '200':
                    m = message_from_file(fp)
                    content_disposition = m.get('Content-Disposition')
                    content_length = m.get('Content-Length')
                    if content_length and content_disposition:
                        content_length = int(content_length)
                        filename = get_filename(content_disposition)
                        if content_length and filename:
                            self.download.content_length = content_length
                            self.download.filename = filename
                            print('')
                            print('# %s %s' % (self.add_download(self.download), self.download.filename))
                            print(self.download.get_curl_cmd())
                elif status_code == '302':
                    m = message_from_file(fp)
                    location = m.get('Location')
                    if location:
                        self.redirect_trace[location] = self.download
                return self.deactive()

    def on_the_end(self):
        self.on_data()

    @classmethod
    def add_download(cls, download):
        """
        :type download: HttpDownload
        """
        num = cls.download_num
        cls.downloads[num] = download
        cls.download_num += 1
        return num

    @classmethod
    def get_download(cls, num):
        if num in cls.downloads:
            return cls.downloads[num].get_dict()
        else:
            return {}

    @classmethod
    def start_server(cls, host, port):
        import zmq.green as zmq
        import json
        import gevent

        def server():
            context = zmq.Context()
            socket = context.socket(zmq.REP)
            socket.bind("tcp://%s:%s" % (host, port))

            while True:
                try:
                    num = int(socket.recv())
                except (TypeError, ValueError):
                    socket.send(b'{}')
                    continue
                data = json.dumps(cls.get_download(num))
                socket.send(data.encode('utf-8'))
        return gevent.spawn(server)


class DebugFilter:

    def on_incoming_socket(self, data):
        del self
        print(b'i:%s' % data)

    def on_outgoing_socket(self, data):
        del self
        print(b'o:%s' % data)
