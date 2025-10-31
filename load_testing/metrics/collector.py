import time
from typing import Dict, List, Any
from collections import defaultdict


class MetricsCollector:
    """Сборщик метрик производительности"""

    def __init__(self):
        self.reset()

    def reset(self):
        """Сброс метрик"""
        self.start_time = time.time()
        self.request_count = 0
        self.success_count = 0
        self.error_count = 0
        self.response_times = []
        self.errors = defaultdict(int)
        self.endpoint_stats = defaultdict(
            lambda: {"count": 0, "success": 0, "errors": 0, "response_times": []}
        )

    def record_request(self, result: Dict[str, Any]):
        """Запись результата запроса"""
        self.request_count += 1

        endpoint = result["endpoint"]
        self.endpoint_stats[endpoint]["count"] += 1
        self.endpoint_stats[endpoint]["response_times"].append(result["response_time"])

        if result["status"] == "success":
            self.success_count += 1
            self.endpoint_stats[endpoint]["success"] += 1
        else:
            self.error_count += 1
            self.endpoint_stats[endpoint]["errors"] += 1
            self.errors[result["error"]] += 1

        self.response_times.append(result["response_time"])

    def get_summary(self) -> Dict[str, Any]:
        """Получение сводки метрик"""
        duration = time.time() - self.start_time
        rps = self.request_count / duration if duration > 0 else 0

        response_times = self.response_times
        avg_response_time = (
            sum(response_times) / len(response_times) if response_times else 0
        )
        max_response_time = max(response_times) if response_times else 0

        success_rate = (
            (self.success_count / self.request_count * 100)
            if self.request_count > 0
            else 0
        )

        return {
            "duration": duration,
            "total_requests": self.request_count,
            "successful_requests": self.success_count,
            "failed_requests": self.error_count,
            "requests_per_second": rps,
            "success_rate": success_rate,
            "avg_response_time": avg_response_time,
            "max_response_time": max_response_time,
            "error_breakdown": dict(self.errors),
            "endpoint_stats": dict(self.endpoint_stats),
        }
