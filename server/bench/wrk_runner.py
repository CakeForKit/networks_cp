import subprocess, argparse, csv, time, signal, socket, random, re
from pathlib import Path
from statistics import mean
import json
from typing import Dict, List, Optional


def start_server(server_cmd: str = "./server.exe") -> tuple[subprocess.Popen, int]:
    """Запускает сервер на свободном порту (порт 0)"""
    print("  Запуск сервера на свободном порту...")
    
    # Создаем временный сокет чтобы получить свободный порт
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    
    cmd = [server_cmd, str(port)] if server_cmd.endswith(".exe") else [server_cmd, str(port)]
    p = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    
    # Ждем пока сервер запустится
    for _ in range(30):  # 30 попыток по 0.1 сек = 3 секунды максимум
        with socket.socket() as s:
            s.settimeout(0.1)
            try:
                if s.connect_ex(("127.0.0.1", port)) == 0:
                    print(f"    Сервер запущен на порту {port} (PID={p.pid})")
                    return p, port
            except:
                pass
        
        # Проверяем не завершился ли процесс с ошибкой
        if p.poll() is not None:
            stderr = p.stderr.read() if p.stderr else ""
            print(f"    Сервер завершился с ошибкой: {stderr[:200]}")
            raise RuntimeError(f"Сервер не запустился, код возврата: {p.returncode}")
        
        time.sleep(0.1)
    
    p.terminate()
    raise RuntimeError(f"Сервер не запустился за 3 секунды на порту {port}")

def stop_server(p: subprocess.Popen):
    print("  Остановка сервера...")
    p.terminate()
    try:
        p.wait(timeout=2)
        print("    Сервер остановлен корректно")
    except subprocess.TimeoutExpired:
        print("    Принудительная остановка...")
        p.kill()
        p.wait()
        print("    Сервер остановлен принудительно")

def parse_wrk_output(text: str) -> Optional[Dict]:
    result = {}
    
    rps_match = re.search(r"Requests/sec:\s*([\d\.]+)", text)
    if rps_match:
        result["rps"] = float(rps_match.group(1))
    
    transfer_match = re.search(r"Transfer/sec:\s*([\d\.]+)([KM]?B)", text)
    if transfer_match:
        value = float(transfer_match.group(1))
        unit = transfer_match.group(2)
        if unit == "MB":
            value *= 1024
        elif unit == "GB":
            value *= 1024 * 1024
        result["transfer"] = value
    
    errors_match = re.search(r"Socket errors:\s*connect (\d+),\s*read (\d+),\s*write (\d+),\s*timeout (\d+)", text)
    if errors_match:
        result["errors_connect"] = int(errors_match.group(1))
        result["errors_read"] = int(errors_match.group(2))
        result["errors_write"] = int(errors_match.group(3))
        result["errors_timeout"] = int(errors_match.group(4))
    
    latency_patterns = {
        "latency_50": r"50%\s*([\d\.]+)([mun]?s)",
        "latency_75": r"75%\s*([\d\.]+)([mun]?s)",
        "latency_90": r"90%\s*([\d\.]+)([mun]?s)",
        "latency_99": r"99%\s*([\d\.]+)([mun]?s)",
    }
    
    for key, pattern in latency_patterns.items():
        match = re.search(pattern, text)
        if match:
            value = float(match.group(1))
            unit = match.group(2)
            if unit == "s":
                value *= 1000
            elif unit == "us":
                value /= 1000
            result[key] = value

    avg_lat_match = re.search(r"Latency\s*([\d\.]+)([mun]?s)\s*([\d\.]+)([mun]?s)", text)
    if avg_lat_match:
        value = float(avg_lat_match.group(1))
        unit = avg_lat_match.group(2)
        if unit == "s":
            value *= 1000
        elif unit == "us":
            value /= 1000
        result["latency_avg"] = value
    

    total_req_match = re.search(r"(\d+)\s+requests in ([\d\.]+)([mun]?s)", text)
    if total_req_match:
        result["total_requests"] = int(total_req_match.group(1))
        result["duration"] = float(total_req_match.group(2))
    
    return result if result else None

def run_wrk(url: str, threads: int, connections: int, duration: int = 10) -> Optional[Dict]:
    print(f"    Запуск wrk: threads={threads}, connections={connections}, duration={duration}s")
    cmd = [
        "wrk",
        "-t", str(threads),
        "-c", str(connections),
        "-d", f"{duration}s",
        "--latency",
        url
    ]
    
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=duration + 5  # Таймаут чуть больше длительности теста
        )
        if result.returncode != 0:
            print(f"    Ошибка wrk (код {result.returncode}): {result.stderr[:200]}")
            return None
        
        print(f"    wrk завершился успешно")
        return parse_wrk_output(result.stdout)
        
    except subprocess.TimeoutExpired:
        print("    wrk превысил время выполнения")
        return None
    except FileNotFoundError:
        print("    Ошибка: wrk не найден. Установите его: sudo apt-get install wrk")
        return None


