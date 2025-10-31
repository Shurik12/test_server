#!/usr/bin/env python3

import requests
import time
import json
from datetime import datetime


class ServiceMonitor:
    def __init__(self, base_url="http://localhost:8080"):
        self.base_url = base_url
        self.metrics_history = []

    def collect_metrics(self):
        """Сбор метрик из Prometheus endpoint"""
        try:
            response = requests.get(f"{self.base_url}/metrics", timeout=5)
            metrics = response.text

            # Парсинг ключевых метрик
            current_metrics = {
                "timestamp": datetime.now().isoformat(),
                "requests_total": self._parse_metric(
                    metrics, "cpp_service_requests_total"
                ),
                "active_connections": self._parse_metric(
                    metrics, "cpp_service_active_connections"
                ),
            }

            self.metrics_history.append(current_metrics)
            return current_metrics

        except Exception as e:
            print(f"Error collecting metrics: {e}")
            return None

    def _parse_metric(self, metrics_text, metric_name):
        """Парсинг значения метрики из текста"""
        for line in metrics_text.split("\n"):
            if line.startswith(metric_name) and not line.startswith("#"):
                try:
                    return float(line.split()[-1])
                except:
                    pass
        return 0

    def monitor_loop(self, interval=5):
        """Цикл мониторинга"""
        print("🔍 Starting service monitoring...")
        try:
            while True:
                metrics = self.collect_metrics()
                if metrics:
                    print(
                        f"[{datetime.now().strftime('%H:%M:%S')}] "
                        f"Requests: {metrics['requests_total']}, "
                        f"Connections: {metrics['active_connections']}"
                    )

                time.sleep(interval)
        except KeyboardInterrupt:
            print("\n🛑 Monitoring stopped")
            self.save_history()

    def save_history(self):
        """Сохранение истории метрик"""
        with open("monitoring_history.json", "w") as f:
            json.dump(self.metrics_history, f, indent=2)
        print("💾 Monitoring history saved")


if __name__ == "__main__":
    monitor = ServiceMonitor()
    monitor.monitor_loop()
