import asyncio
import time
from typing import Dict, Any
from .collector import MetricsCollector


class MetricsReporter:
    """Репортер метрик"""

    def __init__(self, interval: int = 5):
        self.interval = interval
        self.is_running = False
        self.collector = MetricsCollector()
        self.start_time = time.time()

    async def start_reporting(self):
        """Запуск периодической отчетности"""
        self.is_running = True
        while self.is_running:
            await self.report()
            await asyncio.sleep(self.interval)

    async def report(self):
        """Печать текущих метрик"""
        summary = self.collector.get_summary()
        duration = time.time() - self.start_time

        print(f"\n📊 Metrics Report [{time.strftime('%H:%M:%S')}]")
        print(f"   Duration: {duration:.1f}s")
        print(f"   Requests: {summary['total_requests']}")
        print(f"   RPS: {summary['requests_per_second']:.1f}")
        print(f"   Success Rate: {summary['success_rate']:.1f}%")
        print(f"   Avg Response Time: {summary['avg_response_time']:.1f}ms")

    async def stop(self):
        """Остановка репортера"""
        self.is_running = False