def calculate_threads(connections: int) -> int:
    """Рассчитывает оптимальное количество потоков для wrk"""
    # Оптимальное соотношение: примерно 1 поток на 100 соединений, но не менее 2 и не более 12
    threads = max(2, min(connections // 100, 12))
    return threads

def main():
    # CONNECTIONS = [i for i in range(5, 100, 2)] + \
    #     [i for i in range(100, 200, 5)] + \
    #     [i for i in range(200, 1000, 30)]
    CONNECTIONS = [i for i in range(5, 500, 10)] + \
        [i for i in range(500, 1500, 50)]
        
    print(f"CONNECTOPNS = {len(CONNECTIONS)}")
    DURATION = 20  # sec
    REPEATS = 5
    parser = argparse.ArgumentParser(description="Нагрузочное тестирование с помощью wrk")
    parser.add_argument("-n", "--requests", type=int, default=0,
                       help="Количество запросов (0 = использовать длительность теста)")
    parser.add_argument("-c", "--connections", nargs="+", type=int, default=CONNECTIONS,
                       help="Количество соединений для тестирования (можно несколько через пробел)")
    parser.add_argument("-d", "--duration", type=int, default=DURATION,
                       help="Длительность каждого теста в секундах")
    parser.add_argument("-r", "--repeats", type=int, default=REPEATS,
                       help="Количество повторений для каждого уровня соединений")
    parser.add_argument("--server", default="./server.exe",
                       help="Путь к исполняемому файлу сервера")
    parser.add_argument("--threads", type=int, default=0,
                       help="Количество потоков wrk (0 = автоматический расчет)")
    parser.add_argument("-o", "--output", default="wrk_results.csv",
                       help="Файл для сохранения результатов")
    parser.add_argument("--url-path", default="/",
                       help="Путь для тестирования на сервере")
    
    args = parser.parse_args()

    
    print(f"\n=== Нагрузочное тестирование с помощью wrk ===")
    print(f"Сервер: {args.server}")
    print(f"Длительность теста: {args.duration} секунд")
    print(f"Повторений: {args.repeats}")
    print(f"Уровни соединений: {args.connections}")
    print(f"Результаты будут сохранены в: {args.output}")
    print("=" * 60)
    
    # Заголовки CSV файла
    headers = [
        "connections", "threads", "rps", "latency_avg", 
        # "latency_50", "latency_75", "latency_90", "latency_99", 
        "transfer", 
        "errors_timeout"
        #, "total_requests"
    ]
    '''
    connections - количество соединений ВСЕГО 
    threads  - количество потоков wrk
    rps (Requests per second) - запросов в секунду
    latency_avg - средняя задержка
    transfer - скорость передачи данных
    errors_timeout - Сколько запросов превысили время ожидания
    '''
    
    with open(args.output, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(headers)
        
        for connections in args.connections:
            print(f"\n=== Тестирование с connections={connections} ===")
            threads = args.threads if args.threads > 0 else calculate_threads(connections)
            print(f"  Используется потоков: {threads}")

            all_results = []
            for repeat in range(1, args.repeats + 1):
                print(f"\n  --- Прогон {repeat}/{args.repeats} ---")
                
                # port = free_port()
                
                # print(f"    URL: {url}")
                try:
                    server_process, port = start_server(args.server)
                    url = f"http://127.0.0.1:{port}{args.url_path}"
                    result = run_wrk(url, threads, connections, args.duration)
                    stop_server(server_process)
                    
                    if result:
                        print(f"    Результат: RPS={result.get('rps', 0):.1f}, "
                              f"Latency Avg={result.get('latency_avg', 0):.2f}ms")
                        all_results.append(result)
                    else:
                        print("    Прогон завершился ошибкой.")
                        
                except Exception as e:
                    print(f"    Ошибка в прогоне: {e}")
                    continue
            
            if not all_results:
                print(f"  Нет успешных прогонов для connections={connections} — пропуск.")
                continue
            
            avg_results = {}
            for key in all_results[0].keys():
                values = [r.get(key, 0) for r in all_results]
                avg_results[key] = mean(values) if values else 0
            
            
            row = [
                connections,
                threads,
                "{:.5f}".format(avg_results.get("rps", 0)),
                "{:.5f}".format(avg_results.get("latency_avg", 0)),
                # avg_results.get("latency_50", 0),
                # avg_results.get("latency_75", 0),
                # avg_results.get("latency_90", 0),
                # avg_results.get("latency_99", 0),
                "{:.5f}".format(avg_results.get("transfer", 0)),
                avg_results.get("errors_timeout", 0)
                # avg_results.get("total_requests", 0)
            ]
            writer.writerow(row)
            
    print(f"\n=== Тестирование завершено ===")
    print(f"Результаты сохранены в {args.output}")


if __name__ == "__main__":
    main()