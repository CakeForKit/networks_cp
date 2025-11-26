#!/bin/bash

# Запуск сервера в фоне
./static_server 8080 &
SERVER_PID=$!

# Даем серверу время на запуск
sleep 2

echo "Starting load testing..."

# Тест 1: Проверка базовой функциональности
echo "=== Basic functionality test ==="
curl -s http://localhost:8080/ > /dev/null
curl -s http://localhost:8080/style.css > /dev/null
curl -s -I http://localhost:8080/ > /dev/null
echo "Basic tests completed"

# Тест 2: Множественные одновременные соединения
echo "=== Concurrent connections test ==="
for i in {1..50}; do
    curl -s http://localhost:8080/test.txt > /dev/null &
done
wait
echo "Concurrent test completed"

# Тест 3: Длительные соединения (используем ab для нагрузочного тестирования)
if command -v ab &> /dev/null; then
    echo "=== Apache Bench test ==="
    ab -n 1000 -c 50 http://localhost:8080/
else
    echo "Apache Bench not found, skipping benchmark"
fi

# Тест 4: Проверка ошибок
echo "=== Error handling test ==="
curl -s http://localhost:8080/nonexistent.html > /dev/null
curl -s -X POST http://localhost:8080/ > /dev/null

# Остановка сервера
kill $SERVER_PID
wait $SERVER_PID 2>/dev/null

echo "Load testing completed"