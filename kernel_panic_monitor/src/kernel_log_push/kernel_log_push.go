package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"github.com/Shopify/sarama"
	"github.com/docopt/docopt-go"
	"io/ioutil"
	"log"
	"math/rand"
	"net"
	"net/http"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"time"
)

var (
	logger             = log.New(os.Stderr, "[kernel-log]", log.LstdFlags)
	CRASH_PATH         = "/var/crash"
	DURATION           = 87000.00
	SLEEP_TIME         = 600
	CRASH_FILE_NAME    = "vmcore-dmesg.txt"
	// 以下地址与 Kafka 凭证为脱敏占位符（YOUR_*），部署时替换为实际值
	KAFAKA_MachineList = "http://YOUR_ODIN_HOST/api/v1/ns/machine/list?ns=YOUR_KAFKA_NAMESPACE"
	KAFKA_CLUSTER      = "YOUR_KAFKA_BROKER1:9092,YOUR_KAFKA_BROKER2:9092"
	KAFKA_DialTimeout  = 3 * time.Second
	KERNEL_KAFKA_TOPIC = "YOUR_KAFKA_TOPIC"
	KERNEL_KAFKA_KEY   = "crash_log"
	KERNEL_VERSION     = "1.2.4"
	KPATCH_PATH        = "/tmp/kpatch"
	KAFKA_USER          = "YOUR_KAFKA_USER"
	KAFKA_PASSWORD      = "YOUR_KAFKA_PASSWORD"
)

type KernelOpt struct {
	Kafka      string  `json:"kafka"`
	Duration   float64 `json:"duration"`
	Sleep      int     `json:"sleep"`
	Crash_path string  `json:"crash_path"`
	Crash_name string  `json:"crash_name"`
	KafkaTopic string  `json:"kafka_topic"`
	KafkaKey   string  `json:"kafka_key"`
	Version    string  `json:"version"`
}

type KernelLog struct {
	CrashIP            string   `json:"crash_ip"`
	CrashHostName      string   `json:"crash_hostname"`
	CrashOS            string   `json:"crash_os"`
	CrashKernel        string   `json:"crash_kernel"`
	CrashLog           string   `json:"crash_log"`
	CrashTime          string   `json:"crash_time"`
	CrashCollectTime   string   `json:"crash_collect_time"`
	CrashVersion       string   `json:"crash_version"`
	CrashKpatch        []string `json:"crash_kpatch"`
	CrashKpatchVersion string   `json:"crash_kpatch_version"`
}

type KafkaMachines []struct {
	ID             int    `json:"id"`
	Name           string `json:"name"`
	IP             string `json:"ip"`
	Mgroup         string `json:"mgroup"`
	State          string `json:"state"`
	ApplyID        int    `json:"applyId"`
	UplineID       int    `json:"uplineId"`
	Msg            string `json:"msg"`
	LastModifyUser string `json:"lastModifyUser"`
	LastTime       string `json:"lastTime"`
}

func ExecCmd(cmdstr string, timeOut int) (err error, stdout bytes.Buffer, stderr bytes.Buffer) {
	timeout := make(chan bool, 1)
	go func() {
		time.Sleep(time.Duration(timeOut) * time.Second)
		timeout <- true
	}()

	execDown := make(chan bool, 1)
	cmd := exec.Command("sh", "-c", cmdstr)
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	err = cmd.Run()
	execDown <- true
	select {
	case <-execDown:
		return
	case <-timeout:
		if killerr := cmd.Process.Kill(); killerr != nil {
			err = fmt.Errorf("EXEC CMD TIMEOUT:%d  %s\nKILL CMD ERROR:%s", timeOut, cmdstr, killerr.Error())
		} else {
			err = fmt.Errorf("EXEC CMD TIMEOUT:%d  %s", timeOut, cmdstr)
		}
		return
	}
}

func getHostName() string {
	err, stdout, _ := ExecCmd("hostname", 6)
	if err != nil {
		return "unkown"
	} else {
		return stdout.String()
	}
}

