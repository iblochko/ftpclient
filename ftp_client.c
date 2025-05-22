#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/stat.h>

#define BUFFER_SIZE 1024
#define CMD_SIZE 256
#define MAX_PATH 512

typedef struct {
    int control_socket;
    int data_socket;
    char server[256];
    int port;
    char username[256];
    char password[256];
    int passive_mode;
    char current_dir[512];  // Добавлено для отслеживания текущего каталога
} ftp_client_t;

int ftp_pwd(ftp_client_t *client);

// Функция для чтения ответа от FTP сервера
int read_response(int socket, char *buffer, int size) {
    int bytes_read = recv(socket, buffer, size - 1, 0);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("Server: %s", buffer);
    }
    return bytes_read;
}

// Функция для отправки команды FTP серверу
int send_command(ftp_client_t *client, const char *command) {
    char cmd[CMD_SIZE];
    snprintf(cmd, sizeof(cmd), "%s\r\n", command);

    printf("Client: %s", cmd);
    return send(client->control_socket, cmd, strlen(cmd), 0);
}

// Установка соединения с FTP сервером
int ftp_connect(ftp_client_t *client, const char *server, int port) {
    struct sockaddr_in server_addr;
    struct hostent *host_entry;
    char buffer[BUFFER_SIZE];

    // Создание сокета
    client->control_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client->control_socket < 0) {
        perror("Socket creation failed");
        return -1;
    }

    // Получение IP адреса сервера
    host_entry = gethostbyname(server);
    if (!host_entry) {
        fprintf(stderr, "Failed to resolve hostname: %s\n", server);
        return -1;
    }

    // Настройка адреса сервера
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);

    // Подключение к серверу
    if (connect(client->control_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        return -1;
    }

    strcpy(client->server, server);
    client->port = port;
    strcpy(client->current_dir, "/");  // Инициализация текущего каталога

    // Чтение приветственного сообщения
    read_response(client->control_socket, buffer, sizeof(buffer));

    return 0;
}


// Аутентификация на FTP сервере
int ftp_login(ftp_client_t *client, const char *username, const char *password) {
    char buffer[BUFFER_SIZE];
    char command[CMD_SIZE];

    // Отправка имени пользователя
    snprintf(command, sizeof(command), "USER %s", username);
    send_command(client, command);
    read_response(client->control_socket, buffer, sizeof(buffer));

    // Отправка пароля
    snprintf(command, sizeof(command), "PASS %s", password);
    send_command(client, command);
    read_response(client->control_socket, buffer, sizeof(buffer));

    strcpy(client->username, username);
    strcpy(client->password, password);

    // После успешной авторизации получаем текущий каталог
    if (strncmp(buffer, "230", 3) == 0) {
        ftp_pwd(client);  // Получаем текущий каталог
        return 0;
    }

    return -1;
}

// Получение текущего рабочего каталога
int ftp_pwd(ftp_client_t *client) {
    char buffer[BUFFER_SIZE];

    send_command(client, "PWD");
    read_response(client->control_socket, buffer, sizeof(buffer));

    if (strncmp(buffer, "257", 3) == 0) {
        // Парсинг ответа PWD для извлечения пути
        char *start = strchr(buffer, '"');
        if (start) {
            start++;
            char *end = strchr(start, '"');
            if (end) {
                *end = '\0';
                strcpy(client->current_dir, start);
            }
        }
        return 0;
    }

    return -1;
}

// Смена рабочего каталога
int ftp_cwd(ftp_client_t *client, const char *directory) {
    char buffer[BUFFER_SIZE];
    char command[CMD_SIZE];

    snprintf(command, sizeof(command), "CWD %s", directory);
    send_command(client, command);
    read_response(client->control_socket, buffer, sizeof(buffer));

    if (strncmp(buffer, "250", 3) == 0) {
        // Обновляем локальное представление текущего каталога
        if (strcmp(directory, "..") == 0) {
            // Переход в родительский каталог
            char *last_slash = strrchr(client->current_dir, '/');
            if (last_slash && last_slash != client->current_dir) {
                *last_slash = '\0';
            } else if (last_slash == client->current_dir) {
                strcpy(client->current_dir, "/");
            }
        } else if (directory[0] == '/') {
            // Абсолютный путь
            strcpy(client->current_dir, directory);
        } else {
            // Относительный путь
            if (strcmp(client->current_dir, "/") != 0) {
                strcat(client->current_dir, "/");
            }
            strcat(client->current_dir, directory);
        }

        printf("Changed to directory: %s\n", client->current_dir);
        return 0;
    }

    return -1;
}

