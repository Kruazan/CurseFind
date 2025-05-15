#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ncurses.h>
#include <locale.h>

#define BUFFER_SIZE 4096
#define HASH_SIZE 32
#define MAX_PATH_LENGTH 4096
#define MAX_DIRS 1000
#define LOG_CAPACITY 10000

typedef struct FileNode {
    char hash[HASH_SIZE + 1];
    char path[MAX_PATH_LENGTH];
    struct FileNode* next;
} FileNode;

typedef struct DirEntry {
    char name[MAX_PATH_LENGTH];
    char display_name[256];
    int is_dir;
} DirEntry;

FileNode* hashTable = NULL;
WINDOW *main_win, *log_win, *tree_win;
char current_dir[MAX_PATH_LENGTH] = {0};
DirEntry dir_entries[MAX_DIRS];
int dir_count = 0;
int selected_item = 1; // 1 = старт, 2 = выход (0 зарезервировано для дерева)
int cursor_pos = 0;
int tree_focused = 0; // Начинаем с фокуса на кнопках

char log_buffer[LOG_CAPACITY];
int log_length = 0;


void write_in_logWin() {
    if (log_win) {
        werase(log_win);  // Полностью очищаем окно
        box(log_win, 0, 0);  // Рисуем рамку
        mvwprintw(log_win, 0, 2, " Log ");  // Заголовок

        int win_height, win_width;
        getmaxyx(log_win, win_height, win_width);
        
        // Сначала подсчитаем общее количество строк в логе
        int total_lines = 0;
        char *line = log_buffer;
        while (*line) {
            if (strchr(line, '\n')) {
                total_lines++;
                line = strchr(line, '\n') + 1;
            } else {
                total_lines++;
                break;
            }
        }
        
        // Определяем, сколько строк мы можем вывести (высота окна - 2 для рамки)
        int max_display_lines = win_height - 2;
        if (max_display_lines <= 0) return;
        
        // Вычисляем, с какой строки начинать вывод (пропускаем первые строки, если их слишком много)
        int start_line = 0;
        if (total_lines > max_display_lines) {
            start_line = total_lines - max_display_lines;
        }
        
        // Выводим только нужные строки
        int current_line = 0;
        int y = 1;  // Начинаем с первой строки внутри рамки
        line = log_buffer;
        while (y < win_height - 1 && *line) {  // -1 чтобы уместиться в окне
            char *end = strchr(line, '\n');
            
            // Пропускаем строки, которые не нужно выводить
            if (current_line < start_line) {
                if (end) {
                    line = end + 1;
                    current_line++;
                    continue;
                } else {
                    line += strlen(line);
                    current_line++;
                    continue;
                }
            }
            
            if (end) {
                *end = '\0';  // Временно заменяем \n на \0
                mvwprintw(log_win, y, 1, "%.*s", win_width - 2, line);  // Вывод с отступом и обрезкой
                *end = '\n';  // Восстанавливаем \n
                line = end + 1;
            } else {
                mvwprintw(log_win, y, 1, "%.*s", win_width - 2, line);
                line += strlen(line);
            }
            y++;
            current_line++;
        }
    }
}

void write_to_log(const char* format, ...) {
    va_list args;
    va_start(args, format);

    char temp_buf[LOG_CAPACITY];
    int written = vsnprintf(temp_buf, sizeof(temp_buf), format, args);
    
    if (written <= 0) {
        va_end(args);
        return;
    }

    // Добавляем новое сообщение в конец буфера (а не в начало)
    if (log_length + written >= LOG_CAPACITY) {
        // Если не хватает места, удаляем самые старые данные
        int shift = (log_length + written) - (LOG_CAPACITY - 1);
        memmove(log_buffer, log_buffer + shift, log_length - shift);
        log_length -= shift;
    }

    // Добавляем в конец буфера
    strncpy(log_buffer + log_length, temp_buf, written);
    log_length += written;
    log_buffer[log_length] = '\0';

    write_in_logWin();
    va_end(args);
}

