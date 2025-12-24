#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include "child.h"  // ПОДКЛЮЧАЕМ В САМОМ НАЧАЛЕ!

#define BUFFER_SIZE 1024

// Сначала объявляем вспомогательные функции, которые используют child_handle_error
static void notify_parent(void);

// Статические вспомогательные функции
static void write_string(int fd, const char *str) {
    write(fd, str, strlen(str));
}

static void write_int(int fd, int num) {
    char buffer[32];
    int i = 0;
    int is_negative = 0;
    
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }
    
    do {
        buffer[i++] = '0' + (num % 10);
        num /= 10;
    } while (num > 0);
    
    if (is_negative) {
        buffer[i++] = '-';
    }
    
    for (int j = 0; j < i / 2; j++) {
        char temp = buffer[j];
        buffer[j] = buffer[i - j - 1];
        buffer[i - j - 1] = temp;
    }
    
    write(fd, buffer, i);
}

static char *safe_strdup(const char *str) {
    if (str == NULL) return NULL;
    
    size_t len = strlen(str) + 1;
    char *copy = malloc(len);
    if (copy) {
        memcpy(copy, str, len);
    }
    return copy;
}

// Теперь реализация notify_parent
static void notify_parent(void) {
    if (kill(getppid(), SIGUSR1) == -1) {
        child_handle_error("Ошибка отправки сигнала родителю");
    }
}

// Функция для обработки ошибок - реализация
void child_handle_error(const char *msg) {
    write_string(STDERR_FILENO, "Ошибка: ");
    write_string(STDERR_FILENO, msg);
    write_string(STDERR_FILENO, "\n");
    _exit(EXIT_FAILURE);
}

static int parse_numbers(char *line, int **numbers) {
    char *token;
    int count = 0;
    int capacity = 10;
    
    char *line_copy = safe_strdup(line);
    if (!line_copy) {
        child_handle_error("Ошибка выделения памяти для копии строки");
    }
    
    *numbers = malloc(capacity * sizeof(int));
    if (!*numbers) {
        free(line_copy);
        child_handle_error("Ошибка выделения памяти для чисел");
    }

    token = strtok(line_copy, " \t\n");
    while (token != NULL) {
        if (count >= capacity) {
            capacity *= 2;
            int *temp = realloc(*numbers, capacity * sizeof(int));
            if (!temp) {
                free(*numbers);
                free(line_copy);
                child_handle_error("Ошибка перевыделения памяти");
            }
            *numbers = temp;
        }

        char *endptr;
        long num = strtol(token, &endptr, 10);
        
        if (*endptr != '\0' || num > INT_MAX || num < INT_MIN) {
            write_string(STDOUT_FILENO, "Дочерний: Ошибка парсинга числа: '");
            write_string(STDOUT_FILENO, token);
            write_string(STDOUT_FILENO, "'\n");
            free(*numbers);
            free(line_copy);
            return -1;
        }
        
        (*numbers)[count++] = (int)num;
        token = strtok(NULL, " \t\n");
    }

    free(line_copy);
    return count;
}

void run_child_process(void) {
    write_string(STDOUT_FILENO, "Дочерний процесс PID: ");
    write_int(STDOUT_FILENO, getpid());
    write_string(STDOUT_FILENO, " (родитель: ");
    write_int(STDOUT_FILENO, getppid());
    write_string(STDOUT_FILENO, ")\n");
    
    char buffer[BUFFER_SIZE];
    int command_number = 0;
    ssize_t bytes_read;
    
    while ((bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        char *line = buffer;
        char *newline;
        
        while ((newline = strchr(line, '\n')) != NULL) {
            *newline = '\0';
            command_number++;
            
            if (strlen(line) == 0) {
                line = newline + 1;
                continue;
            }
            
            write_string(STDOUT_FILENO, "Дочерний: Получена команда ");
            write_int(STDOUT_FILENO, command_number);
            write_string(STDOUT_FILENO, ": ");
            write_string(STDOUT_FILENO, line);
            write_string(STDOUT_FILENO, "\n");
            
            int *numbers = NULL;
            int count = parse_numbers(line, &numbers);
            
            if (count < 0) {
                line = newline + 1;
                continue;
            }
            
            if (count < 2) {
                write_string(STDOUT_FILENO, "Дочерний: Нужно минимум 2 числа, получено ");
                write_int(STDOUT_FILENO, count);
                write_string(STDOUT_FILENO, "\n");
                if (numbers) free(numbers);
                line = newline + 1;
                continue;
            }
            
            int dividend = numbers[0];
            write_string(STDOUT_FILENO, "Дочерний: Делимое: ");
            write_int(STDOUT_FILENO, dividend);
            write_string(STDOUT_FILENO, "\n");
            
            int result = dividend;
            int error = 0;
            
            for (int i = 1; i < count; i++) {
                int divisor = numbers[i];
                write_string(STDOUT_FILENO, "Дочерний: Делитель ");
                write_int(STDOUT_FILENO, i);
                write_string(STDOUT_FILENO, ": ");
                write_int(STDOUT_FILENO, divisor);
                write_string(STDOUT_FILENO, "\n");
                
                if (divisor == 0) {
                    write_string(STDOUT_FILENO, "Дочерний: ОШИБКА: деление на ноль!\n");
                    error = 1;
                    break;
                }
                
                result /= divisor;
                write_string(STDOUT_FILENO, "Дочерний: Промежуточный результат: ");
                write_int(STDOUT_FILENO, result);
                write_string(STDOUT_FILENO, "\n");
            }
            
            if (error) {
                write_string(STDOUT_FILENO, "Дочерний: Отправка сигнала родителю\n");
                notify_parent();
                free(numbers);
                _exit(EXIT_FAILURE);
            } else {
                write_string(STDOUT_FILENO, "Дочерний: Результат: ");
                write_int(STDOUT_FILENO, result);
                write_string(STDOUT_FILENO, "\n\n");
            }
            
            free(numbers);
            line = newline + 1;
        }
    }
    
    write_string(STDOUT_FILENO, "Дочерний: Завершение работы\n");
}