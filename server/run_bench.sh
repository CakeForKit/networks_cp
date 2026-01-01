#!/bin/bash

RUNNER="/home/kathrine/vuz/networks_cp/server/venv/bin/python"
SCRIPT="/home/kathrine/vuz/networks_cp/server/bench/wrk_runner.py"

if [ ! -f "$RUNNER" ]; then
    echo "Ошибка: Python не найден по пути $RUNNER"
    exit 1
fi

if [ ! -f "$SCRIPT" ]; then
    echo "Ошибка: Скрипт не найден по пути $SCRIPT"
    exit 1
fi


"$RUNNER" "$SCRIPT" -c 5 10 -d 5 -r 1 -o wrk_results.csv

echo "Тестирование завершено. Результаты в ab_results.csv"