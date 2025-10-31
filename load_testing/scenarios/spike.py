import asyncio
import time
from typing import Dict, Any
from .base import BaseScenario
from load_testing.generators.http import HTTPRequestGenerator


class SpikeTestScenario(BaseScenario):
    """Сценарий тестирования пиковых нагрузок"""

    async def execute(self) -> Dict[str, Any]:
        """Запуск тестирования пиковых нагрузок"""
        print("⚡ Starting spike test...")

        metrics = self._init_metrics()
        start_time = time.time()

        async with HTTPRequestGenerator(self.config) as request_generator:
            # Получаем фазы из конфигурации или используем значения по умолчанию
            phases = getattr(
                self.config,
                "spike_phases",
                [
                    (
                        60,
                        self.config.concurrent_users // 2,
                    ),  # 1 мин нормальной нагрузки
                    (10, self.config.concurrent_users * 3),  # 10 сек пиковой нагрузки
                    (
                        120,
                        self.config.concurrent_users // 2,
                    ),  # 2 мин нормальной нагрузки
                    (
                        10,
                        self.config.concurrent_users * 5,
                    ),  # 10 сек экстремальной нагрузки
                    (60, self.config.concurrent_users // 2),  # 1 мин восстановления
                ],
            )

            for phase_duration, phase_users in phases:
                print(f"🔄 Phase: {phase_users} users for {phase_duration}s")

                phase_start = time.time()
                while time.time() - phase_start < phase_duration:
                    tasks = []
                    for _ in range(phase_users):
                        endpoint = self._select_endpoint()
                        tasks.append(request_generator.make_request(endpoint))

                    results = await asyncio.gather(*tasks)
                    self._update_metrics(metrics, results)

                    await asyncio.sleep(0.1)

        return self._finalize_metrics(metrics, start_time)