func getIP() (string, error) {
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return "", err
	} else {
		for _, address := range addrs {
			if ipnet, ok := address.(*net.IPNet); ok && !ipnet.IP.IsLoopback() {
				if ipnet.IP.To4() != nil {
					if strings.HasPrefix(ipnet.IP.String(), "10.") || strings.HasPrefix(ipnet.IP.String(), "100.") {
						return ipnet.IP.String(), nil
					}
				}
			}
		}
	}
	return "", errors.New("mis_match")
}

func getOsVersion() string {
	v, err := ioutil.ReadFile("/etc/redhat-release")
	if err != nil {
		return "unknow"
	}
	return string(v)
}

func getKernelVersion() string {
	err, stdout, _ := ExecCmd("uname -r", 6)
	if err != nil {
		return "error"
	} else {
		return stdout.String()
	}
}

func getPatchVersion() string {
	kernel := getKernelVersion()
	var err error
	var stdout bytes.Buffer
	if strings.HasPrefix(kernel, "3.10") {
		err, stdout, _ = ExecCmd("rpm -qa | grep YOUR_KPATCH_RPM", 6)
	} else if strings.HasPrefix(kernel, "2.6") {
		err, stdout, _ = ExecCmd("rpm -qa | grep YOUR_HOTFIX_RPM", 6)
	} else {
		return ""
	}
	if err != nil {
		return err.Error()
	}
	return stdout.String()
}

func getCrashTime(filename string) string {
	if f, err := os.Stat(filename); err == nil {
		return f.ModTime().Format("2006-01-02 15:04:05")
	}
	return time.Now().Format("2006-01-02 15:04:05")
}

func getCrashLog(filename string) string {
	f, _ := os.Stat(filename)
	data, err := ioutil.ReadFile(filename)
	if err != nil {
		return ""
	}
	if f.Size() > 5*1024*1024-1024*20 {
		fmt.Println("file size is", f.Size())
		data = data[len(data)-5*1024*1024+1024*1024*2:]
		fmt.Println(len(data))
	}
	return string(data)
}

func checkFileIsExist(filename string) bool {
	if _, err := os.Stat(filename); os.IsNotExist(err) {
		return false
	}
	return true
}

func isRunning() bool {
	err, stdout, _ := ExecCmd("ps aux | grep kernel_log_push |grep -v grep", 6)
	c, _ := strconv.Atoi(strings.Replace(strings.Replace(stdout.String(), "\n", "", -1), " ", "", -1))
	if err != nil || c > 1 {
		return true
	}
	return false
}

func ListDir(dirPth string, filename string, duration float64) (files []string, err error) {
	files = make([]string, 0, 10)
	dir, err := ioutil.ReadDir(dirPth)
	if err != nil {
		return nil, err
	}

	PthSep := string(os.PathSeparator)
	for _, fi := range dir {
		if fi.IsDir() {
			tmpFileName := dirPth + PthSep + fi.Name() + PthSep + filename
			logger.Println("all crash list:", tmpFileName)
			if checkFileIsExist(tmpFileName) && time.Now().Sub(fi.ModTime()).Seconds() <= duration {
				files = append(files, tmpFileName)
			}
		}
	}
	logger.Println("files result: ", files)
	return files, nil
}

func getKpatchList(dirPth string) (kpatchs []string, err error) {
	kpatchs = make([]string, 0, 10)
	dir, err := ioutil.ReadDir(dirPth)
	if err != nil {
		return nil, err
	}

	for _, fi := range dir {
		if fi.IsDir() {
			kpatchs = append(kpatchs, fi.Name())
		}
	}
	return kpatchs, nil
}

func isFirstRun() bool {
	filename := "/tmp/kernel_log_push.flag"
	rs := false
	if _, err := os.Stat(filename); os.IsNotExist(err) {
		rs = true
	}
	fout, _ := os.Create(filename)
	defer fout.Close()
	return rs
}

