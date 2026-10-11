#!/usr/bin/python
# -*- coding: utf-8 -*-

import os
import time
import signal
import argparse
import subprocess
import multiprocessing as mp
from subprocess import Popen
from multiprocessing import cpu_count
from argparse import RawTextHelpFormatter

class Load:
    def __init__(self, util):
        self.util = util
        self.whole = int(util / 100)
        self.part = int(util % 100)
        self.kill_now = False
        self.loop_list = []
        print("Will run {} loops and {}% left for stress-ng.".
              format(self.whole, self.part))

    def loop(self, x):
        print("START: loop [{}]".format(x))
        while True:
            pass

    def run_part(self, util):
        with open(os.devnull, 'w') as f:
            cmd = "stress-ng -l {} -c 1".format(util)
            self.stressng = Popen(cmd, shell=True, stdout=f, stderr=subprocess.STDOUT)
            print("START: {}".format(cmd))
        return self.stressng

    def clean(self, *args):
        self.kill_now = True    # flag to exit main loop
        if hasattr(self, 'stressng'):
            self.stressng.kill()    # kill stress-ng
            print("EXIT: stress-ng has been killed.")
        for p in self.loop_list:
            p.terminate()       # kill infinite loop
        if self.loop_list:
            print("EXIT: {} loops has been terminated.".format(self.whole))

    def run(self):
        if self.part > 0:
            self.run_part(self.part)

        for x in range(self.whole):
            p = mp.Process(target=self.loop, args=(x+1,))
            p.start()
            self.loop_list.append(p)

        signal.signal(signal.SIGINT, self.clean)
        signal.signal(signal.SIGTERM, self.clean)

        while not self.kill_now:
            time.sleep(1)

        print("DONE")

def main():
    parser = argparse.ArgumentParser(description="Generate pure CPU load as given",
                                     formatter_class=RawTextHelpFormatter)
    parser.add_argument("util", type=int,
                        help="target CPU utilization, in percent (%%)")
    args = parser.parse_args()
    nr_cpus = cpu_count()
    util = args.util
    if util < 10 or util > nr_cpus * 100:
        print("Invalid Util. Should be > 10 and < {} ({}) CPUs!".format(nr_cpus*100, nr_cpus))
    else:
        print("Target CPU Utilization: {}%".format(util))

    ld = Load(args.util)
    ld.run()

if __name__ == "__main__":
    main()
