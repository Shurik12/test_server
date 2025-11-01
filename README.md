# Simple server

### Description
Simple server implementatio, which contains two types: httplib server and multiplexing server/
It processes jsons and returns changed jsons. Collect statistics by the stream of data. 
Httplib Server has async mode for computing. For Multiplexing - not implemented.

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

### Load tests
```bash
python3 load_test.py --url http://localhost:8080 --test all
python3 load_test.py --test heavy
python3 load_test.py --test spike
python3 load_test.py --test numbers
```
