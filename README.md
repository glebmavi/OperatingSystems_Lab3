# Операционные системы. Лабораторная работа 3

## Вариант
- ОС: Linux
- ioctl: vm areas

## Задание
Разработать комплекс программ на пользовательском уровне и уровне ярда, который собирает информацию на стороне ядра и передает
информацию на уровень пользователя, и выводит ее в удобном для чтения человеком виде. Программа на уровне пользователя получает
на вход аргумент(ы) командной строки (не адрес!), позволяющие идентифицировать из системных таблиц необходимый путь до целевой
структуры, осуществляет передачу на уровень ядра, получает информацию из данной структуры и распечатывает структуру в стандартный вывод.
Загружаемый модуль ядра принимает запрос через указанный в задании интерфейс, определяет путь до целевой структуры по переданному
запросу и возвращает результат на уровень пользователя.

Интерфейс передачи может быть один из следующих:
1. syscall - интерфейс системных вызовов.
2. ioctl - передача параметров через управляющий вызов к файлу/устройству.
3. procfs - файловая система /proc, передача параметров через запись в файл.
4. debugfs - отладочная файловая система /sys/kernel/debug, передача параметров через запись в файл.

Целевая структура может быть задана двумя способами:
1. Именем структуры в заголовочных файлах Linux
2. Файлом в каталоге /proc. В этом случае необходимо определить целевую структуру по пути файла в /proc и выводимым данным.

Интерфейс передачи между программой пользователя и ядром: ioctl - передача параметров через управляющий вызов к файлу/устройству.
Целевая структура: Именем структуры в заголовочных файлах Linux.

## Решение

### Общая схема взаимодействия

``` maybe remove
1. Пользовательская программа:
   - Принимает на вход PID процесса.
   - Открывает специальный файл-устройство (например, /dev/vma_info).
   - Формирует команду управления (ioctl) и передаёт в ядро PID.
   - Получает результат из ядра (данные обо всех vm_area_struct).
   - Отображает результат (печатает на стандартный вывод).
2. Загружаемый модуль ядра:
   - Регистрирует символьное устройство, обрабатывающее вызовы ioctl.
   - По запросу ioctl получает PID из пользовательского пространства.
   - Находит соответствующую задачу в таблице процессов (например, через find_get_task_by_vpid или pid_task).
   - Для найденной задачи получает структуру mm_struct, далее итерируется по связанному списку VMA (vm_area_struct).
   - Составляет удобную для передачи «промежуточную» структуру и копирует её в пользовательское пространство (через copy_to_user).
3. Интерфейс ioctl:
   - Используется как канал передачи данных между пользовательской программой и модулем ядра.
   - В заголовочных файлах определяется номер команды и структура данных, которую нужно передавать.
```

Комплекс программ (модуль ядра + утилита на уровне пользователя):

- Принимает на вход PID из аргументов командной строки в пользовательской программе.
- Посылает этот PID в модуль ядра через ioctl.
- Модуль ядра собирает информацию о VMA (vm_area_struct) целевого процесса.
- Модуль ядра возвращает информацию обратно в пользовательскую программу.
- Пользовательская программа выводит результат в удобном для чтения виде.