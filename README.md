# http-sys-bench
A HTTP Server API app to run performance tests. Written in C++.

Uses a server process and load test process to run testing. Typical output on my machine:

```
Test HTTP GET
Sending 160000 requests on 16 threads
..............................................................................................................................................................XXX.XXXXXXXXXXXXX
Completed 160000 requests in 7.95191 seconds
20121 requests per second
```