void read_log_from_file() {
    const char *filename = "duplicate_log.txt";
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Не удалось открыть файл логов");
        return;
    }

    char *buffer = malloc(LOG_CAPACITY);
    if (!buffer) {
        perror("Не удалось выделить память");
        fclose(file);
        return;
    }

    size_t bytesRead = fread(buffer, 1, LOG_CAPACITY - 1, file);
    if (ferror(file)) {
        perror("Ошибка при чтении файла логов");
        free(buffer);
        fclose(file);
        return;
    }

    buffer[bytesRead] = '\0';  // Гарантированный null-терминатор
    fclose(file);
    write_to_log(buffer);  // Записываем всё в лог

    free(buffer);
}


// Сохранение лога в файл
void save_log_to_file() {
    FILE* log_file = fopen("duplicate_log.txt", "w"); // Открываем для записывания
    if (log_file) {
        fwrite(log_buffer, 1, log_length, log_file);
        fclose(log_file);
    }
}

void scanDirectory(const char* dirPath) {
    dir_count = 0;
    struct dirent* entry;
    DIR* dir = opendir(dirPath);
    if (!dir) {
        dir_count = 0;
        return;
    }

    struct stat fileStat;
    char filePath[MAX_PATH_LENGTH];

    while ((entry = readdir(dir)) && dir_count < MAX_DIRS) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(filePath, sizeof(filePath), "%s/%s", dirPath, entry->d_name);
        if (stat(filePath, &fileStat) == -1) continue;

        strncpy(dir_entries[dir_count].name, filePath, MAX_PATH_LENGTH);
        strncpy(dir_entries[dir_count].display_name, entry->d_name, 255);
        dir_entries[dir_count].is_dir = S_ISDIR(fileStat.st_mode);
        dir_count++;
    }
    closedir(dir);
}

void truncate_name(const char* name, int max_len, char* buf) {
    int len = strlen(name);
    if (len <= max_len) {
        strcpy(buf, name);
    } else {
        strncpy(buf, name, max_len - 3);
        buf[max_len - 3] = '\0';
        strcat(buf, "...");
    }
}

void draw_directory_tree() {
    werase(tree_win);
    box(tree_win, 0, 0);
    mvwprintw(tree_win, 0, 2, " Directory Tree ");
    
    int win_height, win_width;
    getmaxyx(tree_win, win_height, win_width);
    
    int usable_height = win_height - 2;
    int col_width = 30;
    int cols = (win_width - 4) / col_width;
    if (cols < 1) cols = 1;
    
    int items_per_col = usable_height - 1;
    if (items_per_col < 1) items_per_col = 1;
    
    char current_dir_display[col_width];
    truncate_name(current_dir, col_width - 6, current_dir_display);
    
    if (cursor_pos == 0 && tree_focused) {
        wattron(tree_win, A_REVERSE);
    }
    mvwprintw(tree_win, 1, 1, "--> %s", current_dir_display);
    if (cursor_pos == 0 && tree_focused) {
        wattroff(tree_win, A_REVERSE);
    }
    
    for (int i = 0; i < dir_count; i++) {
        int col = i / items_per_col;
        if (col >= cols) break;
        
        int row = i % items_per_col + 2;
        if (row >= win_height - 1) continue;
        
        int x_pos = 4 + col * col_width;
        
        char display_name[col_width - 4];
        truncate_name(dir_entries[i].display_name, col_width - 4, display_name);
        
        if (i == cursor_pos - 1 && tree_focused) {
            wattron(tree_win, A_REVERSE);
        }
        
        mvwprintw(tree_win, row, x_pos, "%s %s", 
                 dir_entries[i].is_dir ? "D" : "F", 
                 display_name);
        
        if (i == cursor_pos - 1 && tree_focused) {
            wattroff(tree_win, A_REVERSE);
        }
    }
    
    if (dir_count == 0) {
        mvwprintw(tree_win, 2, 4, "(пусто)");
    }
    
    if (cursor_pos > dir_count) {
        cursor_pos = dir_count;
    }
}

