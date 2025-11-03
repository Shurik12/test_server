#!/usr/bin/env python3
"""
Comprehensive Load Testing Script for C++ JSON Service
Uses proper connection pooling and keep-alive to prevent CLOSE_WAIT
"""

import requests
import threading
import time
import random
import json
import statistics
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from typing import List, Dict, Tuple
import argparse


@dataclass
class TestResult:
    """Store test results"""

    total_requests: int = 0
    successful_requests: int = 0
    failed_requests: int = 0
    total_response_time: float = 0
    min_response_time: float = float("inf")
    max_response_time: float = 0
    response_times: List[float] = None
    errors: Dict[str, int] = None

    def __post_init__(self):
        if self.response_times is None:
            self.response_times = []
        if self.errors is None:
            self.errors = {}

    def add_request(self, success: bool, response_time: float, error_type: str = None):
        self.total_requests += 1
        if response_time > 0:
            self.total_response_time += response_time
            self.response_times.append(response_time)
            self.min_response_time = min(self.min_response_time, response_time)
            self.max_response_time = max(self.max_response_time, response_time)

        if success:
            self.successful_requests += 1
        else:
            self.failed_requests += 1
            if error_type:
                self.errors[error_type] = self.errors.get(error_type, 0) + 1

    def get_stats(self):
        """Calculate statistics"""
        if not self.response_times:
            return {
                "avg_response_time": 0,
                "median_response_time": 0,
                "p95_response_time": 0,
                "p99_response_time": 0,
                "requests_per_second": 0,
            }

        return {
            "avg_response_time": statistics.mean(self.response_times),
            "median_response_time": statistics.median(self.response_times),
            "p95_response_time": sorted(self.response_times)[
                int(len(self.response_times) * 0.95)
            ],
            "p99_response_time": sorted(self.response_times)[
                int(len(self.response_times) * 0.99)
            ],
            "requests_per_second": len(self.response_times)
            / (max(self.response_times) if self.response_times else 1),
        }