func pushDataToKafka(json_data string, ko *KernelOpt) {
	var producer sarama.SyncProducer
	sarama.Logger = logger
	config := sarama.NewConfig()
	config.Version = sarama.V0_10_2_0
	config.Producer.Compression = sarama.CompressionSnappy
	config.Producer.RequiredAcks = sarama.WaitForAll
	config.Producer.Return.Successes = true
	config.Producer.Partitioner = sarama.NewRandomPartitioner
	config.Producer.Return.Successes = true
	config.Producer.MaxMessageBytes = 5 * 1024 * 1024
	config.Net.DialTimeout = KAFKA_DialTimeout
	config.Net.SASL.User = KAFKA_USER
	config.Net.SASL.Password = KAFKA_PASSWORD
	config.Net.SASL.Handshake = true
	config.Net.SASL.Enable = true
	msg := &sarama.ProducerMessage{}
	msg.Topic = ko.KafkaTopic
	msg.Partition = int32(-1)
	msg.Key = sarama.StringEncoder(ko.KafkaKey)
	msg.Value = sarama.ByteEncoder(json_data)
	producer, err := sarama.NewSyncProducer(strings.Split(ko.Kafka, ","), config)

	if err != nil {
		logger.Println("Failed to produce message: %s", err)
		list, err := PullKafkaMachine()
		if err != nil {
			fmt.Println("Failed to produce message: %s", err)
			os.Exit(500)
		}
		producer, err = sarama.NewSyncProducer(list, config)
		if err != nil {
			fmt.Println("Failed to produce message: %s", err)
			os.Exit(500)
		}
	}

	defer producer.Close()
	partition, offset, err := producer.SendMessage(msg)
	if err != nil {
		logger.Println("Failed to produce message: ", err)
	}
	logger.Printf("partition=%d, offset=%d\n", partition, offset)
}

func PullKafkaMachine() ([]string, error) {
	retryTimes := 3
	var list []string
	var err error
	var machineList KafkaMachines
	client := &http.Client{
		Timeout: 3 * time.Second,
	}
	for i := retryTimes; i > 0; i-- {
		req, err := http.NewRequest("GET", KAFAKA_MachineList, nil)
		if err != nil {
			continue
		}
		resp, err := client.Do(req)
		if err != nil {
			continue
		}
		body, err := ioutil.ReadAll(resp.Body)
		if err != nil {
			continue
		}
		resp.Body.Close()
		if err = json.Unmarshal(body, &machineList); err != nil {
			continue
		}
		break
	}
	for _, x := range machineList {
		list = append(list, x.IP+":9092")
	}
	return list, err
}

func OutPutJson(str string) {
	var out bytes.Buffer
	err := json.Indent(&out, []byte(str), "", "  ")
	if err != nil {
		fmt.Println(str)
	} else {
		out.WriteTo(os.Stdout)
		fmt.Println("\n")
	}
}

func run(ko *KernelOpt) {
	// 避免重复运行
	if isRunning() {
		logger.Println("is running? Yes, exit!")
		os.Exit(1)
	} else {
		logger.Println("is running? No, continue!")
	}

	// 系统基本信息
	crash_ip, _ := getIP()
	crash_os := getOsVersion()
	crash_kernel := getKernelVersion()
	crash_hostname := getHostName()
	crash_patch_version := getPatchVersion()
	crash_time, crash_log := time.Now().Format("2006-01-02 15:04:05"), ""

	fmt.Println(ko)
	// 获取crash日志信息
	files, _ := ListDir(ko.Crash_path, ko.Crash_name, ko.Duration)
	data := &KernelLog{}
	data.CrashIP = strings.Replace(crash_ip, "\n", "", -1)
	data.CrashKernel = strings.Replace(crash_kernel, "\n", "", -1)
	data.CrashOS = strings.Replace(crash_os, "\n", "", -1)
	data.CrashHostName = strings.Replace(crash_hostname, "\n", "", -1)
	data.CrashTime = crash_time
	data.CrashCollectTime = crash_time
	data.CrashLog = crash_log
	data.CrashVersion = ko.Version
	kpatchs, _ := getKpatchList(KPATCH_PATH)
	data.CrashKpatch = kpatchs
	data.CrashKpatchVersion = strings.Replace(crash_patch_version, "\n", "", -1)
	if len(files) > 0 {
		for _, filename := range files {
			logger.Println("current panic:", filename)
			data.CrashTime = getCrashTime(filename)
			data.CrashLog = getCrashLog(filename)
			if d, err := json.Marshal(data); err == nil {
				// OutPutJson(fmt.Sprintf("%s", d))
				fmt.Println("data length:", len(string(d)))
				pushDataToKafka(fmt.Sprintf("%s", d), ko)
			}
		}
	} else {
		// tmp
		if d, err := json.Marshal(data); err == nil {
			// OutPutJson(fmt.Sprintf("%s", d))
			pushDataToKafka(fmt.Sprintf("%s", d), ko)
		}
	}
}

