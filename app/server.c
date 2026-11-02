#include <fcgi_stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    // Бесконечный цикл обработки запросов
    while (FCGI_Accept() >= 0) {
        // HTTP-заголовки
        printf("Content-type: text/html\r\n\r\n");
        
        // HTML-страницу
        printf("<html>");
        printf("<head><title>Hello, World!</title></head>");
        printf("<body>");
        printf("<h1>Hello, World!</h1>");
        printf("<p>This is a FastCGI server running on port 8080</p>");
        printf("</body>");
        printf("</html>");
    }
    
    return 0;
}
