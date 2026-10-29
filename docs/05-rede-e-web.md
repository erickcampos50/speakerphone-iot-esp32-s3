# 05 — Rede e página web

> **Planejamento, não funcionalidade ativa.** A página e o AP descritos abaixo
> pertencem a um protótipo isolado, não integrado ao firmware gravado.

## Modo de operação

A placa sobe **sempre** como ponto de acesso aberto e, opcionalmente, também
entra em uma rede existente (STA). O AP garante que a página funcione mesmo sem
roteador por perto.

| Item | Valor |
|---|---|
| SSID | `ESP32-MIC` |
| Senha | *(aberta — sem senha)* |
| Endereço | `http://192.168.4.1` |
| mDNS (quando em STA) | `http://esp32mic.local` |
| Porta | 80 |

> Segurança foi dispensada explicitamente nesta fase de testes. **Não** use este
> firmware em rede pública com dados sensíveis.

## Configurar a rede da casa (opcional)

As credenciais ficam na NVS (namespace `wifi`, chaves `ssid` e `pass`). Duas
formas de gravar:

1. Pelo console serial (comando `wifi` — a implementar junto com o módulo).
2. Regravando o firmware com as credenciais como padrão de fábrica.

Enquanto não houver credenciais, só o AP existe.

## Rotas HTTP

| Método | Rota | Resposta |
|---|---|---|
| GET | `/` | Página HTML com estado, lista de arquivos e botões |
| GET | `/status` | JSON com estado atual |
| GET | `/list` | JSON com a lista de arquivos |
| GET | `/download?f=NOME` | Arquivo WAV (streaming, `Content-Disposition: attachment`) |
| POST | `/delete?f=NOME` | Apaga o arquivo |
| POST | `/rec?on=1` | Inicia a gravação |
| POST | `/rec?on=0` | Para a gravação |

### `/status`

```json
{
  "recording": true,
  "battery_v": 0.0,
  "battery_ok": false,
  "free_mb": 7600,
  "total_mb": 7864,
  "uptime_s": 3421,
  "files": 12
}
```

### Página principal

- Tema escuro, responsiva (funciona no celular).
- Cabeçalho: estado da gravação, bateria, espaço livre, IP, tempo ligado.
- Tabela: nome, tamanho, botão **Baixar**, botão **Apagar**.
- Botões **Iniciar gravação** / **Parar gravação**.
- Atualização do estado a cada 3 s via `fetch('/status')`, sem recarregar.

### Controle da gravação

A gravação **nunca** começa sozinha — nem ao abrir a página, nem ao conectar o
USB, nem ao entrar no WiFi. Ela só existe depois de uma ação explícita do
usuário (ADR-012):

| Canal | Iniciar | Parar |
|---|---|---|
| Botão | BOOT ou K1 | BOOT ou K1 |
| Página web | botão **Iniciar gravação** | botão **Parar gravação** |
| Console | `rec` | `stop` |

O estado é único: o que a página mostra é o mesmo que os LEDs e o console
mostram. Se a gravação estiver ativa e você abrir a página, o botão já aparece
como **Parar gravação** — nada é disparado pela visita.

### Microfone USB

O USB (UAC) é independente do WiFi e da gravação: a placa pode estar servindo a
página **e** funcionando como microfone no PC **e** gravando no cartão, tudo ao
mesmo tempo.

## Limitações conhecidas

- Baixar um arquivo grande consome banda do WiFi e CPU; durante a gravação pode
  aumentar o número de leituras curtas do SD. Para arquivos longos, prefira
  baixar com a gravação parada.
- O AP é 2,4 GHz (o S3 não tem 5 GHz).
- Sem autenticação: qualquer pessoa na mesma rede acessa os arquivos.

## Consumo

WiFi ligado adiciona tipicamente 80–120 mA em média (picos de ~250 mA ao
transmitir). Isso reduz a autonomia na bateria — para gravações longas, avalie
desligar o rádio por comando.
