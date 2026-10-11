#!/usr/bin/python
# -*- coding: utf-8 -*-
#
# 远程设置 CPU 负载的一次性客户端
#
# 用法:
#   ./oneshot.py <host> <util>          # 默认端口 31415
#   ./oneshot.py <host> <port> <util>   # 指定端口
#
# util 为目标机整机绝对利用率百分比（每核 100，如 4 核发 250 ≈ 62.5%），
# 0 表示清空目标机上所有负载。
# 前置条件: 目标机已运行 cpu_load_generator_server.py。
#
import socket
import argparse
from argparse import RawTextHelpFormatter

PORT = 31415

def set_remote(host, port, util):
    # host = socket.gethostname()  # as both code is running on same pc
    # port = PORT  # socket server port number

    try:
        client_socket = socket.socket()  # instantiate
        client_socket.connect((host, port))  # connect to the server

        client_socket.send(str(util).encode())  # send util
        client_socket.close()  # close the connection
    except Exception as e:
        print(e)

def main():
    parser = argparse.ArgumentParser(description="set remote server CPU load",
                                     formatter_class=RawTextHelpFormatter)
    parser.add_argument("host", type=str, help="hostname or IP")
    parser.add_argument("port", type=int, nargs='?', default=31415, help="IP or hostname")
    parser.add_argument("util", type=int,
                        help="target CPU utilization, in percent (%%)")
    args = parser.parse_args()
    try:
        util = int(args.util)
    except ValueError as ve:
        print("Invalid Parameter: {}, {}.".format(util, ve))
        return 1
    if util < 0:
        print("util should be >= 0")
        return 1
    set_remote(args.host, args.port, util)

if __name__ == '__main__':
    main()
