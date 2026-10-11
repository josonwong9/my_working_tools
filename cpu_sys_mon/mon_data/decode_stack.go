package main

import (
	"bufio"
	"fmt"
	"math"
	"os"
	"regexp"
	"sort"
	"strconv"
	"strings"
)

const (
	KALLSYMS_PATH  = "./kallsyms"
	STACKINFO_PATH = "./stack.log"
	MAX_SYMS       = 300000
	MODULE_KERNEL  = "kernel"
	NOSYM_NAME     = "???"
)

var (
	symbolCnt     = 0
	defaultSymbol = Symbol{math.MaxUint64, NOSYM_NAME, ""}
	isInitialized = false
)

var allSymbols []Symbol

type Symbol struct {
	Addr   uint64
	Name   string
	Module string
	//Module string
}

func (s Symbol) String() string {
	return fmt.Sprintf("{Addr: %x Name: %s Module: %s}", s.Addr, s.Name, s.Module)
}

type symbolSort []Symbol

func (s symbolSort) Len() int {
	return symbolCnt
}

func (s symbolSort) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

func (s symbolSort) Less(i, j int) bool {
	return s[i].Addr < s[j].Addr
}

func loadKAllSymbols() error {
	f, err := os.Open(KALLSYMS_PATH)
	if err != nil {
		return err
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := scanner.Text()
		words := strings.FieldsFunc(line, func(c rune) bool {
			return c == ' ' || c == '\t'
		})

		// only Kernel
		if len(words) != 3 {
			continue
		}

		//if len(words) != 3 || len(words) != 4 {
		//continue
		//}

		//if words[1] != "T" && words[1] != "t" {
		//continue
		//}

		addr, err := strconv.ParseUint(words[0], 16, 64)
		if err != nil {
			fmt.Println(err)
			continue
		}

		if len(words) == 3 {
			allSymbols[symbolCnt] = Symbol{Addr: addr, Name: words[2], Module: MODULE_KERNEL}
		} else {
			allSymbols[symbolCnt] = Symbol{Addr: addr, Name: words[2], Module: words[3]}
		}
		symbolCnt++
	}

	// sort
	sort.Sort(symbolSort(allSymbols))

	return nil
}

func initKSymbol() {
	allSymbols = make([]Symbol, MAX_SYMS)

	// default
	allSymbols[0] = defaultSymbol
	symbolCnt++

	if err := loadKAllSymbols(); err != nil {
		fmt.Sprintf("failed to loadKAllSymbols")
	}

	fmt.Sprintf("Symbols: total %d\n", symbolCnt)
	isInitialized = true
}

func KSymbolSearch(key uint64) Symbol {
	if !isInitialized {
		initKSymbol()
	}

	i, j := 0, symbolCnt
	for i < j {
		h := int(uint(i+j) >> 1) // avoid overflow when computing h
		if allSymbols[h].Addr == key {
			i = h
			break
		} else if allSymbols[h].Addr < key {
			i = h + 1
		} else {
			j = h
		}
	}

	if allSymbols[i].Addr == key {
		return allSymbols[i]
	}
	if i == 0 {
		return allSymbols[0]
	}
	return allSymbols[i-1]
}

var stackinfo map[int64]string

func reverse(s []string) {
	for i, j := 0, len(s)-1; i < j; i, j = i+1, j-1 {
		s[i], s[j] = s[j], s[i]
	}
}

func GetStackInfo(fname string) error {
	f, err := os.Open(fname)
	if err != nil {
		return err
	}
	defer f.Close()

	var key int64

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := scanner.Text()

		is_key, _ := regexp.MatchString("^key", line)
		if is_key == true {
			scanner.Scan()
			line := scanner.Text()
			nums := strings.Fields(line)
			/* little endian reverse */
			reverse(nums)
			key, _ = strconv.ParseInt(strings.Join(nums, ""), 16, 64)
			fmt.Println("key=", key)
			continue
		}

		is_value, _ := regexp.MatchString("^value", line)
		if is_value == true {
			continue
		}

		is_zero, _ := regexp.MatchString("^00 00 00 00", line)
		if is_zero == true {
			continue
		}

		nums := strings.Fields(line)
		// first hex
		n1 := nums[0:8]
		reverse(n1)
		hex1, _ := strconv.ParseUint(strings.Join(n1, ""), 16, 64)
		sym1 := KSymbolSearch(hex1)
		fmt.Printf("%x%32s\t[%s]\n", sym1.Addr, sym1.Name, sym1.Module)

		// second hex
		n2 := nums[8:16]
		reverse(n2)
		hex2, _ := strconv.ParseUint(strings.Join(n2, ""), 16, 64)
		sym2 := KSymbolSearch(hex2)
		fmt.Printf("%x%32s\t[%s]\n", sym2.Addr, sym2.Name, sym2.Module)

	}

	return nil
}

func main() {
	stackinfo = make(map[int64]string)
	GetStackInfo(STACKINFO_PATH)
}
