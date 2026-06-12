-- Sprint 0 — S0-INF-03
-- Tạo DB battery_service_dev (và các DB khác cho microservice sau này)
-- File này chạy 1 lần khi container Postgres khởi tạo (initdb).

-- Tạo database cho BatteryService (telemetry + sensor readings, dùng TimescaleDB)
SELECT 'CREATE DATABASE battery_service_dev'
WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'battery_service_dev')\gexec

-- Placeholder cho các service khác (S2 trở đi)
SELECT 'CREATE DATABASE user_service_dev'
WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'user_service_dev')\gexec

SELECT 'CREATE DATABASE ticket_service_dev'
WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'ticket_service_dev')\gexec

SELECT 'CREATE DATABASE notification_service_dev'
WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'notification_service_dev')\gexec
