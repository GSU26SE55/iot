# Solar Battery IoT Gateway

Project nay la phan IoT rieng cho capstone. No chay doc lap voi backend, dung de:

- Gia lap gateway gui sensor readings len `BatteryService`.
- Chay tren Raspberry Pi de doc BMS that qua RS485/Modbus hoac CAN.
- Gui heartbeat, queue local khi mat mang, retry bang `Idempotency-Key`.
- Lam nen cho provision/device lifecycle khi backend IoT duoc implement sau.

## 1. Kien truc

```text
BMS / Sensors
  -> gateway/bms adapter
  -> validation + calibration
  -> local SQLite queue
  -> HTTPS API client
  -> BatteryService
```

Code chinh:

```text
iot/
  gateway/
    api/client.py           # HTTP client theo contract iot.md
    bms/mock.py             # simulator chay duoc ngay
    bms/modbus.py           # RS485/Modbus adapter
    bms/canbus.py           # CAN adapter
    queue/local_store.py    # SQLite durable queue
    telemetry/heartbeat.py  # CPU/RAM/disk/signal heartbeat
    runner.py               # orchestration
    main.py                 # CLI
  config/config.example.json
  config/config.production.example.json
  config/config.modbus.example.json
  config/config.canbus.example.json
  systemd/solar-battery-gateway.service
  tests/
```

## 2. Chay simulator nhanh

Mac/laptop/Raspberry Pi deu chay duoc bang Python 3.11+.

```bash
cd /Users/alex/Documents/capstone/iot
python -m gateway.main --config config/config.example.json --once --dry-run
```

Config mau dang de `dryRun=true`, nen lenh tren chi in request, khong goi backend.

Chay loop:

```bash
python -m gateway.main --config config/config.example.json --loop --dry-run
```

Sinh anomaly:

```bash
python -m gateway.main --config config/config.example.json --once --dry-run --scenario overheat
python -m gateway.main --config config/config.example.json --once --dry-run --scenario low_soc
python -m gateway.main --config config/config.example.json --once --dry-run --scenario soh_degradation
python -m gateway.main --config config/config.example.json --once --dry-run --scenario mixed
```

## 3. Ket noi backend hien tai

Backend hien tai moi ho tro ingest co ban:

```http
POST /api/sensor-readings/batch
X-Api-Key: dev-battery-ingest-key
```

Vi vay config mac dinh dung:

```json
{
  "ingestMode": "legacy"
}
```

Truoc khi gui that, thay:

- `backendBaseUrl`
- `apiKey`
- `batteryMappings[0].batteryAssetId`

Sau do tat dry run:

```bash
GATEWAY_API_KEY='dev-battery-ingest-key' \
python -m gateway.main --config config/config.example.json --once
```

## 4. Production ingest mode

Khi backend IoT device management duoc implement, doi config:

```json
{
  "ingestMode": "production"
}
```

Payload luc do se co:

- `X-Device-Code`
- `Idempotency-Key`
- `deviceTimestamp`
- `readings[].batteryAssetSerial`
- `readings[].sensorSourceCode`

## 5. Provision, heartbeat, firmware

CLI da co san lenh theo `backend/iot.md`:

```bash
python -m gateway.main --config config/config.example.json --provision
python -m gateway.main --config config/config.example.json --heartbeat-only
python -m gateway.main --config config/config.example.json --firmware-check
python -m gateway.main --config config/config.example.json --flush-only
```

Luu y: cac endpoint nay can backend IoT production duoc implement sau.

## 6. Local queue

Moi sensor batch duoc ghi vao SQLite truoc, roi moi flush len backend. Neu backend down:

- message van nam trong `data/gateway_queue.sqlite3`
- lan sau gateway retry
- moi message co `Idempotency-Key` rieng
- retry dung exponential backoff

Kiem tra queue:

```bash
python -m gateway.main --config config/config.example.json --flush-only --dry-run
```

## 7. Chay voi Modbus/RS485

Can cai dependency hardware:

```bash
pip install -r requirements-hardware.txt
```

Config:

```json
{
  "adapter": "modbus",
  "hardware": {
    "modbus": {
      "port": "/dev/ttyUSB0",
      "baudrate": 9600,
      "unitId": 1,
      "registers": {
        "voltage": { "address": 0, "scale": 0.01 },
        "current": { "address": 1, "scale": 0.01, "signed": true },
        "temperature": { "address": 2, "scale": 0.1, "offset": -40 },
        "socPercent": { "address": 3, "scale": 1 }
      }
    }
  }
}
```

Register map phai lay tu tai lieu BMS that.

## 8. Chay voi CAN

Can cai dependency hardware:

```bash
pip install -r requirements-hardware.txt
```

Config:

```json
{
  "adapter": "canbus",
  "hardware": {
    "canbus": {
      "channel": "can0",
      "bustype": "socketcan",
      "bitrate": 500000,
      "frames": {
        "0x351": {
          "voltage": { "start": 0, "length": 2, "scale": 0.01, "endian": "little" },
          "current": { "start": 2, "length": 2, "scale": 0.01, "signed": true, "endian": "little" },
          "socPercent": { "start": 4, "length": 1, "scale": 1, "endian": "little" }
        }
      }
    }
  }
}
```

CAN frame layout phai lay tu tai lieu BMS/inverter.

## 9. Cai tren Raspberry Pi bang systemd

Vi du:

```bash
sudo mkdir -p /opt/solar-battery-iot /etc/solar-battery-gateway
sudo cp -R gateway config requirements*.txt /opt/solar-battery-iot/
sudo cp config/config.example.json /etc/solar-battery-gateway/config.json
sudo cp systemd/solar-battery-gateway.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable solar-battery-gateway
sudo systemctl start solar-battery-gateway
```

Sua `/etc/solar-battery-gateway/config.json` truoc khi start service that.

## 10. Test

```bash
cd /Users/alex/Documents/capstone/iot
python -m unittest discover -s tests
```

Hoac chay smoke test gom compile, unit tests va CLI dry-run:

```bash
sh scripts/smoke_test.sh
```