// Переход в пассивный режим
int ftp_passive_mode(ftp_client_t *client) {
    char buffer[BUFFER_SIZE];
    char *start, *end;
    int ip[4], port[2];
    struct sockaddr_in data_addr;

    send_command(client, "PASV");
    read_response(client->control_socket, buffer, sizeof(buffer));

    if (strncmp(buffer, "227", 3) != 0) {
        return -1;
    }

    // Парсинг IP адреса и порта из ответа PASV
    start = strchr(buffer, '(');
    if (!start) return -1;
    start++;

    sscanf(start, "%d,%d,%d,%d,%d,%d", &ip[0], &ip[1], &ip[2], &ip[3], &port[0], &port[1]);

    // Создание data соединения
    client->data_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client->data_socket < 0) {
        perror("Data socket creation failed");
        return -1;
    }

    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(port[0] * 256 + port[1]);
    data_addr.sin_addr.s_addr = htonl((ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3]);

    if (connect(client->data_socket, (struct sockaddr*)&data_addr, sizeof(data_addr)) < 0) {
        perror("Data connection failed");
        close(client->data_socket);
        return -1;
    }

    client->passive_mode = 1;
    return 0;
}

// Создание tar архива из каталога
int create_tar_archive(const char *directory, const char *archive_name) {
    char command[CMD_SIZE * 2];

    printf("Creating tar archive: %s from directory: %s\n", archive_name, directory);

    snprintf(command, sizeof(command), "tar -czf %s -C %s .", archive_name, directory);

    if (system(command) != 0) {
        fprintf(stderr, "Failed to create tar archive\n");
        return -1;
    }

    return 0;
}

// Извлечение tar архива
int extract_tar_archive(const char *archive_name, const char *destination) {
    char command[CMD_SIZE * 2];

    printf("Extracting tar archive: %s to directory: %s\n", archive_name, destination);

    // Создание целевого каталога если он не существует
    mkdir(destination, 0755);

    snprintf(command, sizeof(command), "tar -xzf %s -C %s", archive_name, destination);

    if (system(command) != 0) {
        fprintf(stderr, "Failed to extract tar archive\n");
        return -1;
    }

    return 0;
}

// Отправка файла на FTP сервер
int ftp_upload_file(ftp_client_t *client, const char *local_file, const char *remote_file) {
    char buffer[BUFFER_SIZE];
    char command[CMD_SIZE];
    FILE *file;
    int bytes_read, bytes_sent;

    // Переход в пассивный режим
    if (ftp_passive_mode(client) < 0) {
        return -1;
    }

    // Установка бинарного режима
    send_command(client, "TYPE I");
    read_response(client->control_socket, buffer, sizeof(buffer));

    // Команда STOR
    snprintf(command, sizeof(command), "STOR %s", remote_file);
    send_command(client, command);
    read_response(client->control_socket, buffer, sizeof(buffer));

    if (strncmp(buffer, "150", 3) != 0 && strncmp(buffer, "125", 3) != 0) {
        close(client->data_socket);
        return -1;
    }

    // Открытие локального файла
    file = fopen(local_file, "rb");
    if (!file) {
        perror("Failed to open local file");
        close(client->data_socket);
        return -1;
    }

    // Отправка данных
    printf("Uploading file: %s\n", local_file);
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        bytes_sent = send(client->data_socket, buffer, bytes_read, 0);
        if (bytes_sent < 0) {
            perror("Failed to send data");
            break;
        }
        printf(".");
        fflush(stdout);
    }
    printf("\n");

    fclose(file);
    close(client->data_socket);

    // Чтение финального ответа
    read_response(client->control_socket, buffer, sizeof(buffer));

    return 0;
}

