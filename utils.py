import re
from datetime import datetime
from typing import Optional

from flask import jsonify

UID_REGEX = re.compile(r"^[A-Fa-f0-9 ]{8,}$")


def validar_json(data: dict, required: list[str]):
    for campo in required:
        if campo not in data:
            return f"Campo obrigatório ausente: {campo}"
    # UID validation
    uid = str(data.get("uid_tag", "")).strip()
    if not UID_REGEX.match(uid):
        return "UID inválido"
    return None


def sanitize_data(data: dict) -> dict:
    # Convert presenca
    try:
        data["presenca"] = bool(int(str(data.get("presenca", "0"))))
    except (ValueError, TypeError):
        data["presenca"] = False

    # Convert acesso
    data["acesso"] = str(data.get("acesso", "False")).lower() == "true"

    # Sanitize uid
    data["uid_tag"] = str(data.get("uid_tag", "")).strip()

    # timestamp
    data.setdefault("timestamp", datetime.now().isoformat())
    return data


def make_response(
    success: bool, data=None, message: str = "", status_code: int = 200
):
    body = {
        "success": bool(success),
        "data": data or {},
        "message": message,
    }
    return jsonify(body), status_code


def get_client_ip(flask_request) -> Optional[str]:
    # Respect X-Forwarded-For if behind a proxy
    forwarded = flask_request.headers.get("X-Forwarded-For")
    if forwarded:
        return forwarded.split(",")[0].strip()
    return flask_request.remote_addr