void draw_interface() {
    clear();
    box(stdscr, 0, 0);
    mvprintw(0, 2, " CurseFind by kruazan ");
    
    // Exit button
    if (selected_item == 2 && !tree_focused) {
        attron(A_REVERSE);
    }
    mvprintw(0, COLS - 10, "[ Exit ]");
    attroff(A_REVERSE);
    
    mvhline(3, 1, ACS_HLINE, COLS - 2);
    
    // Main window
    main_win = newwin(5, COLS - 2, 4, 1);
    box(main_win, 0, 0);
    mvwprintw(main_win, 1, 1, " Current directory:");
    mvwprintw(main_win, 2, 1, "%s", current_dir);
    
    if (selected_item == 1 && !tree_focused) {
        wattron(main_win, A_REVERSE);
    }
    mvwprintw(main_win, 4, 1, "[ Start ]");
    wattroff(main_win, A_REVERSE);
    
    // Directory tree window
    tree_win = newwin(LINES * 2 / 3 - 8, COLS - 2, 10, 1);
    draw_directory_tree();
    
    log_win = newwin(LINES / 3 - 2, COLS - 2, LINES * 2 / 3 + 2, 1);
    write_in_logWin();
    
    // Выводим текущее содержимое буфера
    // if (log_length > 0) {
    //     wprintw(log_win, "%s", log_buffer);
    // }


    
    refresh();
    wrefresh(main_win);
    wrefresh(tree_win);
    wrefresh(log_win);
}

void free_hash_table() {
    FileNode* current = hashTable;
    while (current) {
        FileNode* next = current->next;
        free(current);
        current = next;
    }
    hashTable = NULL;
}

void process_directory(const char* dirPath) {
    // Ваш код обработки директории
    write_to_log(" Directory processing: %s\n", dirPath);

    char command[512];
    snprintf(command, sizeof(command), "./curseFindUtility %s", dirPath);
    system(command);

    read_log_from_file();
    wrefresh(log_win);
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    save_log_to_file();
    
    if (argc > 1) {
        strncpy(current_dir, argv[1], MAX_PATH_LENGTH - 1);
    } else {
        getcwd(current_dir, MAX_PATH_LENGTH);
    }
    
    scanDirectory(current_dir);
    
    int ch;
    bool running = true;
    
    while (running) {
        draw_interface();
        ch = getch();
        
        switch (ch) {
            case KEY_UP:
                if (tree_focused && cursor_pos > 0) {
                    cursor_pos--;
                } else if (!tree_focused) {
                    selected_item = (selected_item == 1) ? 2 : 1;
                }
                break;
                
            case KEY_DOWN:
                if (tree_focused && cursor_pos < dir_count) {
                    cursor_pos++;
                } else if (!tree_focused) {
                    selected_item = (selected_item == 1) ? 2 : 1;
                }
                break;
                
            case KEY_LEFT:
                if (!tree_focused) {
                    selected_item = (selected_item == 1) ? 2 : 1;
                }
                break;
                
            case KEY_RIGHT:
                if (!tree_focused) {
                    selected_item = (selected_item == 1) ? 2 : 1;
                }
                break;
                
            case '\t':
                tree_focused = !tree_focused;
                if (!tree_focused) selected_item = 1;
                break;
                
                case 10: // Enter
            if (tree_focused) {
                if (cursor_pos == 0) {
                    char parent_dir[MAX_PATH_LENGTH];
                    strncpy(parent_dir, current_dir, MAX_PATH_LENGTH);
                    char* last_slash = strrchr(parent_dir, '/');
                    if (last_slash != NULL) {
                        if (last_slash == parent_dir) {
                            *(last_slash + 1) = '\0';
                        } else {
                            *last_slash = '\0';
                        }
                        strncpy(current_dir, parent_dir, MAX_PATH_LENGTH);
                        scanDirectory(current_dir);
                        cursor_pos = 0;
                    }
                }
                else if (cursor_pos-1 < dir_count && dir_entries[cursor_pos-1].is_dir) {
                    strncpy(current_dir, dir_entries[cursor_pos-1].name, MAX_PATH_LENGTH);
                    scanDirectory(current_dir);
                    cursor_pos = 0;
                }
            } else {
                if (selected_item == 1) { // Start
                    process_directory(current_dir);
                } else if (selected_item == 2) { // Exit
                    running = false;
                    break;
                }
            }
            break;
                
            case 27: // ESC
                running = false;
                break;
        }
    }
    
    save_log_to_file();
    endwin();
    free_hash_table();
    return 0;
}