// Получение файла с FTP сервера
int ftp_download_file(ftp_client_t *client, const char *remote_file, const char *local_file) {
    char buffer[BUFFER_SIZE];
    char command[CMD_SIZE];
    FILE *file;
    int bytes_received;

    // Переход в пассивный режим
    if (ftp_passive_mode(client) < 0) {
        return -1;
    }

    // Установка бинарного режима
    send_command(client, "TYPE I");
    read_response(client->control_socket, buffer, sizeof(buffer));

    // Команда RETR
    snprintf(command, sizeof(command), "RETR %s", remote_file);
    send_command(client, command);
    read_response(client->control_socket, buffer, sizeof(buffer));

    if (strncmp(buffer, "150", 3) != 0 && strncmp(buffer, "125", 3) != 0) {
        close(client->data_socket);
        return -1;
    }

    // Создание локального файла
    file = fopen(local_file, "wb");
    if (!file) {
        perror("Failed to create local file");
        close(client->data_socket);
        return -1;
    }

    // Получение данных
    printf("Downloading file: %s\n", remote_file);
    while ((bytes_received = recv(client->data_socket, buffer, sizeof(buffer), 0)) > 0) {
        fwrite(buffer, 1, bytes_received, file);
        printf(".");
        fflush(stdout);
    }
    printf("\n");

    fclose(file);
    close(client->data_socket);

    // Чтение финального ответа
    read_response(client->control_socket, buffer, sizeof(buffer));

    return 0;
}

// Отправка архивированного каталога
int ftp_upload_directory(ftp_client_t *client, const char *local_dir, const char *remote_name) {
    char archive_name[MAX_PATH];
    int result;

    // Создание временного имени архива
    snprintf(archive_name, sizeof(archive_name), "/tmp/%s.tar.gz", remote_name);

    // Создание архива
    if (create_tar_archive(local_dir, archive_name) < 0) {
        return -1;
    }

    // Отправка архива
    result = ftp_upload_file(client, archive_name, remote_name);

    // Удаление временного архива
    unlink(archive_name);

    return result;
}

// Получение и извлечение архивированного каталога
int ftp_download_directory(ftp_client_t *client, const char *remote_name, const char *local_dir) {
    char archive_name[MAX_PATH];
    int result;

    // Создание временного имени архива
    snprintf(archive_name, sizeof(archive_name), "/tmp/%s", remote_name);

    // Скачивание архива
    result = ftp_download_file(client, remote_name, archive_name);
    if (result < 0) {
        return -1;
    }

    // Извлечение архива
    result = extract_tar_archive(archive_name, local_dir);

    // Удаление временного архива
    unlink(archive_name);

    return result;
}

