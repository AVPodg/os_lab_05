#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "child.h"

#define BUFFER_SIZE 1024

volatile sig_atomic_t child_failed = 0; // Значения: 0 - ошибок нет, 1 - дочерний процесс сообщил об ошибке

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

static void main_handle_error(const char *msg) {
    write_string(STDERR_FILENO, "Ошибка: ");
    write_string(STDERR_FILENO, msg);
    write_string(STDERR_FILENO, "\n");
    exit(EXIT_FAILURE);
}

void handle_child_signal(int sig) {
    (void)sig;
    child_failed = 1;
    write_string(STDOUT_FILENO, "Родитель: Получен сигнал об ошибке\n");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        write_string(STDERR_FILENO, "Использование: ");
        write_string(STDERR_FILENO, argv[0]);
        write_string(STDERR_FILENO, " <файл_с_командами>\n");
        exit(EXIT_FAILURE);
    }

    if (signal(SIGUSR1, handle_child_signal) == SIG_ERR) { 
        // SIG_ERR - константа, означающая что установка не удалась
        // signal() - функция установки обработчика
        main_handle_error("Ошибка установки обработчика сигнала");
    }

    int file_fd = open(argv[1], O_RDONLY);
    if (file_fd == -1) {
        main_handle_error("Ошибка открытия файла");
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {  // pipe() создает однонаправленный канал связи
        close(file_fd);
        main_handle_error("Ошибка создания pipe");
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(file_fd);
        close(pipefd[0]);
        close(pipefd[1]);
        main_handle_error("Ошибка создания процесса");
    }

    if (pid == 0) {
        close(pipefd[1]);
        
        if (dup2(pipefd[0], STDIN_FILENO) == -1) {
            // Здесь: делаем так чтобы STDIN_FILENO (0) указывал на pipefd[0]
            // Теперь когда дочерний процесс читает из stdin (0), 
            // он фактически читает из pipe
            close(pipefd[0]);
            child_handle_error("Ошибка перенаправления stdin");
        }
        close(pipefd[0]);
        
        run_child_process();
        exit(EXIT_SUCCESS);
    } else {
        // pid содержит ID дочернего процесса (положительное число)
        close(pipefd[0]);
        
        write_string(STDOUT_FILENO, "Родительский процесс PID: ");
        write_int(STDOUT_FILENO, getpid());
        write_string(STDOUT_FILENO, "\nДочерний процесс PID: ");
        write_int(STDOUT_FILENO, pid);
        write_string(STDOUT_FILENO, "\nФайл: ");
        write_string(STDOUT_FILENO, argv[1]);
        write_string(STDOUT_FILENO, "\n\n");

        char buffer[BUFFER_SIZE];  
        
        int line_number = 0; 
        ssize_t bytes_read; 
        
        // Цикл чтения файла порциями:
        while ((bytes_read = read(file_fd, buffer, sizeof(buffer) - 1)) > 0) {
            // - file_fd - дескриптор открытого файла
            // - buffer - указатель на буфер для данных
            // - sizeof(buffer) - 1 = 1023 байт (оставляем место для '\0')
            
            // bytes_read - количество фактически прочитанных байт:
            // > 0 - успешно прочитано N байт
            // = 0 - достигнут конец файла (EOF)
            // = -1 - ошибка чтения
            
            buffer[bytes_read] = '\0';  
            // Добавляем нулевой байт в конец данных
            
            char *line = buffer;    // Указатель на текущую обрабатываемую строку
            char *newline;          // Указатель на символ новой строки
            
            // Разбиваем буфер на отдельные строки по символу '\n'
            while ((newline = strchr(line, '\n')) != NULL) {
                // strchr(line, '\n') ищет первое вхождение '\n' в строке
                // Возвращает указатель на найденный '\n' или NULL если не найден
                
                *newline = '\0';  
                // Заменяем символ '\n' на '\0' (нулевой байт)
                
                line_number++;
                
                if (strlen(line) == 0) {
                    line = newline + 1; 
                    continue;  
                }
                
                if (child_failed) {
                    write_string(STDOUT_FILENO, "Родитель: Остановка из-за ошибки\n");
                    break; 
                }
                
                write_string(STDOUT_FILENO, "Родитель: Отправка команды ");
                write_int(STDOUT_FILENO, line_number);
                write_string(STDOUT_FILENO, ": ");
                write_string(STDOUT_FILENO, line);
                write_string(STDOUT_FILENO, "\n");
                
                write(pipefd[1], line, strlen(line));  
                
                write(pipefd[1], "\n", 1);  
                
                sleep(1);  
                
                line = newline + 1;  
                // Перемещаем указатель на следующую строку в буфере
            }
            
            if (child_failed) break;  
        }
        
        close(pipefd[1]);  
        // Закрываем конец pipe для записи
        // Дочерний процесс получит EOF (end-of-file) при чтении из stdin
        
        close(file_fd);  
            int status;  
        
        waitpid(pid, &status, 0);  
        // waitpid() - ожидание завершения конкретного дочернего процесса
        // Параметры:
        //   pid - ID ожидаемого процесса
        //   &status - указатель куда записать статус
        //   0 - флаги ожидания (0 = обычное блокирующее ожидание)
        
        // waitpid() БЛОКИРУЕТ выполнение родителя пока дочерний процесс не завершится
        
        if (WIFEXITED(status)) {              
            write_string(STDOUT_FILENO, "Родитель: Дочерний процесс завершен с кодом: ");
            write_int(STDOUT_FILENO, WEXITSTATUS(status));  
            write_string(STDOUT_FILENO, "\n");
        }
        
        write_string(STDOUT_FILENO, "Родитель: Завершение работы\n");
    }

    return 0;  
}