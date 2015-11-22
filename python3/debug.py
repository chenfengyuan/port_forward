#!/usr/bin/env python
# coding=utf-8
from port_forward.utils import monkey_patch
from port_forward.proxy import server, client
import port_forward.proxy.filter as filter_util
import sys
__author__ = 'chenfengyuan'


monkey_patch.dummy()


def main():
    host = sys.argv[1]
    port = int(sys.argv[2])
    dst_host = sys.argv[3]
    dst_port = int(sys.argv[4])
    s = server.DirectServer(host, port, dst_host, dst_port, client.DirectClient, filter_util.DebugFilter)
    s.run()


if __name__ == '__main__':
    main()
