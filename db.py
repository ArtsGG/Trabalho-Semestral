from __future__ import annotations

import logging
import os
import time
from dataclasses import dataclass
from typing import Any, Dict, Optional

from dotenv import load_dotenv
from pymongo.errors import PyMongoError
from pymongo.mongo_client import MongoClient
from pymongo.server_api import ServerApi

# MongoDB wrapper with repository-like functions.
# This avoids module-level globals and provides clearer SRP/Cohesion.


load_dotenv()


@dataclass
class MongoDB:
    """Small wrapper for MongoDB operations.

    It implements a few repository methods (single responsibility) while
    hiding connection details.
    """

    uri: str = os.getenv("MONGO_URI") or ""
    db_name: str = os.getenv("DB_NAME", "IoT")

    def __post_init__(self) -> None:
        self.logger = logging.getLogger("BancoIoT.db")
        self.logger.setLevel(logging.INFO)
        handler = logging.StreamHandler()
        handler.setFormatter(
            logging.Formatter("[%(asctime)s] %(levelname)s: %(message)s")
        )
        self.logger.addHandler(handler)
        self.client: Optional[MongoClient] = None
        self.db: Optional[Any] = None

        if not self.uri:
            self.logger.error(
                "Variável de ambiente MONGO_URI não configurada!"
            )
            raise RuntimeError(
                "ERRO: Variável de ambiente MONGO_URI não configurada!"
            )

    def init_db(self, retries: int = 5, delay: float = 2.0) -> None:
        """Initialize database connection with retry behavior."""
        for attempt in range(retries):
            try:
                self.logger.info(
                    "Tentando conectar ao MongoDB... (tentativa %d/%d)",
                    attempt + 1,
                    retries,
                )
                self.client = MongoClient(self.uri, server_api=ServerApi("1"))
                self.client.admin.command("ping")
                self.db = self.client[self.db_name]
                self.db.leituras.create_index([("uid_tag", 1)], name="uid_ts")
                self.db.leituras.create_index("timestamp")
                self.db.logs_api.create_index("access_time")
                self.logger.info("Conexão com MongoDB Atlas estabelecida!")
                return
            except PyMongoError:
                self.logger.warning(
                    "Falha ao conectar ao MongoDB. Tentando novamente..."
                )
                time.sleep(delay)

        self.logger.error(
            "Não foi possível conectar ao MongoDB após %d tentativas.", retries
        )
        raise RuntimeError(
            "Não foi possível conectar ao MongoDB após múltiplas tentativas."
        )

    def registrar_log(
        self,
        endpoint: str,
        method: str,
        leitura_id: Optional[str],
        client_ip: Optional[str],
        payload: Optional[Dict[str, Any]],
        status: int,
        response_time_ms: Optional[int] = None,
    ) -> None:
        """Write a compact log into logs_api collection."""
        if not self.db:
            self.logger.warning("DB não inicializado; ignorando log")
            return

        doc = {
            "api_endpoint": endpoint,
            "method": method,
            "access_time": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "leitura_id": leitura_id,
            "client_ip": client_ip,
            "payload": payload,
            "status": status,
            "response_time_ms": response_time_ms,
        }
        try:
            self.db.logs_api.insert_one(doc)
        except PyMongoError:
            self.logger.warning("Falha ao gravar logs na base")

    def insert_leitura(self, data: Dict[str, Any]) -> str:
        """Insert a leitura document and return its ObjectId string."""
        if not self.db:
            raise RuntimeError("DB não inicializado")
        result = self.db.leituras.insert_one(data)
        return str(result.inserted_id)

    def list_leituras(self, limit: int = 100) -> list[Dict[str, Any]]:
        if not self.db:
            raise RuntimeError("DB não inicializado")
        cursor = (
            self.db.leituras.find({}, {"_id": 0})
            .sort("timestamp", -1)
            .limit(limit)
        )
        return list(cursor)

    def list_logs(self, limit: int = 100) -> list[Dict[str, Any]]:
        if not self.db:
            raise RuntimeError("DB não inicializado")
        cursor = (
            self.db.logs_api.find({}, {"_id": 0})
            .sort("access_time", -1)
            .limit(limit)
        )
        return list(cursor)


mongo = MongoDB()
