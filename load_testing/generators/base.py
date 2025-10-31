from abc import ABC, abstractmethod
from typing import Any, Dict


class BaseGenerator(ABC):
    """Базовый класс для генераторов"""

    @abstractmethod
    def generate(self) -> Any:
        pass
