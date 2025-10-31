import yaml
import os
from dataclasses import dataclass
from typing import Dict, Any, List


@dataclass
class LoadTestConfig:
    """Конфигурация нагрузочного тестирования"""

    # Основные настройки
    base_url: str = "http://localhost:8080"
    duration: int = 300  # 5 минут по умолчанию
    concurrent_users: int = 100
    target_rps: int = 1000
    ramp_up_time: int = 30  # время наращивания нагрузки

    # Endpoints для тестирования
    endpoints: List[Dict] = None

    # Настройки запросов
    timeout: int = 10
    max_retries: int = 3

    # Генерация данных
    data_generation: Dict[str, Any] = None

    def __init__(self, config_path=None):
        if config_path and os.path.exists(config_path):
            self.load_from_file(config_path)
        else:
            self._set_defaults()

    def _set_defaults(self):
        """Установка значений по умолчанию"""
        self.endpoints = [
            {
                "path": "/process",
                "method": "POST",
                "weight": 70,  # 70% трафика
                "headers": {"Content-Type": "application/json"},
            },
            {
                "path": "/process-async",
                "method": "POST",
                "weight": 20,  # 20% трафика
                "headers": {"Content-Type": "application/json"},
            },
            {
                "path": "/health",
                "method": "GET",
                "weight": 5,  # 5% трафика
                "headers": {},
            },
            {
                "path": "/metrics",
                "method": "GET",
                "weight": 5,  # 5% трафика
                "headers": {},
            },
        ]

        self.data_generation = {
            "min_id": 1,
            "max_id": 1000000,
            "name_length": 10,
            "phone_prefix": "+1",
        }

    def load_from_file(self, config_path):
        """Загрузка конфигурации из файла"""
        with open(config_path, "r") as f:
            config_data = yaml.safe_load(f)

        for key, value in config_data.items():
            if hasattr(self, key):
                setattr(self, key, value)

    def load_from_file(self, config_path):
        """Загрузка конфигурации из файла"""
        with open(config_path, "r") as f:
            config_data = yaml.safe_load(f)

        for key, value in config_data.items():
            if hasattr(self, key):
                setattr(self, key, value)

            # Обработка специальных конфигураций для сценариев
            if key == "spike_test" and "phases" in value:
                # Конвертируем фазы в нужный формат
                self.spike_phases = [
                    (phase["duration"], phase["users"]) for phase in value["phases"]
                ]
