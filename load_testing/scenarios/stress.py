import asyncio
import time
from typing import Dict, Any
from .base import BaseScenario
from load_testing.generators.http import HTTPRequestGenerator


class StressTestScenario(BaseScenario):
    """Сценарий стресс-тестирования"""

    async def execute(self) -> Dict[str, Any]:
        """Запуск стресс-тестирования"""
        print("🎯 Starting stress test...")

        metrics = self._init_metrics()
        start_time = time.time()

        async with HTTPRequestGenerator(self.config) as request_generator:
            # Постепенное наращивание нагрузки
            for stage in range(1, 6):
                target_users = self.config.concurrent_users * stage // 5
                stage_duration = self.config.ramp_up_time // 5

                print(f"📈 Stage {stage}: {target_users} users for {stage_duration}s")

                stage_start = time.time()
                while time.time() - stage_start < stage_duration:
                    tasks = []
                    for _ in range(target_users):
                        endpoint = self._select_endpoint()
                        tasks.append(request_generator.make_request(endpoint))

                    results = await asyncio.gather(*tasks)
                    self._update_metrics(metrics, results)

                    # Контроль RPS
                    await asyncio.sleep(0.1)

            # Основная фаза тестирования
            main_duration = self.config.duration - self.config.ramp_up_time
            print(
                f"🔥 Main phase: {self.config.concurrent_users} users for {main_duration}s"
            )

            main_start = time.time()
            while time.time() - main_start < main_duration:
                tasks = []
                for _ in range(self.config.concurrent_users):
                    endpoint = self._select_endpoint()
                    tasks.append(request_generator.make_request(endpoint))

                results = await asyncio.gather(*tasks)
                self._update_metrics(metrics, results)

                # Контроль RPS
                await asyncio.sleep(0.1)

        return self._finalize_metrics(metrics, start_time)
