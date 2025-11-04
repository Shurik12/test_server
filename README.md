# Simple server

### Description
Simple server implementatio, which contains two types: httplib server and multiplexing server/
It processes jsons and returns changed jsons. Collect statistics by the stream of data. 
Httplib Server has async mode for computing. For Multiplexing - not implemented.

Current implementation is already optimal for:
  1. Single server deployment
  2. Stateless request processing
  3. Simple number tracking
  4. No real-time broadcasting needed

### Build, run
Use commands from Makefile
```bash
# 1. Install needed dependencies
make install
# 2. Configure and build project
make configure
make build
# 3. Build and run containers
make docker-build
make docker-run
# 4. Stop containers
make docker-down
```

### C++ benchmarks and tests
```bash
./build/load_benchmark --benchmark_min_time=5s
```

### Python Load tests
```bash
python3 load_test.py --url http://localhost:8080 --test all
python3 load_test.py --test heavy
python3 load_test.py --test spike
python3 load_test.py --test numbers
```

### Linux statistics
```bash
# watch connections
watch -n 1 'netstat -an | grep :8080 | awk '\''{print $6}'\'' | sort | uniq -c'
# kill connections
sudo ss -t -K sport = 8080
```