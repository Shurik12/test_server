#!/usr/bin/env python3

import sys
import os
import asyncio
import argparse

# Добавляем родительскую директорию в Python path
current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(current_dir)
sys.path.insert(0, parent_dir)

from load_testing.config import LoadTestConfig
from load_testing.scenarios.stress import StressTestScenario
from load_testing.scenarios.spike import SpikeTestScenario
from load_testing.scenarios.endurance import EnduranceTestScenario


class LoadTester:
    def __init__(self, config_path=None):
        self.config = LoadTestConfig(config_path)
        self.is_running = False
        self.start_time = None

    async def run_test(self, scenario_name):
        """Запуск тестирования по сценарию"""
        print(f"🚀 Starting {scenario_name} load test...")

        # Выбор сценария
        scenarios = {
            "stress": StressTestScenario,
            "spike": SpikeTestScenario,
            "endurance": EnduranceTestScenario,
        }

        if scenario_name not in scenarios:
            raise ValueError(f"Unknown scenario: {scenario_name}")

        scenario = scenarios[scenario_name](self.config)
        self.is_running = True

        try:
            results = await scenario.execute()
            self._print_results(results)
            return results

        except KeyboardInterrupt:
            print("\n🛑 Load test interrupted by user")
        except Exception as e:
            print(f"❌ Load test failed: {e}")
        finally:
            self.is_running = False

    def _print_results(self, results):
        """Печать результатов тестирования"""
        print("\n" + "=" * 60)
        print("📊 LOAD TEST RESULTS")
        print("=" * 60)

        duration = results.get("duration", 0)
        total_requests = results.get("total_requests", 0)
        successful_requests = results.get("successful_requests", 0)
        failed_requests = results.get("failed_requests", 0)

        rps = results.get("requests_per_second", 0)
        success_rate = results.get("success_rate", 0)

        print(f"Duration: {duration:.2f}s")
        print(f"Total Requests: {total_requests}")
        print(f"Successful: {successful_requests}")
        print(f"Failed: {failed_requests}")
        print(f"Requests/sec: {rps:.2f}")
        print(f"Success Rate: {success_rate:.2f}%")
        print(f"Avg Response Time: {results.get('avg_response_time', 0):.2f}ms")
        print(f"Max Response Time: {results.get('max_response_time', 0):.2f}ms")

        if "error_breakdown" in results and results["error_breakdown"]:
            print("\nError Breakdown:")
            for error, count in results["error_breakdown"].items():
                print(f"  {error}: {count}")


def main():
    parser = argparse.ArgumentParser(description="C++ Service Load Testing")
    parser.add_argument(
        "scenario",
        choices=["stress", "spike", "endurance"],
        help="Load test scenario to execute",
    )
    parser.add_argument("--config", "-c", default=None, help="Path to config file")
    parser.add_argument("--duration", "-d", type=int, help="Test duration in seconds")
    parser.add_argument("--users", "-u", type=int, help="Number of concurrent users")
    parser.add_argument("--rps", "-r", type=int, help="Target requests per second")

    args = parser.parse_args()

    # Запуск тестирования
    tester = LoadTester(args.config)

    # Переопределение конфигурации аргументами командной строки
    if args.duration:
        tester.config.duration = args.duration
    if args.users:
        tester.config.concurrent_users = args.users
    if args.rps:
        tester.config.target_rps = args.rps

    # Запуск
    asyncio.run(tester.run_test(args.scenario))


if __name__ == "__main__":
    main()
