#!/usr/bin/env python
# coding=utf-8
from port_forward.utils import monkey_patch
from port_forward.proxy import server, client
__author__ = 'chenfengyuan'


monkey_patch.dummy()
s = server.DirectServer('127.0.0.1', 9990, '127.0.0.1', 9991, client.DirectClient)
s.run()
print('end')
