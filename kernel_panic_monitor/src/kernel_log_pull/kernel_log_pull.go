package main

import (
	"bytes"
	"fmt"
	"github.com/Shopify/sarama"
	docopt "github.com/docopt/docopt-go"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
)

var (
	wg                   sync.WaitGroup
	logger               = log.New(os.Stderr, "[kernel-log]", log.LstdFlags)
	KERNEL_OFFSET_FILE   = "/tmp/kernel_log.offset"
	// 以下地址与 Kafka 凭证为脱敏占位符（YOUR_*），部署时替换为实际值
	KERNEL_SUBMIT_API    = "http://YOUR_API_HOST:8011/panic/crash/set"
	KERNEL_KAFKA_CLUSTER = "YOUR_KAFKA_BROKER1:9092,YOUR_KAFKA_BROKER2:9092"
	KERNEL_KAFKA_TOPIC   = "YOUR_KAFKA_TOPIC"
	KERNEL_KAFKA_KEY     = "crash_log"
	KAFKA_USER          = "YOUR_KAFKA_USER"
	KAFKA_PASSWORD      = "YOUR_KAFKA_PASSWORD"
)

type KernelOpt struct {
	Kafka      string `json:"kafka"`
	SubmitAPI  string `json:"submit_api"`
	OffsetFile string `json:"offset_file"`
	KafkaTopic string `json:"kafka_topic"`
	KafkaKey   string `json:"kafka_key"`
}

func check(e error) {
	if e != nil {
		panic(e)
	}
}

func checkFileIsExist(filename string) bool {
	if _, err := os.Stat(filename); os.IsNotExist(err) {
		return false
	}
	return true
}

func PostData(url string, s []byte) string {
	body := bytes.NewBuffer([]byte(s))
	res, err := http.Post(url, "application/json;charset=utf-8", body)
	if err != nil {
		return "Post error:" + err.Error()
	}
	result, err := ioutil.ReadAll(res.Body)
	res.Body.Close()
	if err != nil {
		return err.Error()
	}
	return string(result)
}

func getPreOffset(ko *KernelOpt) int64 {
	dat, err := ioutil.ReadFile(ko.OffsetFile)
	check(err)
	if err != nil {
		data, _ := strconv.Atoi(strings.Replace(string(dat), "\n", "", -1))
		logger.Printf("---pre-offset: %d------", data)
		return int64(data)
	} else {
		return sarama.OffsetNewest
	}

}

func saveOffset(offset int64, ko *KernelOpt) {
	fout, err := os.Create(ko.OffsetFile)
	defer fout.Close()
	if err == nil {
		fout.WriteString(fmt.Sprintf("%d", offset))
	}
}

func run(ko *KernelOpt) {
	sarama.Logger = logger
	config := sarama.NewConfig()
	config.Net.SASL.User = KAFKA_USER
	config.Net.SASL.Password = KAFKA_PASSWORD
	config.Net.SASL.Handshake = true
	config.Net.SASL.Enable = true
	consumer, err := sarama.NewConsumer(strings.Split(ko.Kafka, ","), config)
	if err != nil {
		logger.Println("Failed to start consumer: %s", err)
	}

	partitionList, err := consumer.Partitions(ko.KafkaTopic)
	if err != nil {
		logger.Println("Failed to get the list of partitions: ", err)
	}

	for partition := range partitionList {
		preOffset := sarama.OffsetNewest
		if preOffset := getPreOffset(ko); preOffset != sarama.OffsetNewest {
			preOffset += 1
		}

		pc, err := consumer.ConsumePartition(ko.KafkaTopic, int32(partition), preOffset)

		if err != nil {
			logger.Printf("Failed to start consumer for partition %d: %s\n", partition, err)
		}

		defer pc.AsyncClose()
		wg.Add(1)
		go func(sarama.PartitionConsumer) {
			defer wg.Done()
			for msg := range pc.Messages() {
				fmt.Printf("Partition:%d, Offset:%d, Key:%s, Value:%s", msg.Partition, msg.Offset, string(msg.Key), string(msg.Value))
				fmt.Println()
				if string(msg.Key) == ko.KafkaKey {
					fmt.Println(string(msg.Value))
					rs := PostData(ko.SubmitAPI, msg.Value)
					fmt.Println(string(rs))
					saveOffset(msg.Offset, ko)
				}
			}
		}(pc)
	}
	wg.Wait()
	consumer.Close()
}

func main() {
	usage := `usage: kernel_log_pull [--kafka=<kafka>] [--submit_api=<submit_api>] [--offset_file=<offset_file>] [--kafka_topic=<kafka_topic>] [--kafka_key=<kafka_key>]
options:
	--kafka=<kafka>               kafka cluster,like 127.0.0.1:9092,11.111.11.22:9092
	--kafka_topic=<kafka_topic>   kafka topic, like: YOUR_KAFKA_TOPIC
	--kafka_key=<kafka_key>       kafka topic key,like：crash_log
	--submit_api=<submit_api>     kernel log api
	--offset_file=<offset_file>   crash_path[default: /tmp/kernel_log.offset]
			`
	args, err := docopt.Parse(usage, nil, true, "kernel_log_pull 0.0.1", false)
	ko := &KernelOpt{}
	if s, ok := args["--kafka"].(string); ok && len(s) != 0 {
		ko.Kafka = s
	} else {
		ko.Kafka = KERNEL_KAFKA_CLUSTER
	}

	if s, ok := args["--submit_api"].(string); ok && len(s) != 0 {
		ko.SubmitAPI = s
	} else {
		ko.SubmitAPI = KERNEL_SUBMIT_API
	}

	if s, ok := args["--offset_file"].(string); ok && len(s) != 0 {
		ko.OffsetFile = s
	} else {
		ko.OffsetFile = KERNEL_OFFSET_FILE
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

	if err != nil {
		fmt.Println(err.Error())
	}
	run(ko)
}
