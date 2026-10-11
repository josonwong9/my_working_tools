//go:build linux
// +build linux

// This program demonstrates attaching an eBPF program to a kernel symbol.
// The eBPF program will be attached to the start of the sys_execve
// kernel function and prints out the number of times it has been called
// every second.
package main

import "C"

import (
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"os/signal"
	"path"
	"strings"
	"syscall"
	"time"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/link"
	"golang.org/x/sys/unix"
)

//go:generate sh -c "bpftool btf dump file /sys/kernel/btf/vmlinux format c > ./headers/vmlinux.h"
//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -cc clang CpuSys ./bpf/cpu_sys.c -- -I./headers -D__TARGET_ARCH_x86

func mount_bpffs() (string, error) {
	const defpath = "/sys/fs/bpf"
	const sched_bpfdir = "debug"
	const mount_type = "bpf"

	//check path mount state
	cmd := exec.Command("mount", "-l", "-t", mount_type)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		fmt.Println("StdoutPipe: " + err.Error())
		return "", err
	}

	// exec cmd
	if err := cmd.Start(); err != nil {
		fmt.Println("Start: ", err.Error())
		return "", err
	}

	//read stdout buffer
	bytes, err := ioutil.ReadAll(stdout)
	if err != nil {
		fmt.Println("ReadAll stdout: ", err.Error())
		return "", err
	}

	// wait for cmd stop
	if err := cmd.Wait(); err != nil {
		fmt.Println("Wait: ", err.Error())
		return "", err
	}

	fmt.Printf("ReadAll bytes: %s\n", string(bytes))

	// find type string in bytes buffer
	exist := strings.Contains(string(bytes), mount_type)
	if exist == true {
		fmt.Printf("%s Contains in the bytes string\n", mount_type)

		//eg: tempfs on /sys/fs/bpf type bpf (rw,relatime) ,get /sys/fs/bpf
		split_str := strings.Split(string(bytes), " ")

		return split_str[2], nil
	}

	// Not exist, start mount
	cmd = exec.Command("mount", "-t", mount_type, defpath, defpath)
	if err := cmd.Start(); err != nil {
		fmt.Println("Start: ", err.Error())
		return "", err
	}

	if err = cmd.Wait(); err != nil {
		fmt.Println("Wait: ", err.Error())
		return "", err
	}

	pinPath := path.Join(defpath, sched_bpfdir)
	if err := os.MkdirAll(pinPath, os.ModePerm); err != nil {
		log.Fatalf("failed to create bpf fs subpath: %+v", err)
	}

	return pinPath, nil
}

func main() {
	// Name of the kernel function to trace.
	//fn1 := "generic_exec_single"
	//fn2 := "flush_smp_call_function_queue"
	fn1 := "account_system_index_time"

	// Subscribe to signals for terminating the program.
	stopper := make(chan os.Signal, 1)
	signal.Notify(stopper, os.Interrupt, syscall.SIGTERM)

	// Increase the rlimit of the current process to provide sufficient space
	// for locking memory for the eBPF map.
	if err := unix.Setrlimit(unix.RLIMIT_MEMLOCK, &unix.Rlimit{
		Cur: unix.RLIM_INFINITY,
		Max: unix.RLIM_INFINITY,
	}); err != nil {
		log.Fatalf("failed to set temporary rlimit: %v", err)
	}

	pinPath, err := mount_bpffs()
	if err != nil {
		log.Fatalf("failed to mount bpffs %v", err)
	}

	// Load pre-compiled programs and maps into the kernel.
	objs := CpuSysObjects{}
	if err := LoadCpuSysObjects(&objs, &ebpf.CollectionOptions{
		Maps: ebpf.MapOptions{
			// Pin the map to the BPF filesystem and configure the
			// library to automatically re-write it in the BPF
			// program so it can be re-used if it already exists or
			// create it if not
			PinPath: pinPath,
		},
	}); err != nil {
		log.Fatalf("loading objects: %v", err)
	}
	defer objs.Close()

	// kp1, err := link.Kprobe(fn1, objs.GenericExecSingleEntry)
	// if err != nil {
	// 	log.Fatalf("opening kprobe: %s", err)
	// }
	// defer kp1.Close()

	// kp2, err := link.Kprobe(fn2, objs.FlushSmpCallFunctionQueueEntry)
	// if err != nil {
	// 	log.Fatalf("opening kprobe: %s", err)
	// }
	// defer kp2.Close()

	kp1, err := link.Kprobe(fn1, objs.AccountSystemIndexTimeEntry)
	if err != nil {
		log.Fatalf("opening kprobe: %s", err)
	}
	defer kp1.Close()

	// tp1, err := link.Tracepoint("irq_vectors", "reschedule_entry", objs.RescheduleEntryEntry)
	// if err != nil {
	// 	log.Fatalf("opening tracepoint: %s", err)
	// }
	// defer tp1.Close()

	// Read loop reporting the total amount of times the kernel
	// function was entered, once per second.
	ticker := time.NewTicker(1 * time.Second)

	log.Println("Waiting for events..")

	for {
		select {
		case <-ticker.C:
		case <-stopper:
			return
		}
	}
}
