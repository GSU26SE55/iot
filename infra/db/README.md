# infra/db — TimescaleDB init

## Scripts auto-run

Khi container `timescaledb` khởi tạo lần đầu, mọi file trong `init/` được Postgres entrypoint chạy theo thứ tự alphabet:

| File | Tác dụng |
|------|----------|
| `01-create-databases.sql` | Tạo `battery_service_dev`, `user_service_dev`, `ticket_service_dev`, `notification_service_dev` |
| `02-enable-timescaledb.sql` | `CREATE EXTENSION timescaledb` trên `battery_service_dev` |

## Verify nhanh (S0-INF-03 acceptance)

```bash
docker compose -f infra/docker-compose.dev.yml exec timescaledb \
  psql -U postgres -d battery_service_dev -c '\dx'
```

Output mong đợi:
```
                                      List of installed extensions
    Name     | Version |   Schema   |                            Description
-------------+---------+------------+-------------------------------------------------------------------
 plpgsql     | 1.0     | pg_catalog | PL/pgSQL procedural language
 timescaledb | 2.15.2  | public     | Enables scalable inserts and complex queries for time-series data
```

## Reset (nếu cần init lại)

```bash
docker compose -f infra/docker-compose.dev.yml down -v   # XÓA volume → mất data
docker compose -f infra/docker-compose.dev.yml up -d timescaledb
```

> Init scripts CHỈ chạy khi volume `timescaledb-data` rỗng. Đã có data thì phải `down -v` mới rerun.
