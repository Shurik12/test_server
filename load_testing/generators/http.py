import aiohttp
import asyncio
import time
from typing import Dict, Any, List
from .data import DataGenerator


class HTTPRequestGenerator:
    """Генератор HTTP запросов"""

    def __init__(self, config):
        self.config = config
        self.data_generator = DataGenerator(config)
        self.session = None

    async def __aenter__(self):
        self.session = aiohttp.ClientSession(
            timeout=aiohttp.ClientTimeout(total=self.config.timeout)
        )
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        if self.session:
            await self.session.close()

    async def make_request(self, endpoint: Dict) -> Dict[str, Any]:
        """Выполнение HTTP запроса"""
        url = self.config.base_url + endpoint["path"]
        headers = endpoint.get("headers", {})

        start_time = time.time()
        status = "success"
        error_message = None
        status_code = 0

        try:
            if endpoint["method"] == "POST":
                data = self.data_generator.generate_json_payload()
                async with self.session.post(
                    url, data=data, headers=headers
                ) as response:
                    status_code = response.status
                    if response.status >= 400:
                        status = "error"
                        error_message = f"HTTP {response.status}"
            else:
                async with self.session.get(url, headers=headers) as response:
                    status_code = response.status
                    if response.status >= 400:
                        status = "error"
                        error_message = f"HTTP {response.status}"

        except asyncio.TimeoutError:
            status = "error"
            error_message = "timeout"
        except Exception as e:
            status = "error"
            error_message = str(e)

        response_time = (time.time() - start_time) * 1000  # в миллисекундах

        return {
            "status": status,
            "response_time": response_time,
            "error": error_message,
            "status_code": status_code,
            "endpoint": endpoint["path"],
        }
