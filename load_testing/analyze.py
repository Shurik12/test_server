#!/usr/bin/env python3

import json
import matplotlib.pyplot as plt
import numpy as np
from datetime import datetime


def analyze_performance():
    """Анализ производительности на основе результатов"""

    # Примерные результаты (в реальности можно сохранять в файл)
    results = {
        "stress": {
            "rps": 525.82,
            "success_rate": 100.0,
            "avg_response_time": 34.87,
            "max_response_time": 1060.41,
            "duration": 300,
        }
    }

    print("📈 PERFORMANCE ANALYSIS")
    print("=" * 50)

    for test_name, metrics in results.items():
        print(f"\n{test_name.upper()} TEST:")
        print(f"  Requests/sec: {metrics['rps']:.1f}")
        print(f"  Success Rate: {metrics['success_rate']:.1f}%")
        print(f"  Avg Response: {metrics['avg_response_time']:.2f}ms")
        print(f"  Max Response: {metrics['max_response_time']:.2f}ms")

        # Оценка производительности
        if metrics["rps"] > 1000:
            rating = "EXCELLENT 🚀"
        elif metrics["rps"] > 500:
            rating = "VERY GOOD 👍"
        elif metrics["rps"] > 200:
            rating = "GOOD ✅"
        else:
            rating = "NEEDS IMPROVEMENT ⚠️"

        print(f"  Rating: {rating}")


def generate_report():
    """Генерация отчета в файл"""
    report = {
        "timestamp": datetime.now().isoformat(),
        "service": "C++ JSON Processing Service",
        "test_results": {
            "stress_test": {
                "requests_per_second": 525.82,
                "success_rate": 100.0,
                "average_response_time_ms": 34.87,
                "max_response_time_ms": 1060.41,
                "total_requests": 158080,
                "duration_seconds": 300.63,
            }
        },
        "performance_rating": "VERY_GOOD",
        "recommendations": [
            "Service handles 500+ RPS reliably",
            "Consider connection pooling for higher loads",
            "Monitor memory usage during peak loads",
        ],
    }

    with open("load_test_report.json", "w") as f:
        json.dump(report, f, indent=2)

    print("📄 Report saved to load_test_report.json")


if __name__ == "__main__":
    analyze_performance()
    generate_report()
