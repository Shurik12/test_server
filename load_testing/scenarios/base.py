import random
import time
from typing import Dict, Any, List
from collections import defaultdict


class BaseScenario:
    """Базовый класс для сценариев тестирования"""

    def __init__(self, config):
        self.config = config

    def _select_endpoint(self) -> Dict[str, Any]:
        """Выбор endpoint на основе весов"""
        endpoints = self.config.endpoints
        weights = [ep["weight"] for ep in endpoints]
        return random.choices(endpoints, weights=weights)[0]

    def _init_metrics(self) -> Dict[str, Any]:
        """Инициализация метрик"""
        return {
            "total_requests": 0,
            "successful_requests": 0,
            "failed_requests": 0,
            "response_times": [],
            "errors": defaultdict(int),
            "start_time": time.time(),
        }

    def _update_metrics(self, metrics: Dict[str, Any], results: List[Dict[str, Any]]):
        """Обновление метрик на основе результатов"""
        for result in results:
            metrics["total_requests"] += 1
            metrics["response_times"].append(result["response_time"])

            if result["status"] == "success":
                metrics["successful_requests"] += 1
            else:
                metrics["failed_requests"] += 1
                metrics["errors"][result["error"]] += 1

    def _finalize_metrics(
        self, metrics: Dict[str, Any], start_time: float
    ) -> Dict[str, Any]:
        """Финальный расчет метрик"""
        duration = time.time() - start_time
        response_times = metrics["response_times"]

        avg_response_time = (
            sum(response_times) / len(response_times) if response_times else 0
        )
        max_response_time = max(response_times) if response_times else 0

        return {
            "total_requests": metrics["total_requests"],
            "successful_requests": metrics["successful_requests"],
            "failed_requests": metrics["failed_requests"],
            "duration": duration,
            "requests_per_second": (
                metrics["total_requests"] / duration if duration > 0 else 0
            ),
            "success_rate": (
                (metrics["successful_requests"] / metrics["total_requests"] * 100)
                if metrics["total_requests"] > 0
                else 0
            ),
            "avg_response_time": avg_response_time,
            "max_response_time": max_response_time,
            "error_breakdown": dict(metrics["errors"]),
        }
