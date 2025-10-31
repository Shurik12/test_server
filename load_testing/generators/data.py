import random
import string
from typing import Dict, Any


class DataGenerator:
    """Генератор тестовых данных"""

    def __init__(self, config):
        self.config = config.data_generation

    def generate_user_data(self) -> Dict[str, Any]:
        """Генерация данных пользователя"""
        user_id = random.randint(
            self.config.get("min_id", 1), self.config.get("max_id", 1000000)
        )

        name = "".join(
            random.choices(string.ascii_letters, k=self.config.get("name_length", 10))
        )

        phone = self.config.get("phone_prefix", "+1") + "".join(
            random.choices(string.digits, k=10)
        )

        number = random.randint(1, 1000)

        return {"id": user_id, "name": name, "phone": phone, "number": number}

    def generate_json_payload(self) -> str:
        """Генерация JSON payload"""
        import json

        data = self.generate_user_data()
        return json.dumps(data)
