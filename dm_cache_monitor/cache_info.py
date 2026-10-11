#!/usr/bin/env python
# -*- coding:utf-8 -*-
#dmsetup table vg-data1 | awk '{print $6}' | awk -F : '{ print $1"[[:space:]]*,[[:space:]]*"$2}'
import os
import re
import time
import json
import socket
old_record = []
now_record = []
old_ios = []
new_ios = []
cache_request = []
point = []
ent   = {
            "name": "${metric_name}",
            "timestamp": 1433654603,
            "value": 0,
            "tags": {
                "host" : "your host name",
                "dev"  : "..."
            }
        }
old_cache_count = 0
now_cache_count = 0
tick = 8
format = ['cache_name','dm_name', 'hdd_name','r_iops','w_iops','read_hit_rate','write_hit_rate','demote','promote','used_rate','dirty_rate']
def cache_hdd_name(cache_name):
	tmp = 'dmsetup table %s'%cache_name
	tmp = os.popen(tmp).read()
	tmp = tmp.split("\n",-1)
	tmp = tmp[0];
	tmp = tmp.split()
	tmp = tmp[5]
	tmp = tmp.split(":")
	major = tmp[0]
	minor = tmp[1]
	s = '[[:space:]]{}[[:space:]]*,[[:space:]]*{}[[:space:]]'.format(major,minor)
	s = 'ls /dev -l | grep "{}"'.format(s)
	s = os.popen(s).read()
	s = s.split("\n")
	s = s[0]
	s = s.split()
	s = s[9]
	s = 'ls /dev/mapper -l | grep "{}$"'.format(s)
	s = os.popen(s).read()
	s = s.split("\n")
	s = s[0]
	s = s.split()
	s = s[8]
	s = 'dmsetup table {}'.format(s)
	s = os.popen(s).read()
	s = s.split("\n")
	s = s[0]
	s = s.split()
	s = s[3]
	s = s.split(":")
	major = s[0]
	minor = s[1]
	s = '[[:space:]]{}[[:space:]]*,[[:space:]]*{}[[:space:]]'.format(major,minor)
	s = 'ls /dev/ -l | grep "{}"'.format(s)
	s = os.popen(s).read() 
	s = s.split("\n")
	s = s[0]
	s = s.split()
	s = s[len(s) -1]
	s = re.sub("\d",'',s)
	return s
def cache_dm_name(cache_name):
	s = 'ls /dev/mapper -l | grep "{}" | grep -v corig'.format(cache_name)
	s = os.popen(s).read()
	s = s.split("\n")
	s = s[0]
	s = s.split("../")
	s = s[-1]
	return s
def get_dm_ios(dm_name):
	s = 'cat /sys/block/{}/stat'.format(dm_name)
	s = os.popen(s).read()
	s = s.split("\n")
	s = s[0]
	return s
def get_cache_num():
	tmp = os.popen("dmsetup status | grep writeback| wc -l").read()
	tmp = tmp.split("\n")
	tmp = tmp[0]
	tmp = int(tmp)
	return tmp
def get_record():
	global old_record
	global now_record
	global old_cache_count
	global now_cache_count
	global old_ios
	global new_ios
	old_cache_count = get_cache_num()
	tmp = os.popen("dmsetup status | grep writeback | sort -k1 -n").read()
	tmp = tmp.split("\n",-1)
	del tmp[-1]
	del old_record[:]
	del old_ios[:]
	old_record = tmp[:]
	for i in range(0,len(tmp)):
		s = tmp[i].split(":")
		s = s[0]
		s = cache_dm_name(s)
		s = get_dm_ios(s)
		old_ios.append(s)
	time.sleep(tick)
	now_cache_count = get_cache_num()
	tmp = os.popen("dmsetup status | grep writeback | sort -k1 -n").read()
	tmp = tmp.split("\n",-1)
	del tmp[-1]
	del now_record[:]
	del new_ios[:]
	now_record = tmp[:]
	for i in range(0,len(tmp)):
		s = tmp[i].split(":")
		s = s[0]
		s = cache_dm_name(s)
		s = get_dm_ios(s)
		new_ios.append(s)
	return
