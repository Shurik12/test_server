#!/usr/bin/env python3
"""
Debugging Script for C++ JSON Service
Identifies specific endpoint issues and connection problems
"""

import requests
import json
import time
from concurrent.futures import ThreadPoolExecutor

class ServiceDebugger:
    def __init__(self, base_url: str):
        self.base_url = base_url.rstrip('/')
        self.session = requests.Session()
    
    def test_endpoint_detailed(self, endpoint: str, method: str = 'GET', payload: dict = None):
        """Test endpoint with detailed error reporting"""
        url = f"{self.base_url}{endpoint}"
        
        print(f"\nTesting {method} {endpoint}")
        print("-" * 40)
        
        try:
            start_time = time.time()
            
            if method.upper() == 'GET':
                response = self.session.get(url, timeout=10)
            else:
                response = self.session.post(url, json=payload, timeout=10)
            
            response_time = time.time() - start_time
            
            print(f"Status Code: {response.status_code}")
            print(f"Response Time: {response_time:.3f}s")
            print(f"Content Length: {len(response.content)} bytes")
            print(f"Headers: {dict(response.headers)}")
            
            if response.status_code == 200:
                try:
                    data = response.json()
                    print(f"JSON Response: {json.dumps(data, indent=2)}")
                except json.JSONDecodeError:
                    print(f"Raw Response (first 500 chars): {response.text[:500]}")
            else:
                print(f"Error Response: {response.text[:500]}")
                
        except requests.exceptions.Timeout:
            print("❌ TIMEOUT: Request took longer than 10 seconds")
        except requests.exceptions.ConnectionError as e:
            print(f"❌ CONNECTION ERROR: {e}")
        except Exception as e:
            print(f"❌ UNEXPECTED ERROR: {e}")
    
    def test_metrics_endpoint(self):
        """Specifically test metrics endpoint issues"""
        print("\n" + "="*50)
        print("DEBUGGING METRICS ENDPOINT")
        print("="*50)
        
        self.test_endpoint_detailed('/metrics')
        
        # Test with different accept headers
        headers = [
            {},
            {'Accept': 'text/plain'},
            {'Accept': 'application/json'},
            {'Accept': '*/*'}
        ]
        
        for header in headers:
            print(f"\nTesting with headers: {header}")
            try:
                response = self.session.get(f"{self.base_url}/metrics", headers=header, timeout=5)
                print(f"Status: {response.status_code}, Length: {len(response.content)}")
                if response.status_code == 200:
                    print("First 200 chars:", response.text[:200])
            except Exception as e:
                print(f"Error: {e}")
    
    def test_root_endpoint(self):
        """Test root endpoint issues"""
        print("\n" + "="*50)
        print("DEBUGGING ROOT ENDPOINT")
        print("="*50)
        
        self.test_endpoint_detailed('/')
    
    def test_connection_stability(self, num_requests: int = 50):
        """Test connection stability under quick succession"""
        print("\n" + "="*50)
        print("TESTING CONNECTION STABILITY")
        print("="*50)
        
        def make_request(i):
            try:
                start_time = time.time()
                response = self.session.get(f"{self.base_url}/health", timeout=5)
                response_time = time.time() - start_time
                return i, response.status_code, response_time, None
            except Exception as e:
                return i, 0, 0, str(e)
        
        with ThreadPoolExecutor(max_workers=20) as executor:
            results = list(executor.map(make_request, range(num_requests)))
        
        successes = [r for r in results if r[1] == 200]
        failures = [r for r in results if r[1] != 200 or r[3] is not None]
        
        print(f"Successful: {len(successes)}/{num_requests}")
        print(f"Failed: {len(failures)}/{num_requests}")
        
        if failures:
            print("\nFailure details:")
            for i, status, rt, error in failures[:10]:  # Show first 10 failures
                print(f"  Request {i}: Status={status}, Error={error}")
    
    def test_server_config(self):
        """Test server configuration and limits"""
        print("\n" + "="*50)
        print("TESTING SERVER CONFIGURATION")
        print("="*50)
        
        # Test with large payload
        large_payload = {
            "id": 1,
            "name": "X" * 10000,  # Large name
            "phone": "1234567890",
            "number": 999
        }
        
        print("Testing with large payload...")
        self.test_endpoint_detailed('/process', 'POST', large_payload)
        
        # Test with invalid JSON
        print("\nTesting with invalid JSON...")
        try:
            response = self.session.post(
                f"{self.base_url}/process", 
                data="invalid json", 
                headers={'Content-Type': 'application/json'},
                timeout=5
            )
            print(f"Status: {response.status_code}")
            print(f"Response: {response.text[:200]}")
        except Exception as e:
            print(f"Error: {e}")

def main():
    debugger = ServiceDebugger('http://localhost:8080')
    
    print("C++ JSON Service Debugger")
    print("=" * 50)
    
    # Run diagnostic tests
    debugger.test_metrics_endpoint()
    debugger.test_root_endpoint() 
    debugger.test_connection_stability()
    debugger.test_server_config()
    
    print("\n" + "="*50)
    print("QUICK HEALTH CHECK")
    print("="*50)
    
    # Final quick health check
    endpoints = [
        ('/health', 'GET'),
        ('/metrics', 'GET'), 
        ('/', 'GET'),
        ('/numbers/sum', 'GET'),
        ('/process', 'POST', {'id': 1, 'name': 'test', 'phone': '123', 'number': 42})
    ]
    
    for endpoint in endpoints:
        if len(endpoint) == 2:
            endpoint, method = endpoint
            payload = None
        else:
            endpoint, method, payload = endpoint
            
        debugger.test_endpoint_detailed(endpoint, method, payload)
        time.sleep(0.5)

if __name__ == "__main__":
    main()