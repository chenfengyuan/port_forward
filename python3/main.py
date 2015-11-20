#!/usr/bin/env python
# coding=utf-8
from port_forward.utils import monkey_patch
from port_forward.proxy import server, client
import port_forward.proxy.filter as filter_util
__author__ = 'chenfengyuan'


monkey_patch.dummy()
s = server.SOCKS5Server('127.0.0.1', 9990, client.DirectClient, filter_util.HTTPDownloadFilter)
s.run()
