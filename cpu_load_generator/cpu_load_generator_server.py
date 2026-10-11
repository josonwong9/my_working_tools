#!/usr/bin/python
# -*- coding: utf-8 -*-
#
# CPU 负载生成服务端 — 运行在目标机/目标容器内
#
# 用法:
#   ./cpu_load_generator_server.py
#
# 监听 31415 端口，接收客户端发送的 util（整机绝对百分比，每核 100），
# 0 或负数表示清空所有负载。Ctrl-C 退出并清理负载。
#
import os
import sys
import time
import socket
import signal
import argparse
import subprocess
import multiprocessing as mp
from subprocess import Popen
from multiprocessing import cpu_count
from argparse import RawTextHelpFormatter

PORT = 31415

class Load:
    def __init__(self, util=0):
        self.update(util)

    def update(self, util):
        self.util = util
        self.whole = int(util / 100)
        self.part = int(util % 100)
        self.stressng = None
        self.loop_list = []

    def loop(self, x):
        print("START: loop [{}]".format(x))
        while True:
            pass

    def run_part(self, util):
        with open(os.devnull, 'w') as f:
            cmd = "stress-ng -l {} -c 1 -k -q".format(util)
            self.stressng = Popen(cmd, shell=True, stdout=f, stderr=subprocess.STDOUT)
            print("START: {}".format(cmd))
        return self.stressng

    def clean(self):
        if self.stressng and self.stressng.poll() is None:
            self.stressng.kill()    # kill stress-ng
            print("EXIT: stress-ng has been killed.")
            # while self.stressng.poll() is None:
            #     pass
        for p in self.loop_list:
            p.terminate()       # kill infinite loop
        # for p in self.loop_list:
        #     while p.is_alive():
        #         time.sleep(0.5)
        if self.loop_list:
            print("EXIT: {} loops has been terminated.".format(self.whole))
        self.stressng = None
        self.loop_list = []

    def spawn(self):
        print("Will run {} loops and {}% left for stress-ng.".
              format(self.whole, self.part))
        if self.part > 0:
            self.run_part(self.part)

        for x in range(self.whole):
            p = mp.Process(target=self.loop, args=(x+1,))
            p.start()
            self.loop_list.append(p)

    def reset(self, util):
        self.clean()
        self.update(util)
        self.spawn()

class Server:
    def __init__(self):
        self.kill_now = False
        signal.signal(signal.SIGINT, self.ready_exit)
        signal.signal(signal.SIGTERM, self.ready_exit)

    def ready_exit(self, *args):
        self.kill_now = True
        if hasattr(self, 'load'):
            self.load.clean()
        self.server_socket.close()
        exit(1)

    def listen(self):
        # get the hostname
        host = socket.gethostname()
        port = PORT  # initiate port no above 1024

        self.server_socket = socket.socket()  # get instance
        # look closely. The bind() function takes tuple as argument
        self.server_socket.bind((host, port))  # bind host address and port together

        # configure how many client the server can listen simultaneously
        self.server_socket.listen(1)
        print("Listening on port {}...".format(PORT))
        self.load = Load()
        while not self.kill_now:
            conn, address = self.server_socket.accept()  # accept new connection
            print("Connection from: " + str(address))
            try:
                while not self.kill_now:
                    # receive data stream. it won't accept data packet greater than 1024 bytes
                    data = conn.recv(1024).decode()
                    if data == '':
                        break
                    try:
                        util = int(data)
                        print("-"*72)
                        print("RECV: {}".format(util))
                        if util <= 0:
                            print("CLEAN all load.")
                            self.load.clean()
                        else:
                            self.load.reset(util)
                    except ValueError as ve:
                        err_msg = "{}. Invalid parameter: {}.".format(ve, data)
                        print(err_msg)
                        conn.send(err_msg.encode())  # send data to the client
                        continue
                    conn.send(data.encode())
                exit_msg = "server exit!"
                conn.send(exit_msg.encode())
                conn.close()  # close the connection
                print("Continue listening...")
            except Exception as e:
                print("{}. Continue listening...".format(e))
                continue

if __name__ == '__main__':
    # fn = '/dev/null'
    # fn = 'cpu_load_generator.log'
    # sys.stdout = open(fn, 'w')
    server = Server()
    server.listen()
