#!/usr/bin/env python
# coding=utf-8
from port_forward.utils import monkey_patch
import zmq.green as zmq
import sys

monkey_patch.dummy()


def client(host, port, num):
    context = zmq.Context()

    #  Socket to talk to server
    socket = context.socket(zmq.REQ)
    socket.connect("tcp://%s:%s" % (host, port))

    #  Do 10 requests, waiting each time for a response
    socket.send(num)
    message = socket.recv()
    print(repr(message))


def main():
    host = sys.argv[1]
    port = int(sys.argv[2])
    num = sys.argv[3].encode('utf-8')
    client(host, port, num)

if __name__ == '__main__':
    main()