func main() {
	usage := `usage: kernel_log_push [--kafka=<kafka>] [--duration=<duration>] [--crash_path=<crash_path>] [--crash_name=<crash_name>] [--kafka_topic=<kafka_topic>] [--kafka_key=<kafka_key>] [--sleep=<sleep>] [--version=<version>]
options:
	--kafka=<kafka>             kafka cluster,like 127.0.0.1:9092,11.111.11.22:9092
	--kafka_topic=<kafka_topic> kafka topic, like: YOUR_KAFKA_TOPIC
	--kafka_key=<kafka_key>     kafka topic key,like：crash_log
	--duration=<duration>       kernel log duration[default: 87000.00]
	--sleep=<sleep>       		sleep rand(600)[default: 600]
	--crash_path=<crash_path>   crash_path[default: /var/crash]
	--crash_name=<crash_name>   crash file name[default: vmcore-dmesg.txt]
	--version=<version>   		set kernel monitor version
			`
	args, err := docopt.Parse(usage, nil, true, "kernel_log_push 0.0.1", false)
	ko := &KernelOpt{}
	if s, ok := args["--kafka"].(string); ok && len(s) != 0 {
		ko.Kafka = s
	} else {
		ko.Kafka = KAFKA_CLUSTER
	}

	if s, ok := args["--duration"].(string); ok {
		log.Println("duration:", ok, s)
		dura, err := strconv.ParseFloat(s, 64)
		if err != nil {
			ko.Duration = DURATION
		}
		ko.Duration = dura
	} else {
		log.Println("duration:", ok, s)
		ko.Duration = DURATION
	}

	if isFirstRun() {
		ko.Duration = 63072000.00
	}

	if s, ok := args["--sleep"].(string); ok && len(s) != 0 {
		ko.Sleep, _ = strconv.Atoi(s)
	} else {
		ko.Sleep = SLEEP_TIME
	}

	if s, ok := args["--crash_path"].(string); ok && len(s) != 0 {
		ko.Crash_path = CRASH_PATH
	}

	if s, ok := args["--crash_name"].(string); ok && len(s) != 0 {
		ko.Crash_name = s
	} else {
		ko.Crash_name = CRASH_FILE_NAME
	}

	if s, ok := args["--kafka_topic"].(string); ok && len(s) != 0 {
		ko.KafkaTopic = s
	} else {
		ko.KafkaTopic = KERNEL_KAFKA_TOPIC
	}

	if s, ok := args["--kafka_key"].(string); ok && len(s) != 0 {
		ko.KafkaKey = s
	} else {
		ko.KafkaKey = KERNEL_KAFKA_KEY
	}

	if s, ok := args["--version"].(string); ok && len(s) != 0 {
		ko.Version = s
	} else {
		ko.Version = KERNEL_VERSION
	}
	if err != nil {
		fmt.Println(err.Error())
	}

	rand.Seed(time.Now().UnixNano())
	i := rand.Intn(ko.Sleep)
	logger.Printf("sleep %d s\n", i)
	time.Sleep(time.Duration(i) * time.Second)

	run(ko)
}
