import asyncio
import time
from typing import Dict, Any
from .base import BaseScenario
from load_testing.generators.http import HTTPRequestGenerator


class EnduranceTestScenario(BaseScenario):
    """Сценарий тестирования на выносливость"""

    async def execute(self) -> Dict[str, Any]:
        """Запуск долговременного тестирования"""
        print("⏳ Starting endurance test...")

        metrics = self._init_metrics()
        start_time = time.time()

        async with HTTPRequestGenerator(self.config) as request_generator:
            # Долговременная стабильная нагрузка
            print(
                f"🕐 Long-term test: {self.config.concurrent_users} users for {self.config.duration}s"
            )

            while time.time() - start_time < self.config.duration:
                tasks = []
                for _ in range(self.config.concurrent_users):
                    endpoint = self._select_endpoint()
                    tasks.append(request_generator.make_request(endpoint))

                results = await asyncio.gather(*tasks)
                self._update_metrics(metrics, results)

                # Небольшая пауза для контроля RPS
                await asyncio.sleep(0.1)

        return self._finalize_metrics(metrics, start_time)
