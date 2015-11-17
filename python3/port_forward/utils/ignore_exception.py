#!/usr/bin/env python
# coding=utf-8
from functools import wraps
__author__ = 'chenfengyuan'


def ignore_closed_socket_error(func):
    @wraps(func)
    def wrapper(*args, **kwargs):
        try:
            return func(*args, **kwargs)
        except OSError as e:
            if e.strerror != 'File descriptor was closed in another greenlet':
                raise
    return wrapper
