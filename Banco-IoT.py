from time import perf_counter

from flask import Flask, request
from pymongo.errors import PyMongoError
from werkzeug.exceptions import BadRequest

from db import mongo
from utils import (
    get_client_ip,
    make_response,
    sanitize_data,
    validar_json,
)

app = Flask(__name__)


@app.errorhandler(Exception)
def erro_global(exc):
    mongo.logger.exception("Erro global no servidor: %s", exc)
    return make_response(False, None, "Erro interno do servidor", 500)


# ==========================================================
# CONFIGURAÇÃO DO MONGO VIA VARIÁVEL DE AMBIENTE
# ==========================================================
# Delegamos inicialização para db.init_db()


# ==========================================================
# FUNÇÃO PARA REGISTRO DE LOG AUTOMATIZADA
# ==========================================================
def registrar_log(
    endpoint, leitura_id, status=200, payload=None, response_time_ms=None
):
    client_ip = get_client_ip(request)
    # Não logamos payload inteiro por segurança — apenas o UID quando disponível
    payload_for_log = None
    if payload and isinstance(payload, dict):
        payload_for_log = {"uid_tag": payload.get("uid_tag")}
    mongo.registrar_log(
        endpoint,
        request.method,
        leitura_id,
        client_ip,
        payload_for_log,
        status,
        response_time_ms,
    )


# ==========================================================
# ROTA POST — RECEBER LEITURAS DO ESP32
# ==========================================================
@app.route("/leituras", methods=["POST"])
def receber_leituras():
    try:
        start = perf_counter()
        data = request.get_json(force=True)

        if not data:
            return make_response(False, None, "JSON inválido", 400)

        # Validacao dos campos obrigatórios e do UID
        required = ["presenca", "acesso", "uid_tag"]
        erro = validar_json(data, required)
        if erro:
            return make_response(False, None, erro, 400)

        # Sanitização
        data = sanitize_data(data)

        # Inserção no banco
        leitura_id = mongo.insert_leitura(data)

        # Salvar log (medindo o tempo de resposta)
        resposta_ms = int((perf_counter() - start) * 1000)
        registrar_log(
            "/leituras",
            leitura_id,
            status=201,
            payload={"uid_tag": data.get("uid_tag")},
            response_time_ms=resposta_ms,
        )

        return make_response(
            True, {"id": leitura_id}, "Leitura registrada com sucesso", 201
        )

    except BadRequest:
        return make_response(False, None, "JSON inválido", 400)
    except PyMongoError:
        mongo.logger.exception("Erro de banco ao processar /leituras")
        return make_response(False, None, "Erro no banco de dados", 500)
    # let the global error handler capture unexpected exceptions


# ==========================================================
# ROTA GET — LISTAR LEITURAS
# ==========================================================
@app.route("/leituras", methods=["GET"])
def listar_leituras():
    try:
        leituras_cursor = mongo.list_leituras(limit=100)
        leituras = list(leituras_cursor)

        # Sanitização final
        for leitura in leituras:
            leitura["presenca"] = bool(leitura.get("presenca", False))
            leitura["acesso"] = bool(leitura.get("acesso", False))

        total = len(leituras)
        return make_response(
            True, {"total": total, "dados": leituras}, "OK", 200
        )

    except PyMongoError:
        mongo.logger.exception("Erro ao listar leituras")
        return make_response(False, None, "Erro no banco de dados", 500)
    # let the global error handler capture unexpected exceptions


# ==========================================================
# ROTA GET — LISTAR LOGS DA API
# ==========================================================
@app.route("/logs_api", methods=["GET"])
def listar_logs():
    try:
        logs = mongo.list_logs(limit=100)
        total = len(logs)
        return make_response(True, {"total": total, "dados": logs}, "OK", 200)

    except PyMongoError:
        mongo.logger.exception("Erro ao listar logs")
        return make_response(False, None, "Erro no banco de dados", 500)
    # let the global error handler capture unexpected exceptions


# ==========================================================
# EXECUÇÃO DO SERVIDOR
# ==========================================================
if __name__ == "__main__":
    # Inicializa base de dados
    mongo.init_db()
    print("Servidor iniciado em http://0.0.0.0:5000")
    # Configurar logger do app
    mongo.logger.info("Inicializando servidor Flask")
    app.run(host="0.0.0.0", port=5000, debug=False)