def make_request():
	global now_record
	global old_record
	global new_ios
	global old_ios
	global old_count
	global cache_request
	global tick
	if len(now_record) == 0 :
		del old_record[:]
		return
	if len(old_record) != len(now_record) :
		old_record = now_record[:]
	del cache_request[:]
	safe_count = min(len(old_record), len(now_record), len(old_ios), len(new_ios))
	for i in range(0,safe_count):
		old = old_record[i]
		now = now_record[i]
		ios_old = old_ios[i]
		ios_new = new_ios[i]
		old = old.split()
		now = now.split()
		ios_old = ios_old.split()
		ios_new = ios_new.split()
		if len(old) < 14 or len(now) < 15 or len(ios_old) < 5 or len(ios_new) < 5:
			continue
		r_iops_old = long(ios_old[0])
		r_iops_new = long(ios_new[0])
		w_iops_old = long(ios_old[4])
		w_iops_new = long(ios_new[4])
		if (r_iops_new <= r_iops_old):
			r_iops = 0
		else:
			r_iops = r_iops_new - r_iops_old
			r_iops = r_iops / tick
		if (w_iops_new <= w_iops_old):
			w_iops = 0
		else:
			w_iops = w_iops_new - w_iops_old
			w_iops = w_iops / tick
		cache_name_old = old[0]
		cache_name_old = cache_name_old.split(":")
		cache_name_old = cache_name_old[0]
		cache_name_now = now[0]
		cache_name_now = cache_name_now.split(":")
		cache_name_now = cache_name_now[0]
		if cache_name_old != cache_name_now:
			continue
		hdd_name = cache_hdd_name(cache_name_now)
		read_hit_old = long(old[8])
		read_hit_now = long(now[8])
		if read_hit_now < read_hit_old:
			continue
		read_miss_old = long(old[9])
		read_miss_now = long(now[9])
		read_hit_rate = (read_hit_now * 100) / (read_hit_now + read_miss_now)
		write_hit_now = long(now[10])
		write_hit_old = long(old[10])	
		if write_hit_now < write_hit_old:
			continue
		write_miss_now = long(now[11])
		write_miss_old = long(old[11])
		write_hit_rate = (write_hit_now * 100) /(write_hit_now + write_miss_now)
		demote_new = long(now[12])
		promote_new = long(now[13])
		demote_old = long(old[12])
		promote_old = long(old[13])
		if(demote_new <= demote_old):
			demote = 0
		else:
			demote = demote_new - demote_old
			demote = demote / tick
		if(promote_new <= promote_old):	
			promote = 0
		else:
			promote = promote_new - promote_old
			promote = promote / tick
		used    = now[7]
		used    = used.split("/")
		total   = long(used[1])
		used    = long(used[0])
		dirtys  = long(now[14])
		used_rate    = used * 100 /total
		dirty_rate  = dirtys * 100 /total
		cache_request.append(cache_name_now)
		cache_request.append(cache_dm_name(cache_name_now))
		cache_request.append(hdd_name)
		cache_request.append(r_iops)
		cache_request.append(w_iops)
		cache_request.append(read_hit_rate)
		cache_request.append(write_hit_rate)
		cache_request.append(demote)
		cache_request.append(promote)
		cache_request.append(used_rate)
		cache_request.append(dirty_rate)
	return
def print_info():
	global cache_request
	global format
	global point
	nowTs = time.time()
	nowTs = int(nowTs - nowTs % 10)
	dev_count = len(cache_request) / 11
	for i in range(0,dev_count*8):
		ent = {}
		ent['tags'] = {}
		ent['name'] = format[i%8+ 3]
		ent['timestamp'] = nowTs
		ent['value'] = cache_request[i/8 * 11 +3 +i%8]
		ent['tags']['host'] = socket.gethostname()
		ent['tags']['dev'] 	= cache_request[i/8 * 11 +2]
		point.append(ent)
	data = json.dumps(point)
	print data
	return	
get_record()
make_request()
print_info()
