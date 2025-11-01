# Simple server

### Description
Simple server implementatio, which contains two types: httplib server and multiplexing server/
It processes jsons and returns changed jsons. Collect statistics by the stream of data. 
Httplib Server has async mode for computing. For Multiplexing - not implemented.

### Build, run
Use commands form Makefile

### Load tests
```bash
python3 load_test.py --url http://localhost:8080 --test all
python3 load_test.py --test heavy
python3 load_test.py --test spike
python3 load_test.py --test numbers
```