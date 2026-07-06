package main

import (
	"fmt"
	"runtime"
)

func main() {
	fmt.Printf("hello from %s on %s/%s\n", runtime.Version(), runtime.GOOS, runtime.GOARCH)
}
