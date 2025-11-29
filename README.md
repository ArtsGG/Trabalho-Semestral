# Banco-IoT

API REST simples para armazenar leituras do ESP32 no MongoDB Atlas.

## Requisitos
- Python 3.10+
- MongoDB Atlas com `MONGO_URI` configurado

## Como usar
1. Crie um arquivo `.env` com as variáveis:
```
MONGO_URI=mongodb+srv://usuario:senha@cluster.mongodb.net/?retryWrites=true&w=majority
DB_NAME=IoT
```

2. Instale dependências:
```powershell
python -m pip install -r requirements.txt
```

3. Execute:
```powershell
python Banco-IoT.py
```

4. Endpoints
- POST /leituras — adiciona uma leitura (JSON com `presenca`, `acesso`, `uid_tag`)
- GET /leituras — lista leituras (retorna `total` e `dados`)
- GET /logs_api — traz logs de acesso

## Notas de segurança
- Não commit `.env` em repositório público. Ele está em `.gitignore`.
- Para produção, use variáveis de ambiente do sistema/serviço e rode com WSGI server.
