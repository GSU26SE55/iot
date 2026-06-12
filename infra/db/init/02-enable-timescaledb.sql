-- Sprint 0 — S0-INF-03
-- Enable timescaledb extension trên database battery_service_dev
-- (timescaledb image đã preload library, chỉ cần CREATE EXTENSION)

\connect battery_service_dev

CREATE EXTENSION IF NOT EXISTS timescaledb;

-- Verify
DO $$
DECLARE
    ext_version text;
BEGIN
    SELECT extversion INTO ext_version FROM pg_extension WHERE extname = 'timescaledb';
    RAISE NOTICE '[S0-INF-03] TimescaleDB extension installed in battery_service_dev (version %)', ext_version;
END $$;
