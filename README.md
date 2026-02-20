# NetGate Lite

Эмулятор сетевого шлюза на C++20 с in-memory аналитикой на Tarantool.

## Стек
- **C++20** — `std::jthread`, `std::atomic`, `std::condition_variable`
- **Tarantool** — in-memory хранилище метрик, Lua-аналитика
- **ns-3** — симуляция задержек сети (опционально)
- **Python** — CLI-дашборд через IPC (Named Pipe)

## Что делает
- Producer-Consumer на пуле потоков (1 producer, 3 workers)
- Потокобезопасная очередь с bounded capacity
- IPC через Unix FIFO — статистика в реальном времени
- Tarantool: пороговые алерты, spike detection, агрегация, TTL-очистка

## Сборка

```bash
mkdir build && cd build
cmake .. -DUSE_TARANTOOL=ON   # опционально: -DUSE_NS3=ON
make -j$(nproc)
```

## Запуск

```bash
# 1. Tarantool
tarantool tarantool/init.lua

# 2. Шлюз
./build/netgate

# 3. Дашборд (опционально)
python3 scripts/cli.py
```

## Структура

```
src/
  main.cpp              — точка входа, потоки, main loop
  PacketQueue.hpp       — потокобезопасная очередь
  ns3_analyzer.hpp/cpp  — интеграция с ns-3
  tarantool_sink.hpp/cpp — коннектор к Tarantool

tarantool/
  init.lua     — схема БД, экспорт функций, запуск console
  metrics.lua  — алерты, spike detection, top-N, дашборд
  cleanup.lua  — TTL-очистка, ring buffer

scripts/
  cli.py — Python-дашборд (IPC via FIFO)
```