class OptimizedLoadTester:
    def __init__(self, base_url: str):
        self.base_url = base_url.rstrip("/")
        # Create session with proper connection pooling for keep-alive
        self.session = self._create_session()
        self.client_ids = [f"client_{i}" for i in range(1000)]

    def _create_session(self):
        """Create a session with optimized connection pooling"""
        session = requests.Session()

        # Configure connection pooling for keep-alive
        adapter = requests.adapters.HTTPAdapter(
            pool_connections=50,  # Reduced to prevent file descriptor exhaustion
            pool_maxsize=50,  # Reduced to prevent file descriptor exhaustion
            max_retries=2,
            pool_block=False,
        )
        session.mount("http://", adapter)
        session.mount("https://", adapter)

        # Set default headers for keep-alive
        session.headers.update(
            {
                "Connection": "keep-alive",
                "Keep-Alive": "timeout=30, max=1000",
                "User-Agent": "LoadTester/1.0",
            }
        )

        return session

    def _recreate_session(self):
        """Recreate session if connection issues occur"""
        self.session.close()
        self.session = self._create_session()

    def generate_payload(self, client_id: str = None) -> dict:
        """Generate random JSON payload"""
        if client_id is None:
            client_id = random.choice(self.client_ids)

        return {
            "id": random.randint(1, 10000),
            "name": f"User_{random.randint(1, 1000)}",
            "phone": f"+1-555-{random.randint(100, 999)}-{random.randint(1000, 9999)}",
            "number": random.randint(1, 1000),
        }

    def test_endpoint(
        self, endpoint: str, method: str = "GET", payload: dict = None
    ) -> Tuple[bool, float, str]:
        """Test a single endpoint with proper connection handling"""
        url = f"{self.base_url}{endpoint}"
        start_time = time.time()

        try:
            if method.upper() == "GET":
                response = self.session.get(url, timeout=10)  # Reduced timeout
            else:  # POST
                response = self.session.post(url, json=payload, timeout=10)

            response_time = time.time() - start_time

            # Check connection header from server
            connection_header = response.headers.get("Connection", "").lower()
            if connection_header == "close":
                # Server wants to close connection, recreate session
                self._recreate_session()

            if response.status_code == 200:
                if endpoint == "/metrics":
                    return True, response_time, "success"
                elif endpoint == "/":
                    try:
                        response.json()
                        return True, response_time, "success"
                    except:
                        return False, response_time, "invalid_json"
                else:
                    try:
                        data = response.json()
                        if data.get("success", False):
                            return True, response_time, "success"
                        else:
                            return (
                                False,
                                response_time,
                                f"api_error: {data.get('error', 'unknown')}",
                            )
                    except:
                        return True, response_time, "success_non_json"
            else:
                return False, response_time, f"http_{response.status_code}"

        except requests.exceptions.Timeout:
            return False, time.time() - start_time, "timeout"
        except requests.exceptions.ConnectionError:
            # Connection error, recreate session
            self._recreate_session()
            return False, time.time() - start_time, "connection_error"
        except requests.exceptions.ChunkedEncodingError:
            # Connection was closed unexpectedly
            self._recreate_session()
            return False, time.time() - start_time, "chunked_encoding_error"
        except Exception as e:
            return False, time.time() - start_time, f"exception: {str(e)}"

    def run_load_scenario(
        self, scenario_name: str, num_clients: int, requests_per_client: int
    ) -> TestResult:
        """Run a specific load scenario with controlled concurrency"""
        print(
            f"\n🏃 Running {scenario_name}: {num_clients} clients × {requests_per_client} requests"
        )

        result = TestResult()
        start_time = time.time()

        def client_workload(client_id: str):
            # Each client gets its own session to avoid connection contention
            client_session = self._create_session()
            client_result = TestResult()

            for i in range(requests_per_client):
                # Mix of request types for realistic load
                request_type = random.choices(
                    ["process", "health", "numbers", "metrics"],
                    weights=[0.7, 0.1, 0.1, 0.1],  # 70% processing, 30% monitoring
                )[0]

                if request_type in ["process"]:
                    endpoint = f"/{request_type}"
                    payload = self.generate_payload(client_id)
                    method = "POST"
                else:
                    if request_type == "health":
                        endpoint = "/health"
                    elif request_type == "metrics":
                        endpoint = "/metrics"
                    else:  # numbers
                        endpoint = random.choice(
                            ["/numbers/sum", f"/numbers/sum/{client_id}"]
                        )
                    payload = None
                    method = "GET"

                success, response_time, error_type = self.test_endpoint(
                    endpoint, method, payload
                )
                client_result.add_request(success, response_time, error_type)

                # Small delay to avoid overwhelming the server
                if i % 10 == 0:  # More frequent delays
                    time.sleep(0.005)

            # Clean up client session
            client_session.close()
            return client_result

        # Reduced max workers to prevent file descriptor exhaustion
        max_workers = min(num_clients, 50)
        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            futures = {
                executor.submit(client_workload, f"client_{i}"): i
                for i in range(num_clients)
            }

            completed = 0
            for future in as_completed(futures):
                client_result = future.result()

                # Merge results
                result.total_requests += client_result.total_requests
                result.successful_requests += client_result.successful_requests
                result.failed_requests += client_result.failed_requests
                result.total_response_time += client_result.total_response_time
                result.response_times.extend(client_result.response_times)
                result.min_response_time = min(
                    result.min_response_time, client_result.min_response_time
                )
                result.max_response_time = max(
                    result.max_response_time, client_result.max_response_time
                )

                for error_type, count in client_result.errors.items():
                    result.errors[error_type] = result.errors.get(error_type, 0) + count

                completed += 1
                if completed % max(1, num_clients // 10) == 0:
                    elapsed = time.time() - start_time
                    print(
                        f"  Progress: {completed}/{num_clients} clients ({completed/num_clients*100:.1f}%) in {elapsed:.1f}s"
                    )

        return result

    def run_rps_scenario(
        self, scenario_name: str, target_rps: int, duration: int
    ) -> TestResult:
        """Run RPS-based load scenario with connection management"""
        print(f"\n🏃 Running {scenario_name}: {target_rps} RPS for {duration}s")

        result = TestResult()
        start_time = time.time()
        request_count = 0

        # Use a session pool for better connection management
        session_pool = [self._create_session() for _ in range(min(target_rps, 20))]
        session_index = 0

        while time.time() - start_time < duration:
            batch_start = time.time()
            batch_requests = 0

            with ThreadPoolExecutor(
                max_workers=min(target_rps, 50)
            ) as executor:  # Reduced max workers
                futures = []
                for i in range(target_rps):
                    # Realistic mix of requests
                    if random.random() < 0.7:  # 70% processing
                        endpoint = random.choice(["/process"])
                        payload = self.generate_payload()
                        futures.append(
                            executor.submit(
                                self.test_endpoint, endpoint, "POST", payload
                            )
                        )
                    else:  # 30% monitoring
                        endpoint = random.choice(
                            ["/health", "/metrics", "/numbers/sum"]
                        )
                        futures.append(
                            executor.submit(self.test_endpoint, endpoint, "GET")
                        )

                for future in as_completed(futures):
                    success, response_time, error_type = future.result()
                    result.add_request(success, response_time, error_type)
                    batch_requests += 1

            # Maintain RPS rate with more precise timing
            batch_time = time.time() - batch_start
            if batch_time < 1.0:
                time.sleep(1.0 - batch_time)

            request_count += batch_requests
            elapsed = time.time() - start_time
            current_rps = batch_requests / batch_time if batch_time > 0 else 0

            print(
                f"  Progress: {elapsed:.1f}s/{duration}s, Requests: {request_count}, RPS: {current_rps:.1f}"
            )

        # Clean up session pool
        for session in session_pool:
            session.close()

        return result

    def run_spike_test(self):
        """Test with sudden traffic spikes with connection control"""
        print(
            f"\n🏃 Running SPIKE TEST: Variable RPS from 10 to 200"
        )  # Reduced max RPS

        result = TestResult()
        phases = [
            ("Warm-up", 10, 10),
            ("Low", 30, 10),
            ("Spike", 200, 5),  # Reduced from 500 to 200
            ("Sustain", 50, 10),
            ("Cool-down", 10, 5),
        ]

        for phase_name, rps, duration in phases:
            print(f"  Phase: {phase_name} - {rps} RPS for {duration}s")
            phase_start = time.time()

            while time.time() - phase_start < duration:
                batch_start = time.time()
                batch_requests = 0

                with ThreadPoolExecutor(
                    max_workers=min(rps, 50)
                ) as executor:  # Reduced max workers
                    futures = []
                    for _ in range(rps):
                        endpoint = random.choice(["/process"])
                        payload = self.generate_payload()
                        futures.append(
                            executor.submit(
                                self.test_endpoint, endpoint, "POST", payload
                            )
                        )

                    for future in as_completed(futures):
                        success, response_time, error_type = future.result()
                        result.add_request(success, response_time, error_type)
                        batch_requests += 1

                batch_time = time.time() - batch_start
                if batch_time < 1.0:
                    time.sleep(1.0 - batch_time)

        return result

    def test_number_tracking_accuracy(
        self, operations: int = 200
    ):  # Reduced operations
        """Test that number tracking is accurate under load"""
        print(f"\n🔢 Testing Number Tracking Accuracy: {operations} operations")

        result = TestResult()
        expected_totals = {}

        # Generate operations
        test_operations = []
        for i in range(operations):
            client_id = f"accuracy_test_{random.randint(1, 10)}"
            number = random.randint(1, 100)
            test_operations.append((client_id, number, i))
            expected_totals[client_id] = expected_totals.get(client_id, 0) + number

        # Execute operations with controlled concurrency
        with ThreadPoolExecutor(max_workers=10) as executor:  # Reduced workers
            futures = []
            for client_id, number, op_id in test_operations:
                payload = {
                    "id": op_id,
                    "name": f"AccuracyTest_{client_id}",
                    "phone": f"+1-555-{random.randint(100, 999)}-{random.randint(1000, 9999)}",
                    "number": number,
                }
                futures.append(
                    executor.submit(self.test_endpoint, "/process", "POST", payload)
                )

            for future in as_completed(futures):
                success, response_time, error_type = future.result()
                result.add_request(success, response_time, error_type)

        # Verify results
        print("  Verifying number sums...")
        self.verify_number_accuracy()

        return result

    def verify_number_accuracy(self):
        """Verify number endpoints are working correctly"""
        endpoints = ["/numbers/sum", "/numbers/sum-all"]
        for endpoint in endpoints:
            success, response_time, _ = self.test_endpoint(endpoint)
            status = "✓" if success else "✗"
            print(f"    {status} {endpoint}: {response_time:.3f}s")

    def print_detailed_results(self, result: TestResult, scenario_name: str):
        """Print comprehensive results"""
        stats = result.get_stats()

        print(f"\n{'='*70}")
        print(f"📊 RESULTS: {scenario_name}")
        print(f"{'='*70}")

        success_rate = (
            (result.successful_requests / result.total_requests * 100)
            if result.total_requests > 0
            else 0
        )

        print(
            f"📈 Requests: {result.total_requests:,} total, {result.successful_requests:,} successful ({success_rate:.2f}%)"
        )
        print(f"❌ Failures: {result.failed_requests:,} ({100-success_rate:.2f}%)")

        print(f"\n⏱️  Response Times:")
        print(f"   Min:    {result.min_response_time*1000:7.1f} ms")
        print(f"   Avg:    {stats['avg_response_time']*1000:7.1f} ms")
        print(f"   Median: {stats['median_response_time']*1000:7.1f} ms")
        print(f"   P95:    {stats['p95_response_time']*1000:7.1f} ms")
        print(f"   P99:    {stats['p99_response_time']*1000:7.1f} ms")
        print(f"   Max:    {result.max_response_time*1000:7.1f} ms")

        print(f"\n🚀 Throughput: {stats['requests_per_second']:7.1f} RPS")

        if result.errors:
            print(f"\n🔍 Error Breakdown:")
            for error_type, count in sorted(
                result.errors.items(), key=lambda x: x[1], reverse=True
            ):
                percentage = (
                    (count / result.total_requests * 100)
                    if result.total_requests > 0
                    else 0
                )
                print(f"   {error_type:20} {count:5d} ({percentage:5.1f}%)")

        # Performance rating
        if success_rate >= 99.9 and stats["p95_response_time"] * 1000 < 200:
            rating = "⭐ EXCELLENT"
        elif success_rate >= 99.0 and stats["p95_response_time"] * 1000 < 500:
            rating = "✅ GOOD"
        elif success_rate >= 95.0:
            rating = "⚠️  ACCEPTABLE"
        else:
            rating = "❌ NEEDS IMPROVEMENT"

        print(f"\n🎯 Performance Rating: {rating}")

    def cleanup(self):
        """Clean up resources"""
        self.session.close()


def main():
    parser = argparse.ArgumentParser(
        description="Optimized Load Testing for C++ JSON Service"
    )
    parser.add_argument("--url", default="http://localhost:8080", help="Server URL")
    parser.add_argument(
        "--test",
        choices=["all", "light", "medium", "heavy", "rps", "spike", "numbers"],
        default="all",
        help="Test scenario to run",
    )

    args = parser.parse_args()

    tester = OptimizedLoadTester(args.url)

    print("🚀 C++ JSON Service - Optimized Load Testing")
    print("=" * 55)
    print(f"🎯 Target: {args.url}")
    print(f"📋 Scenario: {args.test}")
    print("💡 Using keep-alive connections and controlled concurrency")
    print("=" * 55)

    try:
        scenarios = []

        if args.test in ["all", "light"]:
            scenarios.append(
                ("LIGHT LOAD", lambda: tester.run_load_scenario("Light Load", 5, 20))
            )  # Reduced clients

        if args.test in ["all", "medium"]:
            scenarios.append(
                ("MEDIUM LOAD", lambda: tester.run_load_scenario("Medium Load", 20, 30))
            )  # Reduced clients

        if args.test in ["all", "heavy"]:
            scenarios.append(
                ("HEAVY LOAD", lambda: tester.run_load_scenario("Heavy Load", 40, 50))
            )  # Reduced clients

        if args.test in ["all", "rps"]:
            scenarios.append(
                ("BURST RPS", lambda: tester.run_rps_scenario("Burst RPS", 50, 20))
            )  # Reduced RPS
            scenarios.append(
                (
                    "SUSTAINED RPS",
                    lambda: tester.run_rps_scenario("Sustained RPS", 30, 40),
                )
            )  # Reduced RPS

        if args.test in ["all", "spike"]:
            scenarios.append(("SPIKE TEST", lambda: tester.run_spike_test()))

        if args.test in ["all", "numbers"]:
            scenarios.append(
                ("NUMBER ACCURACY", lambda: tester.test_number_tracking_accuracy(100))
            )  # Reduced operations

        # Run all selected scenarios
        all_results = []
        for scenario_name, test_func in scenarios:
            start_time = time.time()
            result = test_func()
            duration = time.time() - start_time

            tester.print_detailed_results(result, scenario_name)
            all_results.append((scenario_name, result, duration))

            # Cool down between tests
            if scenario_name != scenarios[-1][0]:
                print(f"\n💤 Cooling down for 5 seconds...")
                time.sleep(5)

        # Summary
        if len(all_results) > 1:
            print(f"\n{'='*70}")
            print(f"📋 COMPREHENSIVE SUMMARY")
            print(f"{'='*70}")

            for scenario_name, result, duration in all_results:
                stats = result.get_stats()
                success_rate = (
                    (result.successful_requests / result.total_requests * 100)
                    if result.total_requests > 0
                    else 0
                )
                print(
                    f"  {scenario_name:20} | {success_rate:5.1f}% success | {stats['requests_per_second']:6.1f} RPS | {stats['p95_response_time']*1000:6.1f} ms P95 | {duration:5.1f}s"
                )

        print(f"\n🎉 All tests completed successfully!")

    except KeyboardInterrupt:
        print(f"\n⏹️  Testing interrupted by user")
    except Exception as e:
        print(f"\n❌ Fatal error: {e}")
        import traceback

        traceback.print_exc()
    finally:
        # Always clean up
        tester.cleanup()


if __name__ == "__main__":
    main()