// Список файлов на сервере
int ftp_list_files(ftp_client_t *client) {
    char buffer[BUFFER_SIZE];
    int bytes_received;

    // Переход в пассивный режим
    if (ftp_passive_mode(client) < 0) {
        return -1;
    }

    // Команда LIST
    send_command(client, "LIST");
    read_response(client->control_socket, buffer, sizeof(buffer));

    if (strncmp(buffer, "150", 3) != 0 && strncmp(buffer, "125", 3) != 0) {
        close(client->data_socket);
        return -1;
    }

    // Получение списка файлов
    printf("\nFile listing for %s:\n", client->current_dir);
    printf("----------------------------------------\n");
    while ((bytes_received = recv(client->data_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("%s", buffer);
    }
    printf("----------------------------------------\n");

    close(client->data_socket);

    // Чтение финального ответа
    read_response(client->control_socket, buffer, sizeof(buffer));

    return 0;
}

// Закрытие FTP соединения
void ftp_disconnect(ftp_client_t *client) {
    char buffer[BUFFER_SIZE];

    send_command(client, "QUIT");
    read_response(client->control_socket, buffer, sizeof(buffer));

    close(client->control_socket);
}

// Функция для отображения помощи
void print_help() {
    printf("\nFTP Client Commands:\n");
    printf("----------------------------------------\n");
    printf("connect <server> <port>     - Connect to FTP server\n");
    printf("login <username> <password> - Login to FTP server\n");
    printf("pwd                         - Show current directory\n");
    printf("cd <directory>              - Change directory\n");
    printf("list                        - List files on server\n");
    printf("upload <local_file> <remote_file> - Upload file\n");
    printf("download <remote_file> <local_file> - Download file\n");
    printf("upload_dir <local_dir> <remote_name> - Upload directory as archive\n");
    printf("download_dir <remote_name> <local_dir> - Download and extract archive\n");
    printf("quit                        - Disconnect and exit\n");
    printf("help                        - Show this help\n");
    printf("----------------------------------------\n");
}

// Функция для отображения промпта с текущим каталогом
void print_prompt(ftp_client_t *client, int logged_in) {
    if (logged_in) {
        printf("ftp:%s> ", client->current_dir);
    } else {
        printf("ftp> ");
    }
    fflush(stdout);
}

int main() {
    ftp_client_t client;
    char command[CMD_SIZE];
    char arg1[256], arg2[256], arg3[256];
    int connected = 0, logged_in = 0;

    memset(&client, 0, sizeof(client));

    printf("FTP Client with Directory Navigation Support\n");
    printf("Type 'help' for available commands\n\n");

    while (1) {
        print_prompt(&client, logged_in);

        if (!fgets(command, sizeof(command), stdin)) {
            break;
        }

        // Удаление символа новой строки
        command[strcspn(command, "\n")] = '\0';

        // Парсинг команды
        memset(arg1, 0, sizeof(arg1));
        memset(arg2, 0, sizeof(arg2));
        memset(arg3, 0, sizeof(arg3));
        int args = sscanf(command, "%s %s %s", arg1, arg2, arg3);

        if (args == 0) {
            continue;
        }

        if (strcmp(arg1, "help") == 0) {
            print_help();
        }
        else if (strcmp(arg1, "connect") == 0) {
            if (args < 3) {
                printf("Usage: connect <server> <port>\n");
                continue;
            }

            if (ftp_connect(&client, arg2, atoi(arg3)) == 0) {
                printf("Connected to %s:%s\n", arg2, arg3);
                connected = 1;
            } else {
                printf("Failed to connect to server\n");
            }
        }
        else if (strcmp(arg1, "login") == 0) {
            if (!connected) {
                printf("Not connected to server. Use 'connect' first.\n");
                continue;
            }

            if (args < 3) {
                printf("Usage: login <username> <password>\n");
                continue;
            }

            if (ftp_login(&client, arg2, arg3) == 0) {
                printf("Logged in successfully\n");
                logged_in = 1;
            } else {
                printf("Login failed\n");
            }
        }
        else if (strcmp(arg1, "pwd") == 0) {
            if (!logged_in) {
                printf("Not logged in. Use 'login' first.\n");
                continue;
            }

            ftp_pwd(&client);
        }
        else if (strcmp(arg1, "cd") == 0) {
            if (!logged_in) {
                printf("Not logged in. Use 'login' first.\n");
                continue;
            }

            if (args < 2) {
                printf("Usage: cd <directory>\n");
                continue;
            }

            if (ftp_cwd(&client, arg2) == 0) {
                printf("Directory changed successfully\n");
            } else {
                printf("Failed to change directory\n");
            }
        }
        else if (strcmp(arg1, "list") == 0) {
            if (!logged_in) {
                printf("Not logged in. Use 'login' first.\n");
                continue;
            }

            ftp_list_files(&client);
        }
        else if (strcmp(arg1, "upload") == 0) {
            if (!logged_in) {
                printf("Not logged in. Use 'login' first.\n");
                continue;
            }

            if (args < 3) {
                printf("Usage: upload <local_file> <remote_file>\n");
                continue;
            }

            if (ftp_upload_file(&client, arg2, arg3) == 0) {
                printf("File uploaded successfully\n");
            } else {
                printf("Upload failed\n");
            }
        }
        else if (strcmp(arg1, "download") == 0) {
            if (!logged_in) {
                printf("Not logged in. Use 'login' first.\n");
                continue;
            }

            if (args < 3) {
                printf("Usage: download <remote_file> <local_file>\n");
                continue;
            }

            if (ftp_download_file(&client, arg2, arg3) == 0) {
                printf("File downloaded successfully\n");
            } else {
                printf("Download failed\n");
            }
        }
        else if (strcmp(arg1, "upload_dir") == 0) {
            if (!logged_in) {
                printf("Not logged in. Use 'login' first.\n");
                continue;
            }

            if (args < 3) {
                printf("Usage: upload_dir <local_directory> <remote_archive_name>\n");
                continue;
            }

            if (ftp_upload_directory(&client, arg2, arg3) == 0) {
                printf("Directory uploaded successfully as archive\n");
            } else {
                printf("Directory upload failed\n");
            }
        }
        else if (strcmp(arg1, "download_dir") == 0) {
            if (!logged_in) {
                printf("Not logged in. Use 'login' first.\n");
                continue;
            }

            if (args < 3) {
                printf("Usage: download_dir <remote_archive_name> <local_directory>\n");
                continue;
            }

            if (ftp_download_directory(&client, arg2, arg3) == 0) {
                printf("Archive downloaded and extracted successfully\n");
            } else {
                printf("Directory download failed\n");
            }
        }
        else if (strcmp(arg1, "quit") == 0) {
            if (connected) {
                ftp_disconnect(&client);
            }
            break;
        }
        else {
            printf("Unknown command: %s. Type 'help' for available commands.\n", arg1);
        }
    }

    printf("\nGoodbye!\n");
    return 0;
}